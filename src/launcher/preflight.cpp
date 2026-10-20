#include "preflight.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace skcoop::launcher {
namespace {
class File {
public:
    explicit File(const std::filesystem::path& path) {
        handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open selected file for stable read-only inspection.");
        LARGE_INTEGER length{};
        if (!GetFileSizeEx(handle, &length) || length.QuadPart < 0) {
            CloseHandle(handle);
            throw std::runtime_error("Cannot determine selected file size.");
        }
        size = static_cast<std::uint64_t>(length.QuadPart);
    }
    ~File() { CloseHandle(handle); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    void read(std::uint64_t at, void* destination, DWORD count) const {
        if (at > size || count > size - at) throw std::runtime_error("Truncated PE image.");
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(at);
        DWORD received = 0;
        if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) ||
            !ReadFile(handle, destination, count, &received, nullptr) || received != count)
            throw std::runtime_error("Cannot read selected file.");
    }
    HANDLE handle;
    std::uint64_t size{};
};
void crypto_check(NTSTATUS status) {
    if (status < 0) throw std::runtime_error("Windows BCrypt SHA-256 operation failed.");
}
std::string hash_open_file(const File& file) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<UCHAR> object;
    try {
        crypto_check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
        DWORD object_size = 0, returned = 0;
        crypto_check(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &returned, 0));
        object.resize(object_size);
        crypto_check(BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0));
        std::vector<UCHAR> buffer(1024 * 1024);
        for (std::uint64_t at = 0; at < file.size;) {
            const auto length = static_cast<DWORD>(std::min<std::uint64_t>(buffer.size(), file.size - at));
            file.read(at, buffer.data(), length);
            crypto_check(BCryptHashData(hash, buffer.data(), length, 0));
            at += length;
        }
        std::array<UCHAR, 32> digest{};
        crypto_check(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0));
        BCryptDestroyHash(hash);
        hash = nullptr;
        BCryptCloseAlgorithmProvider(algorithm, 0);
        algorithm = nullptr;
        std::ostringstream result;
        result << std::hex << std::setfill('0');
        for (const auto byte : digest) result << std::setw(2) << static_cast<unsigned>(byte);
        return result.str();
    } catch (...) {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        throw;
    }
}
std::uint16_t u16(const unsigned char* bytes) {
    return static_cast<std::uint16_t>(bytes[0] | (static_cast<unsigned>(bytes[1]) << 8));
}
std::uint32_t u32(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}
std::string trim(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r");
    if (begin == std::string_view::npos) return {};
    return std::string(value.substr(begin, value.find_last_not_of(" \t\r") - begin + 1));
}
bool valid_hash(std::string_view hash) {
    return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
bool parse_bool(std::string_view value) {
    if (value == "true") return true;
    if (value == "false") return false;
    throw std::runtime_error("Profile verification flags must be exactly true or false.");
}
}

std::string sha256_file(const std::filesystem::path& path) { return hash_open_file(File(path)); }

ExecutableIdentity inspect_executable(const std::filesystem::path& path) {
    File file(path);
    std::array<unsigned char, 64> dos{};
    file.read(0, dos.data(), static_cast<DWORD>(dos.size()));
    if (dos[0] != 'M' || dos[1] != 'Z') throw std::runtime_error("Selected file is not a DOS/PE executable.");
    const auto pe_offset = u32(dos.data() + 0x3c);
    if (pe_offset < dos.size()) throw std::runtime_error("Invalid PE header offset.");
    std::array<unsigned char, 24> pe{};
    file.read(pe_offset, pe.data(), static_cast<DWORD>(pe.size()));
    if (u32(pe.data()) != 0x00004550) throw std::runtime_error("Invalid PE signature.");
    const auto machine = u16(pe.data() + 4);
    if (machine != IMAGE_FILE_MACHINE_AMD64) throw std::runtime_error("Only native AMD64 executables are supported.");
    if (u16(pe.data() + 6) == 0 || (u16(pe.data() + 22) & IMAGE_FILE_DLL) != 0 ||
        (u16(pe.data() + 22) & IMAGE_FILE_EXECUTABLE_IMAGE) == 0)
        throw std::runtime_error("Selected PE is not an executable application.");
    const auto optional_size = u16(pe.data() + 20);
    if (optional_size < 112) throw std::runtime_error("Missing PE32+ optional header.");
    std::vector<unsigned char> optional(optional_size);
    file.read(static_cast<std::uint64_t>(pe_offset) + pe.size(), optional.data(), optional_size);
    if (u16(optional.data()) != IMAGE_NT_OPTIONAL_HDR64_MAGIC) throw std::runtime_error("PE32+ header required.");
    const auto directory_count = u32(optional.data() + 108);
    if (directory_count > (optional.size() - 112) / 8) throw std::runtime_error("Truncated PE directory table.");
    if (directory_count > IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR &&
        (u32(optional.data() + 112 + IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR * 8) != 0 ||
         u32(optional.data() + 116 + IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR * 8) != 0))
        throw std::runtime_error("Managed PE images are not supported.");
    return {std::filesystem::absolute(path), machine, file.size, hash_open_file(file)};
}

std::vector<GameProfile> parse_profiles(std::string_view text) {
    std::istringstream lines{std::string(text)};
    std::string line, section;
    std::map<std::string, std::map<std::string, std::string>> sections;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            if (section != "meta") {
                constexpr std::string_view prefix = "profile:";
                if (!section.starts_with(prefix) || section.size() == prefix.size() ||
                    !std::all_of(section.begin() + prefix.size(), section.end(), [](char c) {
                        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
                    })) throw std::runtime_error("Invalid profile section.");
            }
            if (!sections.emplace(section, std::map<std::string, std::string>{}).second)
                throw std::runtime_error("Duplicate profile section.");
            continue;
        }
        const auto split = line.find('=');
        if (section.empty() || split == std::string::npos) throw std::runtime_error("Malformed profile configuration.");
        const auto key = trim(std::string_view(line).substr(0, split));
        const auto value = trim(std::string_view(line).substr(split + 1));
        if (!sections.at(section).emplace(key, value).second) throw std::runtime_error("Duplicate profile key.");
    }
    const auto meta = sections.find("meta");
    if (meta == sections.end() || meta->second.size() != 1 || !meta->second.contains("format_version") ||
        meta->second.at("format_version") != "1") throw std::runtime_error("Profile format_version=1 is required.");
    std::vector<GameProfile> profiles;
    std::set<std::string> hashes;
    for (const auto& [name, values] : sections) {
        if (name == "meta") continue;
        if (values.size() != 4 || !values.contains("sha256") || !values.contains("pe_machine") ||
            !values.contains("native_hooks_verified") || !values.contains("saves_verified"))
            throw std::runtime_error("Every profile requires exactly sha256, pe_machine, native_hooks_verified and saves_verified.");
        const auto& hash = values.at("sha256");
        if (!valid_hash(hash) || values.at("pe_machine") != "amd64") throw std::runtime_error("Profile requires an exact lowercase SHA-256 and pe_machine=amd64.");
        if (!hashes.insert(hash).second) throw std::runtime_error("Duplicate profile executable hash.");
        profiles.push_back({name.substr(8), hash, parse_bool(values.at("native_hooks_verified")), parse_bool(values.at("saves_verified"))});
    }
    return profiles;
}

std::vector<GameProfile> read_profiles(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read game_profiles.ini; no executable is approved.");
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || size > 1024 * 1024) throw std::runtime_error("Profile configuration exceeds its 1 MiB limit.");
    input.seekg(0);
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!text.empty() && !input.read(text.data(), static_cast<std::streamsize>(text.size())))
        throw std::runtime_error("Cannot read complete profile configuration.");
    return parse_profiles(text);
}

std::optional<GameProfile> verified_profile(const ExecutableIdentity& identity, const std::vector<GameProfile>& profiles) {
    if (identity.machine != IMAGE_FILE_MACHINE_AMD64) return std::nullopt;
    for (const auto& profile : profiles) {
        if (profile.sha256 == identity.sha256 && profile.native_hooks_verified && profile.saves_verified) return profile;
    }
    return std::nullopt;
}
}
