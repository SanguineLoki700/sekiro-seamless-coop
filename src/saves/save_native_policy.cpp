#include "skcoop/save_native_policy.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <limits>
#include <utility>
#include <vector>

namespace skcoop::saves {
namespace {

bool equal_windows(std::wstring_view a, std::wstring_view b) noexcept {
    return a.size() == b.size() && a.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
        (a.empty() || CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
            b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL);
}

bool separator(wchar_t c) noexcept { return c == L'\\' || c == L'/'; }

bool drive_absolute(std::wstring_view path) noexcept {
    return path.size() >= 3 && path[1] == L':' && separator(path[2]) &&
        ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z'));
}

std::wstring join(std::wstring_view root, std::wstring_view suffix) {
    std::wstring result(root);
    if (!result.empty() && !separator(result.back())) result += L'\\';
    result += suffix;
    return result;
}

NativePathDecision denied(NativePathError error, SavePolicyError policy = SavePolicyError::none) {
    NativePathDecision result;
    result.error = error;
    result.policy_error = policy;
    return result;
}

NativePathDecision unrelated() {
    NativePathDecision result;
    result.action = NativePathAction::passthrough;
    result.error = NativePathError::none;
    return result;
}

NativePathDecision redirected(SavePathDecision decision) {
    if (!decision) return denied(NativePathError::policy_rejected, decision.error);
    NativePathDecision result;
    result.action = NativePathAction::redirect;
    result.error = NativePathError::none;
    result.kind = decision.kind;
    result.account_id = std::move(decision.account_id);
    result.destination = std::move(decision.destination);
    return result;
}

// These deliberately permissive transformations are ONLY for recognizing and
// denying ambiguous candidates. Acceptance always goes through decide_save_path.
// They must never become a way to authorize normalized '.'/'..'/alias inputs.
std::wstring classification_path(std::wstring_view input, bool resolve_dots) {
    std::wstring path(input.substr(0, input.find(L'\0')));
    for (auto& c : path) if (c == L'/') c = L'\\';
    for (const auto prefix : {L"\\\\?\\", L"\\\\.\\", L"\\??\\"}) {
        const std::wstring_view value(prefix);
        if (path.starts_with(value)) { path.erase(0, value.size()); break; }
    }
    std::wstring prefix;
    std::size_t begin{};
    if (drive_absolute(path)) { prefix = path.substr(0, 2); begin = 3; }
    else if (!path.empty() && path.front() == L'\\') { prefix = L"\\"; begin = 1; }
    std::vector<std::wstring> components;
    while (begin < path.size()) {
        auto end = path.find(L'\\', begin);
        if (end == std::wstring::npos) end = path.size();
        auto part = path.substr(begin, end - begin);
        begin = end + 1;
        if (part.empty()) continue;
        if (resolve_dots && part == L".") continue;
        if (resolve_dots && part == L"..") {
            if (!components.empty() && components.back() != L"..") components.pop_back();
            else components.push_back(part);
            continue;
        }
        if (part != L"." && part != L"..")
            while (!part.empty() && (part.back() == L'.' || part.back() == L' ')) part.pop_back();
        components.push_back(std::move(part));
    }
    std::wstring result(prefix);
    for (const auto& part : components) {
        if (!result.empty() && result.back() != L'\\') result += L'\\';
        result += part;
    }
    return result;
}

bool below(std::wstring_view tree, std::wstring_view path) noexcept {
    return path.size() >= tree.size() && equal_windows(path.substr(0, tree.size()), tree) &&
        (path.size() == tree.size() || path[tree.size()] == L'\\');
}

bool extension_candidate(std::wstring_view component, std::wstring_view extension) noexcept {
    for (std::size_t i = 0; i < component.size(); ++i) {
        if (component[i] != L'.' || component.size() - i - 1 < extension.size() ||
            !equal_windows(component.substr(i + 1, extension.size()), extension)) continue;
        const auto end = i + 1 + extension.size();
        if (end == component.size()) return true;
        const auto next = component[end];
        // Includes .bak, ADS, trailing punctuation/space and embedded NUL.
        if (!((next >= L'a' && next <= L'z') || (next >= L'A' && next <= L'Z') ||
              (next >= L'0' && next <= L'9'))) return true;
    }
    return false;
}

bool save_candidate(std::wstring_view path, std::wstring_view extension) noexcept {
    for (std::size_t begin = 0; begin < path.size();) {
        auto end = path.find_first_of(L"\\/", begin);
        if (end == std::wstring_view::npos) end = path.size();
        const auto component = path.substr(begin, end - begin);
        if (extension_candidate(component, L"sl2") || extension_candidate(component, L"co2") ||
            extension_candidate(component, extension) ||
            (component.size() >= 6 && equal_windows(component.substr(0, 6), L"S0000."))) return true;
        begin = end + 1;
    }
    return false;
}

NativePathPairDecision denied_pair(NativePathError error, SavePolicyError policy = SavePolicyError::none) {
    NativePathPairDecision result;
    result.error = error;
    result.source = denied(error, policy);
    result.target = denied(error, policy);
    return result;
}

} // namespace

NativePathDecision decide_native_save_path(std::wstring_view path, const SavePolicyConfig& config,
    const SaveProcessContext& context, std::wstring_view bound_account) {
    const auto source_directory = join(join(config.appdata_root, L"Sekiro"), bound_account);
    const auto bound = decide_save_path(join(source_directory, L"S0000.sl2"), config, context);
    if (!bound) return denied(NativePathError::invalid_binding, bound.error);

    const auto solo = decide_save_path(path, config, context);
    if (solo) {
        if (solo.account_id != bound_account) return denied(NativePathError::account_mismatch);
        return redirected(solo);
    }

    auto reverse = config;
    reverse.appdata_root = config.coop_profile_root;
    reverse.coop_profile_root = config.appdata_root;
    const auto private_solo = decide_save_path(path, reverse, context);
    if (private_solo) {
        if (private_solo.account_id != bound_account) return denied(NativePathError::account_mismatch);
        return redirected(decide_save_path(join(source_directory,
            private_solo.kind == SaveFileKind::backup ? L"S0000.sl2.bak" : L"S0000.sl2"), config, context));
    }

    // Validate an already cooperative path by replacing ONLY its exact expected
    // filename and applying the same strict lexical policy with swapped roots.
    const auto leaf = path.find_last_of(L"\\/");
    if (leaf != std::wstring_view::npos) {
        const auto filename = path.substr(leaf + 1);
        const auto primary = L"S0000." + config.extension;
        const bool is_primary = equal_windows(filename, primary);
        const bool is_backup = equal_windows(filename, primary + L".bak");
        if (is_primary || is_backup) {
            auto logical = std::wstring(path.substr(0, leaf + 1));
            logical += is_backup ? L"S0000.sl2.bak" : L"S0000.sl2";
            const auto existing = decide_save_path(logical, reverse, context);
            if (existing) {
                if (existing.account_id != bound_account) return denied(NativePathError::account_mismatch);
                return redirected(decide_save_path(join(source_directory,
                    is_backup ? L"S0000.sl2.bak" : L"S0000.sl2"), config, context));
            }
        }
    }

    const auto source_tree = classification_path(join(config.appdata_root, L"Sekiro"), true);
    const auto target_tree = classification_path(join(config.coop_profile_root, L"Sekiro"), true);
    const auto literal = classification_path(path, false);
    const auto resolved = classification_path(path, true);
    const bool inside = below(source_tree, literal) || below(target_tree, literal) ||
        below(source_tree, resolved) || below(target_tree, resolved);
    const bool candidate = save_candidate(path, config.extension);
    const bool wildcard = path.find_first_of(L"*?") != std::wstring_view::npos;
    if (inside && (candidate || wildcard))
        return denied(NativePathError::policy_rejected, solo.error);
    auto name = path.substr(leaf == std::wstring_view::npos ? 0 : leaf + 1);
    // Recognize Win32 trailing-dot/space aliases and ADS syntax for denial only.
    // The accepted source/destination paths above remain strictly lexical.
    name = name.substr(0, name.find_first_of(std::wstring_view(L":\0", 2)));
    while (!name.empty() && (name.back() == L'.' || name.back() == L' ')) name.remove_suffix(1);
    if (equal_windows(name, L"S0000.sl2") || equal_windows(name, L"S0000.sl2.bak") ||
        equal_windows(name, L"S0000.co2") || equal_windows(name, L"S0000.co2.bak") ||
        equal_windows(name, L"S0000." + config.extension) || equal_windows(name, L"S0000." + config.extension + L".bak"))
        return denied(NativePathError::ambiguous_save_path, solo.error);
    if (candidate && (!drive_absolute(path) || path.find(L'~') != std::wstring_view::npos))
        return denied(NativePathError::ambiguous_save_path, solo.error);
    return unrelated();
}

NativePathPairDecision decide_native_save_pair(std::wstring_view source, std::wstring_view target,
    const SavePolicyConfig& config, const SaveProcessContext& context, std::wstring_view bound_account) {
    auto from = decide_native_save_path(source, config, context, bound_account);
    auto to = decide_native_save_path(target, config, context, bound_account);
    if (from.action == NativePathAction::deny) return denied_pair(from.error, from.policy_error);
    if (to.action == NativePathAction::deny) return denied_pair(to.error, to.policy_error);
    if (from.action != to.action) return denied_pair(NativePathError::mixed_namespaces);
    if (from.action == NativePathAction::redirect && equal_windows(from.destination, to.destination))
        return denied_pair(NativePathError::same_destination);
    NativePathPairDecision result;
    result.action = from.action;
    result.error = NativePathError::none;
    result.source = std::move(from);
    result.target = std::move(to);
    return result;
}

} // namespace skcoop::saves
