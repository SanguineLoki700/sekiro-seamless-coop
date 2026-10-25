#include "launch_settings.h"
#include "skcoop/lan/protocol.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace skcoop::launcher {
namespace {
[[noreturn]] void invalid(const char* message) { throw std::invalid_argument(message); }

std::wstring_view trim(std::wstring_view text) {
    const auto first = text.find_first_not_of(L" \t\r");
    if (first == std::wstring_view::npos) return {};
    return text.substr(first, text.find_last_not_of(L" \t\r") - first + 1);
}

std::wstring decode_utf8(std::string_view bytes) {
    if (bytes.size() > max_launch_settings_bytes) invalid("Launch settings exceed 64 KiB.");
    if (bytes.find('\0') != std::string_view::npos) invalid("NUL is not allowed in launch settings.");
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
    if (bytes.empty()) return {};
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), nullptr, 0);
    if (!length) invalid("Launch settings are not valid UTF-8.");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()),
        result.data(), length) != length) invalid("Cannot decode launch settings UTF-8.");
    return result;
}

bool equal_windows(std::wstring_view left, std::wstring_view right) {
    return left.size() == right.size() && CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
        right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

bool reserved_device(std::wstring_view component) {
    const auto stem = component.substr(0, component.find(L'.'));
    for (const auto name : {L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$"})
        if (equal_windows(stem, name)) return true;
    if (stem.size() == 4 && (equal_windows(stem.substr(0, 3), L"COM") || equal_windows(stem.substr(0, 3), L"LPT"))) {
        const auto number = stem[3];
        return (number >= L'1' && number <= L'9') || number == L'\u00b9' || number == L'\u00b2' || number == L'\u00b3';
    }
    return false;
}

bool separator(wchar_t value) { return value == L'\\' || value == L'/'; }

bool valid_path(std::wstring_view path) {
    if (path.size() <= 3 || path.size() >= max_launch_path_units || path[1] != L':' || !separator(path[2]) ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
        path.find(L'%') != std::wstring_view::npos) return false;
    const auto end = path.size();
    if (separator(path.back())) return false;
    for (std::size_t begin = 3; begin < end;) {
        auto at = path.find_first_of(L"\\/", begin);
        if (at == std::wstring_view::npos || at > end) at = end;
        const auto component = path.substr(begin, at - begin);
        if (component.empty() || component.size() > 255 || component == L"." || component == L".." ||
            component.back() == L'.' || component.back() == L' ' || reserved_device(component)) return false;
        for (const auto unit : component)
            if (static_cast<std::uint16_t>(unit) < 32 || unit == 127 ||
                std::wstring_view(L"<>:\"|?*").find(unit) != std::wstring_view::npos) return false;
        begin = at + 1;
    }
    return true;
}

bool valid_account(std::wstring_view account) {
    if (account.empty() || account.size() > 20 || account.front() < L'1' || account.front() > L'9') return false;
    std::uint64_t number{};
    for (const auto digit : account) {
        if (digit < L'0' || digit > L'9') return false;
        const auto next = static_cast<std::uint64_t>(digit - L'0');
        if (number > (std::numeric_limits<std::uint64_t>::max() - next) / 10) return false;
        number = number * 10 + next;
    }
    return number != 0;
}
} // namespace

LaunchSettings parse_launch_settings(std::string_view utf8) {
    const auto text = decode_utf8(utf8);
    LaunchSettings result;
    bool section{};
    unsigned seen{};
    for (std::size_t begin = 0; begin < text.size();) {
        auto end = text.find(L'\n', begin);
        if (end == std::wstring::npos) end = text.size();
        const auto line = trim(std::wstring_view(text).substr(begin, end - begin));
        begin = end + 1;
        if (line.empty() || line.front() == L';' || line.front() == L'#') continue;
        if (line.front() == L'[') {
            if (section || line != L"[LAUNCH]") invalid("Expected exactly one [LAUNCH] section.");
            section = true;
            continue;
        }
        const auto split = line.find(L'=');
        if (!section || split == std::wstring_view::npos) invalid("Malformed launch settings key.");
        const auto key = trim(line.substr(0, split));
        const auto value = trim(line.substr(split + 1));
        unsigned bit{};
        if (key == L"test_executable") bit = 1;
        else if (key == L"coop_profile_root") bit = 2;
        else if (key == L"account") bit = 4;
        else if (key == L"receipt_directory") bit = 8;
        else if (key == L"actor_test") bit = 16;
        else if (key == L"lan_role") bit = 32;
        else if (key == L"lan_code") bit = 64;
        else if (key == L"lan_profile") bit = 128;
        else invalid("Unknown launch settings key.");
        if (seen & bit) invalid("Duplicate launch settings key.");
        if (bit == 16) {
            if (value != L"off" && value != L"follow" && value != L"locomotion" && value != L"pad-reference" && value != L"graph-locomotion" && value != L"lan") invalid("Unknown actor_test mode.");
            result.actor_follow_test = value != L"off";
            result.actor_locomotion_test = value == L"locomotion";
            result.actor_pad_reference_test = value == L"pad-reference";
            result.actor_graph_locomotion_test = value == L"graph-locomotion";
            result.actor_lan_test = value == L"lan";
        } else if(bit==128) {
            if(value!=L"independent")invalid("Unknown LAN profile mode.");result.independent_profile=true;
        } else if(bit==32) {
            if(value!=L"host"&&value!=L"guest") invalid("LAN role must be host or guest.");
            result.lan_role=value==L"host"?1u:2u;
        } else if(bit==64) {
            if(value.size()>=160 || std::any_of(value.begin(),value.end(),[](wchar_t c){return c<33||c>126;}))invalid("Invalid LAN code.");
            for(const auto c:value)result.lan_code.push_back(static_cast<char>(c));
            if(!lan::parse_ticket(result.lan_code))invalid("Invalid LAN code.");
        } else if (bit == 4) {
            if (!valid_account(value)) invalid("Account must be a positive canonical decimal uint64.");
            result.account = value;
        } else {
            if (!valid_path(value)) invalid("Launch path must be an unambiguous absolute local drive path shorter than 1024 UTF-16 units, without a trailing separator.");
            if (bit == 1) result.test_executable = value;
            else if (bit == 2) result.coop_profile_root = value;
            else result.receipt_directory = value;
        }
        seen |= bit;
    }
    if (!section || (seen & 15) != 15) invalid("All four [LAUNCH] keys are required.");
    if(result.actor_lan_test ? (seen&96)!=96 : (seen&96)!=0)invalid("LAN mode requires both LAN settings; other modes forbid them.");
    if(!result.actor_lan_test&&(seen&128))invalid("Independent profile requires LAN mode.");
    return result;
}

LaunchSettings read_launch_settings(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open launch settings configuration.");
    std::string bytes(max_launch_settings_bytes + 1, '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto length = input.gcount();
    if (input.bad() || length < 0) throw std::runtime_error("Cannot read launch settings configuration.");
    bytes.resize(static_cast<std::size_t>(length));
    return parse_launch_settings(bytes);
}
} // namespace skcoop::launcher
