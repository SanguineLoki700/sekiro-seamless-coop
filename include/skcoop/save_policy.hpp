#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace skcoop::saves {

using ExecutableSha256 = std::array<std::uint8_t, 32>;

enum class SaveFileKind { primary, backup };
enum class DestinationMode {
    // <explicit root>\Sekiro\<same account>\S0000.<extension>[.bak]
    separate_profile,
    // Explicitly opt in to the original account directory, changing only the
    // namespace extension. coop_profile_root must equal appdata_root.
    same_account_directory,
};

struct SavePolicyConfig {
    // Absolute, local, drive-qualified Windows paths supplied by the caller.
    // Both slash separators are accepted; output uses backslash separators.
    // No environment expansion, current-directory lookup or filesystem access.
    std::wstring appdata_root;
    std::wstring coop_profile_root;
    std::wstring extension{L"co2"};
    DestinationMode destination_mode{DestinationMode::separate_profile};
    ExecutableSha256 expected_executable_sha256{};
    std::uint32_t selected_game_process_id{};
};

struct SaveProcessContext {
    bool cooperative_profile_opt_in{};
    bool is_selected_game_process{};
    bool diagnostic_only{true};
    std::uint32_t process_id{};
    ExecutableSha256 observed_executable_sha256{};
};

enum class SavePolicyError {
    none,
    not_opted_in,
    process_not_allowed,
    game_identity_mismatch,
    invalid_configuration,
    invalid_extension,
    overlapping_save_trees,
    invalid_source_path,
    outside_save_namespace,
    invalid_account,
    unsupported_filename,
    destination_too_long,
};

struct SavePathDecision {
    SavePolicyError error{SavePolicyError::invalid_configuration};
    SaveFileKind kind{SaveFileKind::primary};
    std::wstring account_id;
    // Empty on every rejection. A caller must never interpret denial as
    // permission to retry a save operation against the original solo path.
    std::wstring destination;
    [[nodiscard]] explicit operator bool() const noexcept {
        return error == SavePolicyError::none;
    }
};

// Pure lexical policy, not a hook or a filesystem access sandbox. Context facts
// must be established by the caller. Reparse points, hard links, existing files
// and TOCTOU require independent handle-based checks at the future I/O boundary.
// This function does not read, write, rename, import or create any file/directory.
[[nodiscard]] SavePathDecision decide_save_path(
    std::wstring_view absolute_source,
    const SavePolicyConfig&,
    const SaveProcessContext&);

} // namespace skcoop::saves
