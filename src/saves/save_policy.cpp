#include "skcoop/save_policy.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace skcoop::saves {
namespace {

constexpr std::size_t max_path_units = 32767;
constexpr std::size_t max_component_units = 255;

struct ParsedPath {
    wchar_t drive{};
    std::vector<std::wstring> components;
};

bool is_separator(wchar_t value) noexcept {
    return value == L'\\' || value == L'/';
}

bool equal_windows(std::wstring_view left, std::wstring_view right) noexcept {
    if (left.size() != right.size()) return false;
    if (left.empty()) return true;
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                                right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

bool reserved_device(std::wstring_view component) noexcept {
    const auto stem = component.substr(0, component.find(L'.'));
    for (const auto name : {L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$"}) {
        if (equal_windows(stem, name)) return true;
    }
    if (stem.size() == 4 &&
        (equal_windows(stem.substr(0, 3), L"COM") || equal_windows(stem.substr(0, 3), L"LPT"))) {
        const auto number = stem[3];
        if ((number >= L'1' && number <= L'9') || number == L'\u00b9' ||
            number == L'\u00b2' || number == L'\u00b3') return true;
    }
    return false;
}

bool valid_component(std::wstring_view value) noexcept {
    if (value.empty() || value.size() > max_component_units || value == L"." || value == L".." ||
        value.back() == L'.' || value.back() == L' ') return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto unit = static_cast<std::uint16_t>(value[i]);
        if (unit < 32 || std::wstring_view(L"<>:\"/\\|?*").find(value[i]) != std::wstring_view::npos)
            return false;
        // Reject ill-formed UTF-16 rather than relying on later Win32 handling.
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (i + 1 == value.size()) return false;
            const auto next = static_cast<std::uint16_t>(value[++i]);
            if (next < 0xdc00 || next > 0xdfff) return false;
        } else if (unit >= 0xdc00 && unit <= 0xdfff) {
            return false;
        }
    }
    return !reserved_device(value);
}

bool parse_path(std::wstring_view path, bool directory, ParsedPath& parsed) {
    if (path.size() < 3 || path.size() > max_path_units || path[1] != L':' || !is_separator(path[2]))
        return false;
    const auto drive = path[0];
    if (!((drive >= L'A' && drive <= L'Z') || (drive >= L'a' && drive <= L'z'))) return false;
    parsed.drive = drive >= L'a' && drive <= L'z' ? static_cast<wchar_t>(drive - L'a' + L'A') : drive;
    auto end = path.size();
    if (end > 3 && is_separator(path.back())) {
        if (!directory) return false;
        --end; // One trailing directory separator is accepted; duplicate ones are not.
        if (is_separator(path[end - 1])) return false;
    }
    for (std::size_t begin = 3; begin < end;) {
        auto separator = path.find_first_of(L"\\/", begin);
        if (separator == std::wstring_view::npos || separator > end) separator = end;
        const auto component = path.substr(begin, separator - begin);
        if (!valid_component(component)) return false;
        parsed.components.emplace_back(component);
        begin = separator + 1;
    }
    return !parsed.components.empty();
}

bool is_prefix(const ParsedPath& prefix, const ParsedPath& path) noexcept {
    if (prefix.drive != path.drive || prefix.components.size() > path.components.size()) return false;
    for (std::size_t i = 0; i < prefix.components.size(); ++i)
        if (!equal_windows(prefix.components[i], path.components[i])) return false;
    return true;
}

bool same_path(const ParsedPath& left, const ParsedPath& right) noexcept {
    return left.components.size() == right.components.size() && is_prefix(left, right);
}

std::wstring serialize(const ParsedPath& path) {
    std::wstring result;
    result.push_back(path.drive);
    result += L":\\";
    for (std::size_t i = 0; i < path.components.size(); ++i) {
        if (i != 0) result.push_back(L'\\');
        result += path.components[i];
    }
    return result;
}

bool valid_account(std::wstring_view account) noexcept {
    if (account.empty() || account.front() < L'1' || account.front() > L'9' || account.size() > 20)
        return false;
    std::uint64_t value{};
    for (const auto digit : account) {
        if (digit < L'0' || digit > L'9') return false;
        const auto number = static_cast<std::uint64_t>(digit - L'0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - number) / 10) return false;
        value = value * 10 + number;
    }
    return value != 0;
}

bool valid_extension(std::wstring_view extension) noexcept {
    if (extension.empty() || extension.size() > 120 || equal_windows(extension, L"sl2")) return false;
    return std::all_of(extension.begin(), extension.end(), [](wchar_t value) {
        return (value >= L'a' && value <= L'z') || (value >= L'A' && value <= L'Z') ||
               (value >= L'0' && value <= L'9');
    });
}

bool nonzero_hash(const ExecutableSha256& digest) noexcept {
    return std::any_of(digest.begin(), digest.end(), [](auto value) { return value != 0; });
}

SavePathDecision reject(SavePolicyError error) {
    return {error, SaveFileKind::primary, {}, {}};
}

} // namespace

SavePathDecision decide_save_path(std::wstring_view source,
                                 const SavePolicyConfig& config,
                                 const SaveProcessContext& context) {
    if (!context.cooperative_profile_opt_in) return reject(SavePolicyError::not_opted_in);
    if (!context.is_selected_game_process || context.diagnostic_only || context.process_id == 0 ||
        config.selected_game_process_id == 0 || context.process_id != config.selected_game_process_id)
        return reject(SavePolicyError::process_not_allowed);
    if (!nonzero_hash(config.expected_executable_sha256) || !nonzero_hash(context.observed_executable_sha256) ||
        config.expected_executable_sha256 != context.observed_executable_sha256)
        return reject(SavePolicyError::game_identity_mismatch);
    if (!valid_extension(config.extension)) return reject(SavePolicyError::invalid_extension);

    ParsedPath appdata;
    ParsedPath profile;
    if (!parse_path(config.appdata_root, true, appdata) || !parse_path(config.coop_profile_root, true, profile))
        return reject(SavePolicyError::invalid_configuration);
    auto source_tree = appdata;
    source_tree.components.emplace_back(L"Sekiro");
    auto destination_tree = profile;
    destination_tree.components.emplace_back(L"Sekiro");
    switch (config.destination_mode) {
    case DestinationMode::separate_profile:
        if (is_prefix(source_tree, destination_tree) || is_prefix(destination_tree, source_tree))
            return reject(SavePolicyError::overlapping_save_trees);
        break;
    case DestinationMode::same_account_directory:
        if (!same_path(appdata, profile)) return reject(SavePolicyError::invalid_configuration);
        break;
    default:
        return reject(SavePolicyError::invalid_configuration);
    }

    ParsedPath input;
    if (!parse_path(source, false, input)) return reject(SavePolicyError::invalid_source_path);
    if (!is_prefix(source_tree, input) || input.components.size() != source_tree.components.size() + 2)
        return reject(SavePolicyError::outside_save_namespace);
    const auto& account = input.components[source_tree.components.size()];
    if (!valid_account(account)) return reject(SavePolicyError::invalid_account);
    const auto& name = input.components.back();
    SaveFileKind kind;
    if (equal_windows(name, L"S0000.sl2")) kind = SaveFileKind::primary;
    else if (equal_windows(name, L"S0000.sl2.bak")) kind = SaveFileKind::backup;
    else return reject(SavePolicyError::unsupported_filename);

    destination_tree.components.push_back(account);
    auto filename = L"S0000." + config.extension;
    if (kind == SaveFileKind::backup) filename += L".bak";
    destination_tree.components.push_back(std::move(filename));
    auto destination = serialize(destination_tree);
    if (destination.size() > max_path_units) return reject(SavePolicyError::destination_too_long);
    return {SavePolicyError::none, kind, account, std::move(destination)};
}

} // namespace skcoop::saves
