#pragma once

#include "skcoop/save_settings.hpp"
#include "skcoop/save_store.hpp"

namespace skcoop::saves {

// Supplied by the caller, never discovered or authenticated by this component.
struct SaveProfileBinding {
    std::wstring appdata_root;
    std::wstring source_primary;
    ExecutableSha256 expected_executable_sha256{};
    std::uint32_t selected_game_process_id{};
};

enum class SaveBridgeError {
    none, incomplete_settings, unsupported_mode, policy_rejected,
    account_mismatch, unexpected_file_kind, storage_error,
};
struct SaveBridgeStatus {
    SaveBridgeError error{SaveBridgeError::none};
    SavePolicyError policy_error{SavePolicyError::none};
    SaveStoreStatus storage;
    [[nodiscard]] explicit operator bool() const noexcept { return error == SaveBridgeError::none; }
};
struct SaveBridgeReadResult {
    SaveBridgeStatus status;
    std::vector<std::uint8_t> bytes;
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(status); }
};
class SaveProfileBridge;
struct SaveBridgeOpenResult {
    SaveBridgeStatus status;
    std::unique_ptr<SaveProfileBridge> bridge;
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(status) && bridge != nullptr; }
};

// Binds one declared process/profile/account to the guarded opaque store.
// This is a high-level integration component, not an API/file hook or a native
// save-format adapter. It never opens the source path or imports a solo save.
class SaveProfileBridge {
public:
    [[nodiscard]] static SaveBridgeOpenResult open(
        const SaveSettings&, const SaveProfileBinding&, const SaveProcessContext&);
    ~SaveProfileBridge();
    SaveProfileBridge(const SaveProfileBridge&) = delete;
    SaveProfileBridge& operator=(const SaveProfileBridge&) = delete;

    [[nodiscard]] const std::wstring& account_id() const noexcept { return account_id_; }
    // Every operation revalidates the caller-supplied context and source path.
    // write requires the source primary; restore requires the source backup.
    [[nodiscard]] SaveBridgeReadResult read(std::wstring_view source,
        const SaveProcessContext&, std::optional<std::size_t> expected_size = {});
    [[nodiscard]] SaveBridgeStatus write(std::wstring_view source,
        const SaveProcessContext&, std::span<const std::uint8_t>, BackupMode = BackupMode::none);
    [[nodiscard]] SaveBridgeStatus restore_backup(std::wstring_view source_backup,
        const SaveProcessContext&, std::optional<std::size_t> expected_size = {});
private:
    SaveProfileBridge(SavePolicyConfig, std::wstring, std::unique_ptr<CoOpSaveStore>);
    [[nodiscard]] SaveBridgeStatus authorize(std::wstring_view,
        const SaveProcessContext&, SavePathDecision&) const;
    SavePolicyConfig policy_;
    std::wstring account_id_;
    std::unique_ptr<CoOpSaveStore> store_;
};

} // namespace skcoop::saves
