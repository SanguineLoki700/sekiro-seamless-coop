#pragma once

#include "skcoop/save_policy.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace skcoop::saves {

inline constexpr std::size_t max_store_save_bytes = 64u * 1024u * 1024u;
enum class SaveStoreError {
    none, invalid_configuration, invalid_extension, unsupported_filesystem,
    reparse_point, alias_path, overlapping_roots, hard_link, busy,
    missing_file, invalid_size, size_mismatch, backup_exists, io_error, commit_failed,
};
struct SaveStoreStatus {
    SaveStoreError error{SaveStoreError::none};
    std::uint32_t system_error{};
    bool backup_committed{};
    bool primary_committed{};
    std::uint32_t cleanup_error{};
    [[nodiscard]] explicit operator bool() const noexcept { return error == SaveStoreError::none; }
};
struct SaveStoreConfig {
    // Existing final account directory and existing protected solo tree.
    // Explicit local Windows paths only; this API never discovers AppData.
    std::wstring coop_directory;
    std::wstring protected_directory;
    std::wstring extension{L"co2"};
};
enum class BackupMode { none, create_if_missing, replace_existing };
struct SaveStoreReadResult {
    SaveStoreStatus status;
    std::vector<std::uint8_t> bytes;
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(status); }
};
class CoOpSaveStore;
struct SaveStoreOpenResult {
    SaveStoreStatus status;
    std::unique_ptr<CoOpSaveStore> store;
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(status) && store != nullptr; }
};

// Directory-bound, NTFS-only opaque byte storage. This is not a Sekiro save
// adapter or hook. The caller must supply an already isolated test/profile tree.
class CoOpSaveStore {
public:
    [[nodiscard]] static SaveStoreOpenResult open(const SaveStoreConfig&);
    ~CoOpSaveStore();
    CoOpSaveStore(const CoOpSaveStore&) = delete;
    CoOpSaveStore& operator=(const CoOpSaveStore&) = delete;

    // Only S0000.<extension> and S0000.<extension>.bak are exposed. expected_size
    // lets a caller with a verified format reject a truncated opaque save.
    [[nodiscard]] SaveStoreReadResult read(SaveFileKind, std::optional<std::size_t> expected_size = {});
    [[nodiscard]] SaveStoreStatus write(std::span<const std::uint8_t>, BackupMode = BackupMode::none);
    // Explicitly replaces the primary with validated backup bytes; leaves backup intact.
    [[nodiscard]] SaveStoreStatus restore_backup(std::optional<std::size_t> expected_size = {});
private:
    struct Impl;
    explicit CoOpSaveStore(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};

} // namespace skcoop::saves
