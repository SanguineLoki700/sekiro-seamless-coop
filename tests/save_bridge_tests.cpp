#include "skcoop/save_bridge.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
using namespace skcoop::saves;
namespace fs = std::filesystem;
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
const std::vector<std::uint8_t> solo{0x91, 0x72, 0x53, 0x34};
const std::vector<std::uint8_t> solo_backup{0x64, 0x45, 0x26};
const std::vector<std::uint8_t> first{1, 2, 3, 4, 5};
const std::vector<std::uint8_t> second{6, 7, 8};
const std::wstring selected_account = L"76561198000000001";
const std::wstring other_account = L"76561198000000002";

void seed(const fs::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "cannot prepare owned seed");
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(output), "cannot write owned seed");
}
std::vector<std::uint8_t> raw_read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot inspect owned seed");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
fs::path temporary_parent() {
    const auto requested = fs::temp_directory_path();
    const auto handle = CreateFileW(requested.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    require(handle != INVALID_HANDLE_VALUE, "cannot inspect temporary parent");
    std::wstring name(32768, L'\0');
    const auto length = GetFinalPathNameByHandleW(handle, name.data(), static_cast<DWORD>(name.size()),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(handle);
    require(length > 0 && length < name.size(), "cannot resolve temporary parent");
    name.resize(length);
    require(name.starts_with(L"\\\\?\\") && name.size() >= 7 && name[5] == L':', "local temporary drive required");
    name.erase(0, 4);
    return fs::path(name).lexically_normal();
}
std::string utf8(std::wstring_view value) {
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    require(count > 0, "cannot encode fixture configuration");
    std::string result(static_cast<std::size_t>(count), '\0');
    require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), count, nullptr, nullptr) == count, "cannot encode fixture configuration");
    return result;
}
struct Fixture {
    fs::path parent = temporary_parent();
    fs::path root;
    Fixture() {
        const auto prefix = L"skcoop-bridge-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        for (unsigned n = 0; n < 100; ++n) {
            auto candidate = parent / (prefix + L"-" + std::to_wstring(n));
            if (fs::create_directory(candidate)) { root = std::move(candidate); break; }
        }
        require(!root.empty(), "cannot create exclusive temporary fixture");
        for (const auto& account : {selected_account, other_account}) {
            fs::create_directories(solo_directory(account));
            fs::create_directories(coop_directory(account));
            seed(solo_directory(account) / L"S0000.sl2", solo);
            seed(solo_directory(account) / L"S0000.sl2.bak", solo_backup);
        }
    }
    ~Fixture() {
        if (!root.empty() && root.is_absolute() && root.parent_path() == parent &&
            root.filename().wstring().starts_with(L"skcoop-bridge-")) {
            std::error_code error;
            fs::remove_all(root, error);
        }
    }
    fs::path appdata() const { return root / L"explicit-roaming-保存"; }
    fs::path coop_root() const { return root / L"explicit-coop-á"; }
    fs::path solo_directory(const std::wstring& account = selected_account) const { return appdata() / L"Sekiro" / account; }
    fs::path coop_directory(const std::wstring& account = selected_account) const { return coop_root() / L"Sekiro" / account; }
    std::wstring source(const std::wstring& account = selected_account) const { return (solo_directory(account) / L"S0000.sl2").wstring(); }
    std::wstring backup_source() const { return source() + L".bak"; }
    SaveSettings settings() const {
        return parse_save_settings("[SAVE]\nsave_file_extension=co2\ndestination_mode=separate_profile\ncoop_profile_root=" + utf8(coop_root().wstring()) + "\n");
    }
    SaveProfileBinding binding(const std::wstring& account = selected_account) const {
        SaveProfileBinding result{appdata().wstring(), source(account)};
        result.expected_executable_sha256.fill(0x37);
        result.selected_game_process_id = 4242;
        return result;
    }
    SaveProcessContext context() const {
        SaveProcessContext result{true, true, false, 4242};
        result.observed_executable_sha256.fill(0x37);
        return result;
    }
    void seeds_intact() const {
        for (const auto& account : {selected_account, other_account}) {
            require(raw_read(solo_directory(account) / L"S0000.sl2") == solo, "solo seed changed");
            require(raw_read(solo_directory(account) / L"S0000.sl2.bak") == solo_backup, "solo backup seed changed");
        }
    }
};

void configuration_policy_storage_cycle_preserves_solo() {
    Fixture fixture;
    auto opened = SaveProfileBridge::open(fixture.settings(), fixture.binding(), fixture.context());
    require(opened && opened.bridge->account_id() == selected_account, "profile not bound to selected account");
    auto written = opened.bridge->write(fixture.source(), fixture.context(), first);
    require(written && written.storage.primary_committed, "first primary write failed");
    written = opened.bridge->write(fixture.source(), fixture.context(), second, BackupMode::create_if_missing);
    require(written && written.storage.primary_committed && written.storage.backup_committed, "backup/update failed");
    auto current = opened.bridge->read(fixture.source(), fixture.context(), second.size());
    auto backup = opened.bridge->read(fixture.backup_source(), fixture.context(), first.size());
    require(current && current.bytes == second && backup && backup.bytes == first, "read routed to wrong bytes");
    auto restored = opened.bridge->restore_backup(fixture.backup_source(), fixture.context(), first.size());
    require(restored && restored.storage.primary_committed, "restore failed");
    current = opened.bridge->read(fixture.source(), fixture.context(), first.size());
    require(current && current.bytes == first, "restore routed to wrong primary");
    fixture.seeds_intact();
}

void opening_does_not_import_or_create_save_files() {
    Fixture fixture;
    auto opened = SaveProfileBridge::open(fixture.settings(), fixture.binding(), fixture.context());
    require(static_cast<bool>(opened), "profile open failed");
    const auto missing = opened.bridge->read(fixture.source(), fixture.context());
    require(!missing && missing.status.error == SaveBridgeError::storage_error &&
        missing.status.storage.error == SaveStoreError::missing_file && missing.bytes.empty(), "solo content imported on open/read");
    require(!fs::exists(fixture.coop_directory() / L"S0000.co2") && !fs::exists(fixture.coop_directory() / L"S0000.co2.bak"),
        "save created implicitly");
    fixture.seeds_intact();
}

void incomplete_or_unsupported_settings_do_not_open_storage() {
    Fixture fixture;
    auto settings = fixture.settings();
    settings.coop_profile_root.clear();
    auto result = SaveProfileBridge::open(settings, fixture.binding(), fixture.context());
    require(!result && result.status.error == SaveBridgeError::incomplete_settings, "empty settings root accepted");
    settings = fixture.settings();
    settings.mode = DestinationMode::same_account_directory;
    settings.coop_profile_root = fixture.appdata().wstring();
    result = SaveProfileBridge::open(settings, fixture.binding(), fixture.context());
    require(!result && result.status.error == SaveBridgeError::unsupported_mode, "same-directory mode enabled");
    settings.mode = static_cast<DestinationMode>(99);
    result = SaveProfileBridge::open(settings, fixture.binding(), fixture.context());
    require(!result && result.status.error == SaveBridgeError::unsupported_mode, "unknown mode accepted");
    require(!fs::exists(fixture.coop_directory() / L".skcoop-store.lock"), "invalid settings opened a store");
    fixture.seeds_intact();
}

void manually_constructed_solo_extension_is_rejected() {
    Fixture fixture;
    auto settings = fixture.settings();
    settings.extension = L"SL2";
    const auto result = SaveProfileBridge::open(settings, fixture.binding(), fixture.context());
    require(!result && result.status.error == SaveBridgeError::policy_rejected &&
        result.status.policy_error == SavePolicyError::invalid_extension, "typed settings bypassed solo-extension rejection");
    require(!fs::exists(fixture.coop_directory() / L".skcoop-store.lock"), "invalid extension opened storage");
    fixture.seeds_intact();
}

void context_rejection_precedes_open_and_each_operation() {
    Fixture fixture;
    const auto settings = fixture.settings();
    const auto binding = fixture.binding();
    std::array<SaveProcessContext, 6> invalid;
    invalid.fill(fixture.context());
    invalid[0].cooperative_profile_opt_in = false;
    invalid[1].is_selected_game_process = false;
    invalid[2].diagnostic_only = true;
    invalid[3].process_id += 1;
    invalid[4].observed_executable_sha256[0] ^= 1;
    invalid[5].observed_executable_sha256.fill(0);
    for (const auto& context : invalid) {
        const auto rejected = SaveProfileBridge::open(settings, binding, context);
        require(!rejected && rejected.status.error == SaveBridgeError::policy_rejected, "invalid context opened a bridge");
    }
    require(!fs::exists(fixture.coop_directory() / L".skcoop-store.lock"), "invalid context accessed storage");
    auto opened = SaveProfileBridge::open(settings, binding, fixture.context());
    require(opened && opened.bridge->write(fixture.source(), fixture.context(), first) &&
        opened.bridge->write(fixture.source(), fixture.context(), second, BackupMode::create_if_missing), "seed failed");
    for (const auto& context : invalid) {
        auto read = opened.bridge->read(fixture.source(), context);
        auto write = opened.bridge->write(fixture.source(), context, first, BackupMode::replace_existing);
        auto restore = opened.bridge->restore_backup(fixture.backup_source(), context);
        require(!read && read.bytes.empty() && read.status.error == SaveBridgeError::policy_rejected &&
            !write && write.error == SaveBridgeError::policy_rejected && !restore && restore.error == SaveBridgeError::policy_rejected,
            "operation bypassed context revalidation");
    }
    require(raw_read(fixture.coop_directory() / L"S0000.co2") == second &&
        raw_read(fixture.coop_directory() / L"S0000.co2.bak") == first, "rejected operation modified storage");
    fixture.seeds_intact();
}

void account_change_cannot_retarget_open_profile() {
    Fixture fixture;
    auto opened = SaveProfileBridge::open(fixture.settings(), fixture.binding(), fixture.context());
    require(opened && opened.bridge->write(fixture.source(), fixture.context(), first), "seed failed");
    seed(fixture.coop_directory(other_account) / L"S0000.co2", second);
    const auto read = opened.bridge->read(fixture.source(other_account), fixture.context());
    const auto write = opened.bridge->write(fixture.source(other_account), fixture.context(), first);
    const auto restore = opened.bridge->restore_backup(fixture.source(other_account) + L".bak", fixture.context());
    require(!read && read.bytes.empty() && read.status.error == SaveBridgeError::account_mismatch &&
        !write && write.error == SaveBridgeError::account_mismatch && !restore && restore.error == SaveBridgeError::account_mismatch,
        "opened profile switched accounts");
    require(raw_read(fixture.coop_directory() / L"S0000.co2") == first &&
        raw_read(fixture.coop_directory(other_account) / L"S0000.co2") == second, "account mismatch changed data");
    auto independent = SaveProfileBridge::open(fixture.settings(), fixture.binding(other_account), fixture.context());
    require(independent && independent.bridge->account_id() == other_account, "independent account profile could not open");
    const auto own_read = independent.bridge->read(fixture.source(other_account), fixture.context());
    require(own_read && own_read.bytes == second, "independent account shares the first store");
    fixture.seeds_intact();
}

void source_file_kind_and_namespace_are_enforced() {
    Fixture fixture;
    auto binding = fixture.binding();
    binding.source_primary = fixture.backup_source();
    auto rejected = SaveProfileBridge::open(fixture.settings(), binding, fixture.context());
    require(!rejected && rejected.status.error == SaveBridgeError::unexpected_file_kind, "backup used as profile primary");
    auto opened = SaveProfileBridge::open(fixture.settings(), fixture.binding(), fixture.context());
    require(opened && opened.bridge->write(fixture.source(), fixture.context(), first), "seed failed");
    const auto wrong_write = opened.bridge->write(fixture.backup_source(), fixture.context(), second);
    const auto wrong_restore = opened.bridge->restore_backup(fixture.source(), fixture.context());
    require(!wrong_write && wrong_write.error == SaveBridgeError::unexpected_file_kind &&
        !wrong_restore && wrong_restore.error == SaveBridgeError::unexpected_file_kind, "wrong operation kind accepted");
    for (const auto& source : {fixture.source() + L":stream", (fixture.root / L"S0000.sl2").wstring(),
                              (fixture.solo_directory() / L"S0000.co2").wstring()}) {
        const auto denied = opened.bridge->read(source, fixture.context());
        require(!denied && denied.bytes.empty() && denied.status.error == SaveBridgeError::policy_rejected, "wrong source namespace accepted");
    }
    require(raw_read(fixture.coop_directory() / L"S0000.co2") == first, "wrong-kind write changed primary");
    fixture.seeds_intact();
}

void missing_profile_directory_is_not_created() {
    Fixture fixture;
    auto settings = fixture.settings();
    const auto absent = fixture.root / L"not-prepared";
    settings.coop_profile_root = absent.wstring();
    const auto result = SaveProfileBridge::open(settings, fixture.binding(), fixture.context());
    require(!result && result.status.error == SaveBridgeError::storage_error && !fs::exists(absent), "missing profile implicitly created");
    fixture.seeds_intact();
}

void physical_link_rejection_is_preserved_through_bridge() {
    Fixture fixture;
    require(CreateHardLinkW((fixture.coop_directory() / L"S0000.co2").c_str(),
        (fixture.solo_directory() / L"S0000.sl2").c_str(), nullptr) != FALSE, "cannot prepare controlled hardlink");
    const auto result = SaveProfileBridge::open(fixture.settings(), fixture.binding(), fixture.context());
    require(!result && result.status.error == SaveBridgeError::storage_error &&
        result.status.storage.error == SaveStoreError::hard_link, "physical link failure lost in bridge");
    fixture.seeds_intact();
}

void storage_errors_and_commit_flags_are_preserved() {
    Fixture fixture;
    auto opened = SaveProfileBridge::open(fixture.settings(), fixture.binding(), fixture.context());
    require(opened && opened.bridge->write(fixture.source(), fixture.context(), first) &&
        opened.bridge->write(fixture.source(), fixture.context(), second, BackupMode::create_if_missing), "seed failed");
    auto denied = opened.bridge->write(fixture.source(), fixture.context(), first, BackupMode::create_if_missing);
    require(!denied && denied.error == SaveBridgeError::storage_error && denied.storage.error == SaveStoreError::backup_exists &&
        !denied.storage.primary_committed && !denied.storage.backup_committed, "backup refusal hidden");
    denied = opened.bridge->restore_backup(fixture.backup_source(), fixture.context(), first.size() + 1);
    require(!denied && denied.error == SaveBridgeError::storage_error && denied.storage.error == SaveStoreError::size_mismatch,
        "restore length failure hidden");
    require(raw_read(fixture.coop_directory() / L"S0000.co2") == second &&
        raw_read(fixture.coop_directory() / L"S0000.co2.bak") == first, "storage refusal changed files");
    fixture.seeds_intact();
}

void configured_extension_and_mixed_separators_are_used() {
    Fixture fixture;
    auto settings = fixture.settings();
    settings.extension = L"profile42";
    auto binding = fixture.binding();
    std::replace(binding.source_primary.begin(), binding.source_primary.end(), L'\\', L'/');
    binding.appdata_root += L"/";
    settings.coop_profile_root += L"\\";
    auto opened = SaveProfileBridge::open(settings, binding, fixture.context());
    require(opened && opened.bridge->write(fixture.source(), fixture.context(), first), "configured namespace/mixed separators failed");
    require(raw_read(fixture.coop_directory() / L"S0000.profile42") == first &&
        !fs::exists(fixture.coop_directory() / L"S0000.co2"), "extension setting not bound to store");
    fixture.seeds_intact();
}

void reopened_bridge_recovers_empty_primary_from_backup() {
    Fixture fixture;
    seed(fixture.coop_directory() / L"S0000.co2", {});
    seed(fixture.coop_directory() / L"S0000.co2.bak", first);
    auto opened = SaveProfileBridge::open(fixture.settings(), fixture.binding(), fixture.context());
    require(static_cast<bool>(opened), "empty primary blocked profile opening");
    const auto restored = opened.bridge->restore_backup(fixture.backup_source(), fixture.context(), first.size());
    require(restored && restored.storage.primary_committed &&
        raw_read(fixture.coop_directory() / L"S0000.co2") == first, "bridge recovery failed");
    fixture.seeds_intact();
}
} // namespace

int main() {
    const std::array<std::pair<std::string_view, std::function<void()>>, 12> cases{{
        {"configuration_policy_storage_cycle_preserves_solo", configuration_policy_storage_cycle_preserves_solo},
        {"opening_does_not_import_or_create_save_files", opening_does_not_import_or_create_save_files},
        {"incomplete_or_unsupported_settings_do_not_open_storage", incomplete_or_unsupported_settings_do_not_open_storage},
        {"manually_constructed_solo_extension_is_rejected", manually_constructed_solo_extension_is_rejected},
        {"context_rejection_precedes_open_and_each_operation", context_rejection_precedes_open_and_each_operation},
        {"account_change_cannot_retarget_open_profile", account_change_cannot_retarget_open_profile},
        {"source_file_kind_and_namespace_are_enforced", source_file_kind_and_namespace_are_enforced},
        {"missing_profile_directory_is_not_created", missing_profile_directory_is_not_created},
        {"physical_link_rejection_is_preserved_through_bridge", physical_link_rejection_is_preserved_through_bridge},
        {"storage_errors_and_commit_flags_are_preserved", storage_errors_and_commit_flags_are_preserved},
        {"configured_extension_and_mixed_separators_are_used", configured_extension_and_mixed_separators_are_used},
        {"reopened_bridge_recovers_empty_primary_from_backup", reopened_bridge_recovers_empty_primary_from_backup},
    }};
    std::size_t failed{};
    for (const auto& [name, test] : cases) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    std::cout << "SAVE_BRIDGE_RESULT " << (failed ? "FAIL" : "PASS") << " cases=" << cases.size()
              << " failed=" << failed << '\n';
    return failed ? 1 : 0;
}
