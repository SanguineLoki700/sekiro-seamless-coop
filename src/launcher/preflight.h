#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace skcoop::launcher {
struct ExecutableIdentity {
    std::filesystem::path path;
    std::uint16_t machine;
    std::uint64_t file_size;
    std::string sha256;
};
struct GameProfile {
    std::string id;
    std::string sha256;
    bool native_hooks_verified = false;
    bool saves_verified = false;
};
ExecutableIdentity inspect_executable(const std::filesystem::path& path);
std::string sha256_file(const std::filesystem::path& path);
std::vector<GameProfile> parse_profiles(std::string_view text);
std::vector<GameProfile> read_profiles(const std::filesystem::path& path);
std::optional<GameProfile> verified_profile(const ExecutableIdentity&, const std::vector<GameProfile>&);
}
