#include "skcoop/save_policy.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace skcoop::saves;
namespace fs = std::filesystem;
constexpr std::wstring_view account = L"76561198000000001";

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void denied(const SavePathDecision& decision, SavePolicyError error) {
    require(!static_cast<bool>(decision), "rejected decision evaluated true");
    require(decision.error == error, "unexpected rejection reason");
    require(decision.destination.empty() && decision.account_id.empty(),
            "a rejected decision leaked a usable path or account");
}

class Fixture {
public:
    Fixture() : temporary_parent_(fs::temp_directory_path().lexically_normal()) {
        require(temporary_parent_.is_absolute(), "temporary parent must be absolute");
        const auto prefix = L"skcoop-save-policy-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                            std::to_wstring(GetTickCount64()) + L"-";
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = temporary_parent_ / (prefix + std::to_wstring(attempt));
            std::error_code error;
            if (fs::create_directory(candidate, error)) { root_ = candidate; break; }
            if (error) throw std::runtime_error("could not create controlled temporary fixture");
        }
        require(!root_.empty(), "temporary fixture names were exhausted");
    }
    ~Fixture() {
        // Delete only the exact unique directory this fixture successfully created.
        if (!root_.empty() && root_.is_absolute() && root_.parent_path() == temporary_parent_ &&
            root_.filename().wstring().starts_with(L"skcoop-save-policy-")) {
            std::error_code error;
            fs::remove_all(root_, error);
        }
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    SavePolicyConfig config() const {
        SavePolicyConfig result;
        result.appdata_root = (root_ / L"roaming-é-保存").wstring();
        result.coop_profile_root = (root_ / L"coop-ç-共有").wstring();
        result.expected_executable_sha256.fill(0x42);
        result.selected_game_process_id = 42;
        return result;
    }
    SaveProcessContext context() const {
        SaveProcessContext result;
        result.cooperative_profile_opt_in = true;
        result.is_selected_game_process = true;
        result.diagnostic_only = false;
        result.process_id = 42;
        result.observed_executable_sha256.fill(0x42);
        return result;
    }
    const fs::path& root() const { return root_; }
private:
    fs::path temporary_parent_;
    fs::path root_;
};

std::wstring source(const SavePolicyConfig& config,
                    std::wstring_view name = L"S0000.sl2",
                    std::wstring_view identifier = account) {
    return config.appdata_root + L"\\Sekiro\\" + std::wstring(identifier) + L"\\" + std::wstring(name);
}

std::wstring destination(const SavePolicyConfig& config,
                         std::wstring_view name = L"S0000.co2",
                         std::wstring_view identifier = account) {
    auto path = config.coop_profile_root + L"\\Sekiro\\" + std::wstring(identifier) + L"\\" + std::wstring(name);
    if (path[0] >= L'a' && path[0] <= L'z') path[0] = static_cast<wchar_t>(path[0] - L'a' + L'A');
    return path;
}

void default_context_denied() {
    Fixture fixture;
    const auto config = fixture.config();
    denied(decide_save_path(source(config), config, {}), SavePolicyError::not_opted_in);
    auto context = fixture.context(); context.cooperative_profile_opt_in = false;
    denied(decide_save_path(source(config), config, context), SavePolicyError::not_opted_in);
}

void process_context_fails_closed() {
    Fixture fixture;
    auto config = fixture.config();
    auto context = fixture.context(); context.is_selected_game_process = false;
    denied(decide_save_path(source(config), config, context), SavePolicyError::process_not_allowed);
    context = fixture.context(); context.diagnostic_only = true;
    denied(decide_save_path(source(config), config, context), SavePolicyError::process_not_allowed);
    context = fixture.context(); context.process_id = 0;
    denied(decide_save_path(source(config), config, context), SavePolicyError::process_not_allowed);
    context = fixture.context(); context.process_id = 43;
    denied(decide_save_path(source(config), config, context), SavePolicyError::process_not_allowed);
    context = fixture.context(); config.selected_game_process_id = 0;
    denied(decide_save_path(source(config), config, context), SavePolicyError::process_not_allowed);
}

void full_executable_identity_required() {
    Fixture fixture;
    auto config = fixture.config();
    auto context = fixture.context(); context.observed_executable_sha256 = {};
    denied(decide_save_path(source(config), config, context), SavePolicyError::game_identity_mismatch);
    context = fixture.context(); context.observed_executable_sha256[31] ^= 1;
    denied(decide_save_path(source(config), config, context), SavePolicyError::game_identity_mismatch);
    config.expected_executable_sha256 = {}; context.observed_executable_sha256 = {};
    denied(decide_save_path(source(config), config, context), SavePolicyError::game_identity_mismatch);
}

void primary_and_backup_route_to_same_account_directory() {
    Fixture fixture;
    const auto config = fixture.config();
    const auto primary = decide_save_path(source(config), config, fixture.context());
    const auto backup = decide_save_path(source(config, L"S0000.sl2.bak"), config, fixture.context());
    require(primary && backup, "expected primary/backup names were rejected");
    require(primary.kind == SaveFileKind::primary && backup.kind == SaveFileKind::backup, "wrong save kind");
    require(primary.destination == destination(config), "wrong primary destination");
    require(backup.destination == destination(config, L"S0000.co2.bak"), "wrong backup destination");
    require(fs::path(primary.destination).parent_path() == fs::path(backup.destination).parent_path(),
            "primary and backup directories diverged");
    require(primary.account_id == account && backup.account_id == account, "account identity changed");
}

void native_mixed_save_paths_are_accepted() {
    Fixture fixture;
    const auto config = fixture.config();
    const auto native_directory = config.appdata_root + L"/Sekiro/" + std::wstring(account);
    const auto primary = decide_save_path(native_directory + L"/S0000.sl2", config, fixture.context());
    const auto backup = decide_save_path(native_directory + L"\\S0000.sl2.bak", config, fixture.context());
    require(primary && primary.destination == destination(config), "native forward-slash directory rejected");
    require(backup && backup.destination == destination(config, L"S0000.co2.bak"),
            "native mixed slash backup path rejected");
}

void forward_slash_roots_and_single_terminators_are_accepted() {
    Fixture fixture;
    const auto original = fixture.config();
    auto forward_source = source(original);
    std::replace(forward_source.begin(), forward_source.end(), L'\\', L'/');
    for (const auto appdata_ending : {L"", L"\\", L"/"}) {
        for (const auto profile_ending : {L"", L"\\", L"/"}) {
            auto config = original;
            std::replace(config.appdata_root.begin(), config.appdata_root.end(), L'\\', L'/');
            std::replace(config.coop_profile_root.begin(), config.coop_profile_root.end(), L'\\', L'/');
            config.appdata_root += appdata_ending;
            config.coop_profile_root += profile_ending;
            const auto result = decide_save_path(forward_source, config, fixture.context());
            require(result && result.destination == destination(original), "forward roots or a single trailing separator rejected");
            require(result.destination.find(L'/') == std::wstring::npos, "output separators were not canonicalized");
        }
    }
    denied(decide_save_path(forward_source + L"/", original, fixture.context()), SavePolicyError::invalid_source_path);
}

void repeated_separators_are_rejected_including_mixed_pairs() {
    Fixture fixture;
    const auto original = fixture.config();
    for (const auto pair : {L"\\\\", L"//", L"\\/", L"/\\"}) {
        denied(decide_save_path(original.appdata_root + pair + L"Sekiro/" + std::wstring(account) + L"/S0000.sl2",
                                original, fixture.context()), SavePolicyError::invalid_source_path);
        denied(decide_save_path(original.appdata_root + L"/Sekiro/" + std::wstring(account) + pair + L"S0000.sl2",
                                original, fixture.context()), SavePolicyError::invalid_source_path);
        auto config = original; config.coop_profile_root += pair + std::wstring(L"nested");
        denied(decide_save_path(source(original), config, fixture.context()), SavePolicyError::invalid_configuration);
        config = original; config.appdata_root += pair;
        denied(decide_save_path(source(original), config, fixture.context()), SavePolicyError::invalid_configuration);
    }
}

void unicode_and_windows_ordinal_case_are_preserved() {
    Fixture fixture;
    const auto config = fixture.config();
    auto input = source(config, L"s0000.SL2.BAK");
    const auto at = input.find(L"roaming-é-保存");
    require(at != std::wstring::npos, "unicode fixture component missing");
    input.replace(at, std::wstring_view(L"roaming-é-保存").size(), L"ROAMING-É-保存");
    const auto sek = input.find(L"\\Sekiro\\");
    input.replace(sek, std::wstring_view(L"\\Sekiro\\").size(), L"\\sEkIrO\\");
    const auto result = decide_save_path(input, config, fixture.context());
    require(result && result.destination == destination(config, L"S0000.co2.bak"),
            "Windows ordinal comparison or Unicode destination failed");
    auto emoji_config = config;
    emoji_config.coop_profile_root += L"\\profile-\U0001f43a";
    const auto emoji = decide_save_path(source(emoji_config), emoji_config, fixture.context());
    require(emoji && emoji.destination == destination(emoji_config), "valid UTF-16 surrogate pair rejected");
}

void account_paths_remain_distinct() {
    Fixture fixture;
    const auto config = fixture.config();
    const auto first = decide_save_path(source(config), config, fixture.context());
    const auto second = decide_save_path(source(config, L"S0000.sl2", L"76561198000000002"), config, fixture.context());
    require(first && second && first.destination != second.destination, "two accounts shared a destination");
    require(second.account_id == L"76561198000000002", "second account was rewritten");
    require(second.destination == destination(config, L"S0000.co2", L"76561198000000002"), "second account directory lost");
}

void account_format_and_overflow_are_rejected() {
    Fixture fixture;
    const auto config = fixture.config();
    for (const auto identifier : {L"0", L"0001", L"-1", L"+1", L"1e2", L"１２３", L"18446744073709551616"})
        denied(decide_save_path(source(config, L"S0000.sl2", identifier), config, fixture.context()),
               SavePolicyError::invalid_account);
    const auto maximum = decide_save_path(source(config, L"S0000.sl2", L"18446744073709551615"), config, fixture.context());
    require(maximum && maximum.account_id == L"18446744073709551615", "maximum decimal account was truncated");
}

void only_confirmed_input_filenames_are_accepted() {
    Fixture fixture;
    const auto config = fixture.config();
    for (const auto filename : {L"S0001.sl2", L"S0000.co2", L"S0000.sl2.tmp", L"S0000.sl2.bak.bak",
                                L"S0000.bak", L"S0000~1.sl2", L"S0000.sl2backup"})
        denied(decide_save_path(source(config, filename), config, fixture.context()), SavePolicyError::unsupported_filename);
}

void relative_and_device_paths_are_rejected() {
    Fixture fixture;
    const auto config = fixture.config();
    for (const auto path : {L"S0000.sl2", L"C:S0000.sl2", L"\\Sekiro\\1\\S0000.sl2",
                            L"%APPDATA%\\Sekiro\\1\\S0000.sl2", L"\\\\server\\share\\S0000.sl2",
                            L"\\\\?\\C:\\Sekiro\\1\\S0000.sl2", L"\\\\.\\C:\\Sekiro\\1\\S0000.sl2",
                            L"\\??\\C:\\Sekiro\\1\\S0000.sl2", L"//server/share/S0000.sl2",
                            L"//?/C:/Sekiro/1/S0000.sl2", L"//./C:/Sekiro/1/S0000.sl2"})
        denied(decide_save_path(path, config, fixture.context()), SavePolicyError::invalid_source_path);
}

void traversal_separators_and_streams_are_rejected() {
    Fixture fixture;
    const auto config = fixture.config();
    for (const auto suffix : {L"\\Sekiro\\.\\1\\S0000.sl2", L"\\Sekiro\\..\\Sekiro\\1\\S0000.sl2",
                              L"\\Sekiro\\\\1\\S0000.sl2", L"\\Sekiro\\1\\S0000.sl2:stream",
                              L"\\Sekiro\\1\\S0000.sl2.", L"\\Sekiro\\1\\S0000.sl2 ",
                              L"\\Sekiro\\1\\S0000.sl2\\", L"/Sekiro/../Sekiro/1/S0000.sl2",
                              L"/Sekiro/./1/S0000.sl2", L"/Sekiro/1/S0000.sl2:stream"})
        denied(decide_save_path(config.appdata_root + suffix, config, fixture.context()), SavePolicyError::invalid_source_path);
}

void unexpected_namespace_and_nested_paths_are_rejected() {
    Fixture fixture;
    const auto config = fixture.config();
    for (const auto suffix : {L"\\SekiroOther\\1\\S0000.sl2", L"\\Sekiro\\1\\extra\\S0000.sl2",
                              L"\\Sekiro\\S0000.sl2", L"-other\\Sekiro\\1\\S0000.sl2"})
        denied(decide_save_path(config.appdata_root + suffix, config, fixture.context()), SavePolicyError::outside_save_namespace);
}

void control_and_malformed_utf16_are_rejected() {
    Fixture fixture;
    const auto config = fixture.config();
    for (const wchar_t unit : {wchar_t{0}, wchar_t{10}, wchar_t{0xd800}, wchar_t{0xdc00}}) {
        auto input = source(config);
        input.push_back(unit);
        denied(decide_save_path(input, config, fixture.context()), SavePolicyError::invalid_source_path);
    }
}

void extension_rules_are_strict_and_never_vanilla() {
    Fixture fixture;
    auto config = fixture.config();
    for (const auto extension : {L"", L"sl2", L"SL2", L"sL2", L".co2", L"co2.bak", L"co 2", L"co2:stream",
                                 L"../co2", L"co2\\x", L"co_op", L"é"}) {
        config.extension = extension;
        denied(decide_save_path(source(config), config, fixture.context()), SavePolicyError::invalid_extension);
    }
    config.extension.assign(121, L'a');
    denied(decide_save_path(source(config), config, fixture.context()), SavePolicyError::invalid_extension);
    config.extension.assign(120, L'a');
    const auto maximum = decide_save_path(source(config), config, fixture.context());
    require(maximum && fs::path(maximum.destination).filename().wstring() == L"S0000." + config.extension,
            "maximum allowed extension was truncated");
    config.extension = L"CoOp2026";
    require(decide_save_path(source(config), config, fixture.context()).destination == destination(config, L"S0000.CoOp2026"),
            "custom extension changed case or content");
}

void roots_must_be_explicit_canonical_local_paths() {
    Fixture fixture;
    auto config = fixture.config();
    const auto original = config;
    for (const auto path : {L"", L"relative", L"%APPDATA%", L"C:\\", L"C:relative", L"C:/profiles:stream",
                            L"\\\\server\\profiles", L"\\\\?\\C:\\profiles", L"C:\\profiles\\..\\other",
                            L"C:\\profiles\\\\other", L"C:\\profiles.\\other", L"C:\\CON\\other",
                            L"C:\\COM1.txt\\other", L"C:\\LPT\u00b2\\other", L"C:\\CONOUT$\\other"}) {
        config = original; config.coop_profile_root = path;
        denied(decide_save_path(source(config), config, fixture.context()), SavePolicyError::invalid_configuration);
        config = original; config.appdata_root = path;
        denied(decide_save_path(source(config), config, fixture.context()), SavePolicyError::invalid_configuration);
    }
    config = original; config.coop_profile_root += L"\\";
    const auto trailing = decide_save_path(source(config), config, fixture.context());
    require(trailing && trailing.destination == destination(original), "single root separator changed destination");
}

void source_device_components_are_rejected() {
    Fixture fixture;
    const auto config = fixture.config();
    for (const auto component : {L"NUL", L"con.txt", L"COM9", L"LPT\u00b9", L"AUX"})
        denied(decide_save_path(source(config, L"S0000.sl2", component), config, fixture.context()),
               SavePolicyError::invalid_source_path);
}

void separate_save_trees_cannot_overlap() {
    Fixture fixture;
    auto config = fixture.config();
    config.coop_profile_root = config.appdata_root;
    denied(decide_save_path(source(config), config, fixture.context()), SavePolicyError::overlapping_save_trees);
    config = fixture.config(); config.coop_profile_root = config.appdata_root + L"\\Sekiro\\profiles";
    denied(decide_save_path(source(config), config, fixture.context()), SavePolicyError::overlapping_save_trees);
    config = fixture.config(); config.coop_profile_root = (fixture.root() / L"base").wstring();
    config.appdata_root = config.coop_profile_root + L"\\Sekiro\\nested";
    denied(decide_save_path(source(config), config, fixture.context()), SavePolicyError::overlapping_save_trees);
}

void same_account_directory_requires_explicit_mode_and_matching_roots() {
    Fixture fixture;
    auto config = fixture.config();
    config.destination_mode = DestinationMode::same_account_directory;
    denied(decide_save_path(source(config), config, fixture.context()), SavePolicyError::invalid_configuration);
    config.coop_profile_root = config.appdata_root;
    const auto result = decide_save_path(source(config), config, fixture.context());
    require(result && result.destination == destination(config), "explicit extension-only mapping failed");
    require(fs::path(result.destination).parent_path() == fs::path(source(config)).parent_path(),
            "extension-only mapping moved account directory");
    require(result.destination != source(config), "extension-only mapping targeted the solo file");
    config.extension = L"Sl2";
    denied(decide_save_path(source(config), config, fixture.context()), SavePolicyError::invalid_extension);
    config.extension = L"co2"; config.destination_mode = static_cast<DestinationMode>(99);
    denied(decide_save_path(source(config), config, fixture.context()), SavePolicyError::invalid_configuration);
}

void excessive_component_and_destination_lengths_are_rejected() {
    Fixture fixture;
    auto config = fixture.config();
    config.coop_profile_root += L"\\" + std::wstring(256, L'x');
    denied(decide_save_path(source(config), config, fixture.context()), SavePolicyError::invalid_configuration);
    config = fixture.config(); config.coop_profile_root = L"R:\\root";
    while (config.coop_profile_root.size() + 256 < 32760)
        config.coop_profile_root += L"\\" + std::wstring(255, L'x');
    const auto remaining = 32760 - config.coop_profile_root.size();
    require(remaining >= 2 && remaining <= 256, "long path fixture shape is wrong");
    config.coop_profile_root += L"\\" + std::wstring(remaining - 1, L'y');
    denied(decide_save_path(source(config), config, fixture.context()), SavePolicyError::destination_too_long);
}

void write_seed(const fs::path& file, const std::string& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream stream(file, std::ios::binary);
    require(static_cast<bool>(stream), "could not open temporary seed");
    stream << bytes;
    require(static_cast<bool>(stream), "could not write temporary seed");
}

std::string read_seed(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary);
    require(static_cast<bool>(stream), "could not read temporary seed");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::vector<std::wstring> inventory(const fs::path& root) {
    std::vector<std::wstring> result;
    for (const auto& entry : fs::recursive_directory_iterator(root))
        result.push_back(fs::relative(entry.path(), root).wstring());
    std::sort(result.begin(), result.end());
    return result;
}

void decisions_never_import_overwrite_or_create_files() {
    Fixture fixture;
    const auto config = fixture.config();
    const fs::path solo(source(config));
    const fs::path backup(source(config, L"S0000.sl2.bak"));
    write_seed(solo, "solo-fixture-original");
    write_seed(backup, "backup-fixture-original");
    const auto before = inventory(fixture.root());
    const auto primary = decide_save_path(solo.wstring(), config, fixture.context());
    const auto backup_decision = decide_save_path(backup.wstring(), config, fixture.context());
    require(primary && backup_decision, "seeded path mapping failed");
    require(inventory(fixture.root()) == before, "policy created a file or directory");
    require(!fs::exists(fs::path(config.coop_profile_root)), "policy created the destination root");
    require(read_seed(solo) == "solo-fixture-original" && read_seed(backup) == "backup-fixture-original",
            "policy changed a solo fixture");
    write_seed(fs::path(primary.destination), "coop-fixture-existing");
    const auto with_destination = inventory(fixture.root());
    require(static_cast<bool>(decide_save_path(solo.wstring(), config, fixture.context())), "existing target changed lexical decision");
    require(inventory(fixture.root()) == with_destination && read_seed(fs::path(primary.destination)) == "coop-fixture-existing",
            "policy overwrote or imported the existing destination");
    require(read_seed(solo) == "solo-fixture-original", "policy imported or renamed the original");
}

} // namespace

int main() {
    const std::array<std::pair<std::string_view, std::function<void()>>, 22> cases{{
        {"default_context_denied", default_context_denied},
        {"process_context_fails_closed", process_context_fails_closed},
        {"full_executable_identity_required", full_executable_identity_required},
        {"primary_and_backup_route_to_same_account_directory", primary_and_backup_route_to_same_account_directory},
        {"native_mixed_save_paths_are_accepted", native_mixed_save_paths_are_accepted},
        {"forward_slash_roots_and_single_terminators_are_accepted", forward_slash_roots_and_single_terminators_are_accepted},
        {"repeated_separators_are_rejected_including_mixed_pairs", repeated_separators_are_rejected_including_mixed_pairs},
        {"unicode_and_windows_ordinal_case_are_preserved", unicode_and_windows_ordinal_case_are_preserved},
        {"account_paths_remain_distinct", account_paths_remain_distinct},
        {"account_format_and_overflow_are_rejected", account_format_and_overflow_are_rejected},
        {"only_confirmed_input_filenames_are_accepted", only_confirmed_input_filenames_are_accepted},
        {"relative_and_device_paths_are_rejected", relative_and_device_paths_are_rejected},
        {"traversal_separators_and_streams_are_rejected", traversal_separators_and_streams_are_rejected},
        {"unexpected_namespace_and_nested_paths_are_rejected", unexpected_namespace_and_nested_paths_are_rejected},
        {"control_and_malformed_utf16_are_rejected", control_and_malformed_utf16_are_rejected},
        {"extension_rules_are_strict_and_never_vanilla", extension_rules_are_strict_and_never_vanilla},
        {"roots_must_be_explicit_canonical_local_paths", roots_must_be_explicit_canonical_local_paths},
        {"source_device_components_are_rejected", source_device_components_are_rejected},
        {"separate_save_trees_cannot_overlap", separate_save_trees_cannot_overlap},
        {"same_account_directory_requires_explicit_mode_and_matching_roots", same_account_directory_requires_explicit_mode_and_matching_roots},
        {"excessive_component_and_destination_lengths_are_rejected", excessive_component_and_destination_lengths_are_rejected},
        {"decisions_never_import_overwrite_or_create_files", decisions_never_import_overwrite_or_create_files},
    }};
    std::size_t failures{};
    for (const auto& [name, test] : cases) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << "SAVE_POLICY_RESULT " << (failures == 0 ? "PASS" : "FAIL")
              << " cases=" << cases.size() << " failed=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
