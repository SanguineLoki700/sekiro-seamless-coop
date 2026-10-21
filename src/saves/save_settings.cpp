#include "skcoop/save_settings.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace skcoop::saves {
namespace {

[[noreturn]] void invalid(const char* reason) { throw std::invalid_argument(reason); }

std::wstring_view trim(std::wstring_view value) {
    const auto first = value.find_first_not_of(L" \t\r");
    if (first == std::wstring_view::npos) return {};
    return value.substr(first, value.find_last_not_of(L" \t\r") - first + 1);
}

bool equal_windows(std::wstring_view value, std::wstring_view expected) {
    return value.size() == expected.size() &&
        CompareStringOrdinal(value.data(), static_cast<int>(value.size()),
                             expected.data(), static_cast<int>(expected.size()), TRUE) == CSTR_EQUAL;
}

bool reserved_device(std::wstring_view component) {
    const auto stem = component.substr(0, component.find(L'.'));
    for (const auto name : {L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$"})
        if (equal_windows(stem, name)) return true;
    if (stem.size() == 4 && (equal_windows(stem.substr(0, 3), L"COM") ||
                             equal_windows(stem.substr(0, 3), L"LPT"))) {
        const auto digit = stem[3];
        return (digit >= L'1' && digit <= L'9') || digit == L'\u00b9' ||
               digit == L'\u00b2' || digit == L'\u00b3';
    }
    return false;
}

bool separator(wchar_t value) { return value == L'\\' || value == L'/'; }

bool valid_root(std::wstring_view root) {
    if (root.empty()) return true;
    if (root.size() <= 3 || root.size() > 32767 || root[1] != L':' || !separator(root[2]) ||
        !((root[0] >= L'A' && root[0] <= L'Z') || (root[0] >= L'a' && root[0] <= L'z')) ||
        root.find(L'%') != std::wstring_view::npos) return false;
    auto end = root.size();
    if (separator(root.back())) {
        --end;
        if (end == 3 || separator(root[end - 1])) return false;
    }
    for (std::size_t begin = 3; begin < end;) {
        auto at = root.find_first_of(L"\\/", begin);
        if (at == std::wstring_view::npos || at > end) at = end;
        const auto component = root.substr(begin, at - begin);
        if (component.empty() || component.size() > 255 || component == L"." || component == L".." ||
            component.back() == L'.' || component.back() == L' ' || reserved_device(component)) return false;
        for (const auto unit : component)
            if (static_cast<std::uint16_t>(unit) < 32 ||
                std::wstring_view(L"<>:\"|?*").find(unit) != std::wstring_view::npos) return false;
        begin = at + 1;
    }
    return true;
}

bool valid_extension(std::wstring_view value) {
    return !value.empty() && value.size() <= 120 && !equal_windows(value, L"sl2") &&
        std::all_of(value.begin(), value.end(), [](wchar_t c) {
            return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9');
        });
}

std::wstring decode_utf8(std::string_view input) {
    if (input.size() > max_save_settings_bytes) invalid("Save settings exceed 64 KiB.");
    if (input.find('\0') != std::string_view::npos) invalid("NUL is not allowed in save settings.");
    if (input.starts_with("\xef\xbb\xbf")) input.remove_prefix(3);
    if (input.empty()) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                          static_cast<int>(input.size()), nullptr, 0);
    if (count == 0) invalid("Save settings are not valid UTF-8.");
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()),
                            output.data(), count) != count) invalid("Cannot decode save settings UTF-8.");
    return output;
}

} // namespace

SaveSettings parse_save_settings(std::string_view utf8) {
    const auto text = decode_utf8(utf8);
    SaveSettings result;
    bool section{}, extension{}, mode{}, root{};
    for (std::size_t begin = 0; begin < text.size();) {
        auto end = text.find(L'\n', begin);
        if (end == std::wstring::npos) end = text.size();
        const auto line = trim(std::wstring_view(text).substr(begin, end - begin));
        begin = end + 1;
        if (line.empty() || line.front() == L';' || line.front() == L'#') continue;
        if (line.front() == L'[') {
            if (line != L"[SAVE]" || section) invalid("Expected exactly one [SAVE] section.");
            section = true;
            continue;
        }
        const auto split = line.find(L'=');
        if (!section || split == std::wstring_view::npos) invalid("Malformed save settings key.");
        const auto key = trim(line.substr(0, split));
        const auto value = trim(line.substr(split + 1));
        if (key == L"save_file_extension") {
            if (extension || !valid_extension(value)) invalid("Duplicate or invalid save_file_extension.");
            extension = true;
            result.extension = value;
        } else if (key == L"destination_mode") {
            if (mode) invalid("Duplicate destination_mode.");
            mode = true;
            if (value == L"separate_profile") result.mode = DestinationMode::separate_profile;
            else if (value == L"same_account_directory") result.mode = DestinationMode::same_account_directory;
            else invalid("Unknown destination_mode.");
        } else if (key == L"coop_profile_root") {
            if (root || !valid_root(value)) invalid("Duplicate or invalid coop_profile_root.");
            root = true;
            result.coop_profile_root = value;
        } else invalid("Unknown save settings key.");
    }
    if (!section || !extension || !mode || !root) invalid("All three [SAVE] keys are required.");
    return result;
}

SaveSettings read_save_settings(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open save settings configuration.");
    std::string bytes(max_save_settings_bytes + 1, '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto count = input.gcount();
    if (input.bad() || count < 0) throw std::runtime_error("Cannot read save settings configuration.");
    bytes.resize(static_cast<std::size_t>(count));
    return parse_save_settings(bytes);
}

} // namespace skcoop::saves
