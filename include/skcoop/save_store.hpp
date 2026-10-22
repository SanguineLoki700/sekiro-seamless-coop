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
    // Native CopyFileW can fail after modifying an existing destination. A false
    // committed flag alone must not be interpreted as preservation of its bytes.
    bool destination_may_be_modified{};
    [[nodiscard]] explicit operator bool() const noexcept { return error == SaveStoreError::none; }
};
struct SaveStoreConfig {
    // Existing final account directory and existing protected solo tree.
    // Explicit local Windows paths only; this API never discovers AppData.
    std::wstring coop_directory;
    std::wstring protected_directory;
    std::wstring extension{L"co2"};
};

// Conservative Win32 boundary, independent of a game's codec. Values use the
// Windows CreateFile constants. Unsupported options fail before opening a file.
struct NativeSaveOpenRequest {
    std::uint32_t desired_access{};
    std::uint32_t share_mode{};
    std::uint32_t creation_disposition{};
    std::uint32_t flags_and_attributes{0x80}; // FILE_ATTRIBUTE_NORMAL
    bool security_attributes_present{};
    bool template_file_present{};
};
struct NativeSaveOpenResult {
    SaveStoreStatus status;
    // An owned Windows HANDLE; caller closes it with CloseHandle. Never the
    // original solo file. Null on failure, including physical validation failure.
    void* handle{};
    std::uint32_t last_error{};
    bool created{};
    [[nodiscard]] explicit operator bool() const noexcept { return status && handle; }
};
struct NativeSaveFileInfo {
    SaveStoreStatus status;
    std::uint32_t attributes{};
    std::uint64_t size{};
    std::uint64_t creation_time{};
    std::uint64_t access_time{};
    std::uint64_t write_time{};
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(status); }
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

    // Handles preserve native file position, partial reads/writes and flushes.
    // The controller MUST outlive every returned handle; releasing the store
    // releases directory pins/lease and does not extend them into those handles.
    // Native entry points never wait for the controller mutex: contention is a
    // sharing violation. This avoids waiting on a store operation from a loader
    // callback; it does not prove every underlying Win32 call loader-lock safe.
    // Existing files are validated before any requested truncation. Current
    // contract: synchronous 0/GENERIC_READ/GENERIC_WRITE access, no share-delete,
    // normal attributes (backup-semantics only with metadata-only access), null
    // security/template arguments; all five CreateFile dispositions supported.
    [[nodiscard]] NativeSaveOpenResult open_native(SaveFileKind, const NativeSaveOpenRequest&);
    // Checks immutable normalized roots/namespace already physically pinned by
    // this controller, without filesystem discovery or changing its binding.
    [[nodiscard]] bool matches_binding(const SaveStoreConfig&) const noexcept;
    [[nodiscard]] NativeSaveFileInfo query_native(SaveFileKind);
    [[nodiscard]] SaveStoreStatus remove_native(SaveFileKind);
    // Pins source and destination file identities while ordinary CopyFileW
    // preserves file-copy semantics. An API interceptor must bypass its own
    // callbacks only for this trusted internal operation, never an input path.
    // This is not an atomic/rollback-capable copy. See destination_may_be_modified.
    [[nodiscard]] SaveStoreStatus copy_native(SaveFileKind source, SaveFileKind target, bool fail_if_exists);
private:
    struct Impl;
    explicit CoOpSaveStore(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};

} // namespace skcoop::saves
