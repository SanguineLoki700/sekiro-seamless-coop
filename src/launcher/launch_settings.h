#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace skcoop::launcher {

inline constexpr std::size_t max_launch_settings_bytes = 64 * 1024;
inline constexpr std::size_t max_launch_path_units = 1024;

struct LaunchSettings {
    std::filesystem::path test_executable;
    std::filesystem::path coop_profile_root;
    std::filesystem::path receipt_directory;
    std::wstring account;
    bool actor_follow_test{};
    bool actor_locomotion_test{};
};

// Strict UTF-8 INI subset: exactly one [LAUNCH] section and the four required,
// case-sensitive keys test_executable, coop_profile_root, account and
// receipt_directory. Optional actor_test=off|follow|locomotion defaults to off.
// One initial BOM, whole-line ;/# comments and surrounding
// ASCII whitespace are accepted. Values are neither quoted nor expanded.
// Paths must be explicit local drive paths with fewer than 1024 UTF-16 units
// and no trailing separator;
// aliases, reparse points and physical separation are checked later by the
// protected launch host. Parsing does not open a game or access save files.
// Invalid contents or an input exceeding 64 KiB throw std::invalid_argument.
[[nodiscard]] LaunchSettings parse_launch_settings(std::string_view utf8);

// Reads only the selected configuration, at most 64 KiB plus one byte for
// overflow detection. I/O failures throw std::runtime_error.
[[nodiscard]] LaunchSettings read_launch_settings(const std::filesystem::path& path);

} // namespace skcoop::launcher
