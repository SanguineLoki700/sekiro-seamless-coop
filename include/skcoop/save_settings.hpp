#pragma once

#include "skcoop/save_policy.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace skcoop::saves {

inline constexpr std::size_t max_save_settings_bytes = 64 * 1024;

struct SaveSettings {
    std::wstring extension;
    DestinationMode mode{DestinationMode::separate_profile};
    // Empty is a valid incomplete configuration, never native readiness.
    std::wstring coop_profile_root;
};

// Strict UTF-8 INI subset: one [SAVE] section and exactly the three required keys
// save_file_extension, destination_mode and coop_profile_root. Keys/section are
// case-sensitive. Leading/trailing ASCII whitespace and whole-line ;/# comments
// are accepted, as is one initial UTF-8 BOM. Values are not quoted or expanded.
// Nonempty roots must follow the lexical Windows path rules of the save policy.
// Invalid syntax/values/UTF-8 or inputs over 64 KiB throw std::invalid_argument.
[[nodiscard]] SaveSettings parse_save_settings(std::string_view utf8);

// Reads only this configuration, at most 64 KiB + one overflow-detection byte.
// I/O errors throw std::runtime_error; invalid contents throw std::invalid_argument.
// Neither function writes files, inspects saves, authenticates process facts or
// enables gameplay/save-isolation capability flags. The caller still supplies
// AppData/process identity and applies the independent path/I/O policies.
[[nodiscard]] SaveSettings read_save_settings(const std::filesystem::path& config_path);

} // namespace skcoop::saves
