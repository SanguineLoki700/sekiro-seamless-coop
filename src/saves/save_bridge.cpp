#include "skcoop/save_bridge.hpp"

#include <algorithm>
#include <utility>

namespace skcoop::saves {
namespace {
SaveBridgeStatus failure(SaveBridgeError error) { return {error, SavePolicyError::none, {}}; }
SaveBridgeStatus policy_failure(SavePolicyError error) {
    return {SaveBridgeError::policy_rejected, error, {}};
}
SaveBridgeStatus storage_result(SaveStoreStatus status) {
    return {status ? SaveBridgeError::none : SaveBridgeError::storage_error, SavePolicyError::none, status};
}
std::wstring protected_tree(std::wstring root) {
    // Called only after the path policy has accepted both roots and the source.
    std::replace(root.begin(), root.end(), L'/', L'\\');
    if (root.back() == L'\\') root.pop_back();
    return root + L"\\Sekiro";
}
} // namespace

SaveProfileBridge::SaveProfileBridge(SavePolicyConfig policy, std::wstring account,
                                   std::unique_ptr<CoOpSaveStore> store)
    : policy_(std::move(policy)), account_id_(std::move(account)), store_(std::move(store)) {}
SaveProfileBridge::~SaveProfileBridge() = default;

SaveBridgeOpenResult SaveProfileBridge::open(const SaveSettings& settings,
        const SaveProfileBinding& binding, const SaveProcessContext& context) {
    if (settings.coop_profile_root.empty()) return {failure(SaveBridgeError::incomplete_settings), {}};
    if (settings.mode != DestinationMode::separate_profile)
        return {failure(SaveBridgeError::unsupported_mode), {}};
    SavePolicyConfig policy{binding.appdata_root, settings.coop_profile_root, settings.extension,
        settings.mode, binding.expected_executable_sha256, binding.selected_game_process_id};
    auto decision = decide_save_path(binding.source_primary, policy, context);
    if (!decision) return {policy_failure(decision.error), {}};
    if (decision.kind != SaveFileKind::primary)
        return {failure(SaveBridgeError::unexpected_file_kind), {}};
    const auto separator = decision.destination.find_last_of(L'\\');
    // A successful path policy decision always contains an absolute file path.
    if (separator == std::wstring::npos) return {policy_failure(SavePolicyError::invalid_configuration), {}};
    auto opened = CoOpSaveStore::open({decision.destination.substr(0, separator),
                                     protected_tree(policy.appdata_root), policy.extension});
    if (!opened) return {storage_result(opened.status), {}};
    return {{}, std::unique_ptr<SaveProfileBridge>(new SaveProfileBridge(
        std::move(policy), std::move(decision.account_id), std::move(opened.store)))};
}

SaveBridgeStatus SaveProfileBridge::authorize(std::wstring_view source,
        const SaveProcessContext& context, SavePathDecision& decision) const {
    decision = decide_save_path(source, policy_, context);
    if (!decision) return policy_failure(decision.error);
    if (decision.account_id != account_id_) return failure(SaveBridgeError::account_mismatch);
    return {};
}

SaveBridgeReadResult SaveProfileBridge::read(std::wstring_view source,
        const SaveProcessContext& context, std::optional<std::size_t> expected_size) {
    SavePathDecision decision;
    const auto authorized = authorize(source, context, decision);
    if (!authorized) return {authorized, {}};
    auto result = store_->read(decision.kind, expected_size);
    return {storage_result(result.status), std::move(result.bytes)};
}

SaveBridgeStatus SaveProfileBridge::write(std::wstring_view source,
        const SaveProcessContext& context, std::span<const std::uint8_t> bytes, BackupMode mode) {
    SavePathDecision decision;
    const auto authorized = authorize(source, context, decision);
    if (!authorized) return authorized;
    if (decision.kind != SaveFileKind::primary) return failure(SaveBridgeError::unexpected_file_kind);
    return storage_result(store_->write(bytes, mode));
}

SaveBridgeStatus SaveProfileBridge::restore_backup(std::wstring_view source,
        const SaveProcessContext& context, std::optional<std::size_t> expected_size) {
    SavePathDecision decision;
    const auto authorized = authorize(source, context, decision);
    if (!authorized) return authorized;
    if (decision.kind != SaveFileKind::backup) return failure(SaveBridgeError::unexpected_file_kind);
    return storage_result(store_->restore_backup(expected_size));
}

} // namespace skcoop::saves
