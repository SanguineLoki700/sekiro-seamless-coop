#include "skcoop/save_native_policy.hpp"

#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace skcoop::saves;
constexpr std::wstring_view account = L"76561198000000001";
constexpr std::wstring_view other_account = L"76561198000000002";

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

SavePolicyConfig config() {
    SavePolicyConfig result;
    result.appdata_root = L"C:\\synthetic\\roaming-é-保存";
    result.coop_profile_root = L"D:\\synthetic\\coop-ç-共有";
    result.expected_executable_sha256.fill(0x42);
    result.selected_game_process_id = 42;
    return result;
}

SaveProcessContext context() {
    SaveProcessContext result;
    result.cooperative_profile_opt_in = true;
    result.is_selected_game_process = true;
    result.diagnostic_only = false;
    result.process_id = 42;
    result.observed_executable_sha256.fill(0x42);
    return result;
}

std::wstring source(const SavePolicyConfig& settings, std::wstring_view name = L"S0000.sl2",
    std::wstring_view identifier = account) {
    return settings.appdata_root + L"\\Sekiro\\" + std::wstring(identifier) + L"\\" + std::wstring(name);
}

std::wstring target(const SavePolicyConfig& settings, std::wstring_view name = L"S0000.co2",
    std::wstring_view identifier = account) {
    return settings.coop_profile_root + L"\\Sekiro\\" + std::wstring(identifier) + L"\\" + std::wstring(name);
}

NativePathDecision decide(std::wstring_view path, const SavePolicyConfig& settings = config()) {
    return decide_native_save_path(path, settings, context(), account);
}

void denied(const NativePathDecision& result) {
    require(result.action == NativePathAction::deny && result.error != NativePathError::none,
        "save candidate did not fail closed");
    require(result.destination.empty() && result.account_id.empty(), "denial exposed usable save path/account");
}

void denied(const NativePathPairDecision& result) {
    require(result.action == NativePathAction::deny && result.error != NativePathError::none, "pair did not fail closed");
    denied(result.source);
    denied(result.target);
}

void passthrough(const NativePathDecision& result) {
    require(result.action == NativePathAction::passthrough && result.error == NativePathError::none,
        "unrelated path was claimed as cooperative");
    require(result.destination.empty() && result.account_id.empty(), "passthrough exposed a cooperative path/account");
}

void redirect(const NativePathDecision& result, std::wstring_view expected, SaveFileKind kind) {
    require(result.action == NativePathAction::redirect && result.error == NativePathError::none &&
        result.policy_error == SavePolicyError::none, "valid save did not redirect");
    require(result.destination == expected && result.account_id == account && result.kind == kind,
        "redirect changed account, kind or bound destination");
}

void primary_and_backup_redirect() {
    const auto settings = config();
    redirect(decide(source(settings)), target(settings), SaveFileKind::primary);
    redirect(decide(source(settings, L"S0000.sl2.bak")), target(settings, L"S0000.co2.bak"), SaveFileKind::backup);
}

void already_redirected_inputs_are_idempotent() {
    const auto settings = config();
    redirect(decide(target(settings)), target(settings), SaveFileKind::primary);
    redirect(decide(target(settings, L"S0000.co2.bak")), target(settings, L"S0000.co2.bak"), SaveFileKind::backup);
    const auto first = decide(source(settings));
    redirect(decide(first.destination), first.destination, SaveFileKind::primary);
}

void case_mixed_separators_and_unicode() {
    const auto settings = config();
    auto path = source(settings, L"s0000.SL2.BaK");
    for (std::size_t i = 0; i < path.size(); ++i) if (path[i] == L'\\' && i % 2 == 0) path[i] = L'/';
    path[0] = L'c';
    redirect(decide(path), target(settings, L"S0000.co2.bak"), SaveFileKind::backup);
    path = target(settings, L"s0000.Co2.BAK");
    for (auto& value : path) if (value == L'\\') value = L'/';
    redirect(decide(path), target(settings, L"S0000.co2.bak"), SaveFileKind::backup);
}

void account_is_fixed_for_both_namespaces() {
    const auto settings = config();
    for (const auto& path : {source(settings, L"S0000.sl2", other_account),
        source(settings, L"S0000.sl2.bak", other_account), target(settings, L"S0000.co2", other_account),
        target(settings, L"S0000.co2.bak", other_account)}) {
        const auto result = decide(path);
        denied(result);
        require(result.error == NativePathError::account_mismatch, "account mismatch reason lost");
    }
}

void absolute_unrelated_files_remain_unrelated() {
    const auto settings = config();
    for (const auto& path : {std::wstring(L"C:\\another-game\\OtherSave.sl2"),
        std::wstring(L"D:\\another-game\\OtherSave.co2.bak"), std::wstring(L"C:\\assets\\*.sl2"),
        std::wstring(L"C:\\synthetic\\roaming-é-保存\\SekiroOther\\OtherSave.sl2"),
        source(settings, L"settings.ini"), target(settings, L"log.txt")}) passthrough(decide(path));
    passthrough(decide(L"textures\\character.bin"));
}

void relative_and_device_save_candidates_are_denied() {
    for (const auto path : {L"S0000.sl2", L"S0000.co2.bak", L".\\S0000.sl2", L"..\\S0000.sl2",
        L"C:S0000.sl2", L"\\S0000.sl2", L".\\Sekiro\\76561198000000001\\S0000.sl2",
        L"*.sl2", L"\\\\server\\share\\S0000.sl2", L"\\\\.\\C:\\other\\S0000.sl2"}) denied(decide(path));
}

void known_save_extensions_never_passthrough_inside_trees() {
    const auto settings = config();
    for (const auto name : {L"S0001.sl2", L"other.sl2", L"other.co2.bak", L"S0000.unknown",
        L"S0000.sl2.tmp", L"S0000.sl2:stream", L"S0000.sl2.", L"S0000.sl2 ", L"*.sl2", L"*"}) {
        denied(decide(source(settings, name)));
        denied(decide(target(settings, name)));
    }
    denied(decide(source(settings, L"S0000.co2")));
}

void private_appdata_solo_names_redirect_to_cooperative_extension() {
    const auto settings = config();
    redirect(decide(target(settings, L"S0000.sl2")), target(settings), SaveFileKind::primary);
    redirect(decide(target(settings, L"S0000.sl2.bak")), target(settings, L"S0000.co2.bak"), SaveFileKind::backup);
    const auto result = decide_native_save_pair(target(settings, L"S0000.sl2"), target(settings, L"S0000.sl2.bak"),
        settings, context(), account);
    require(result.action == NativePathAction::redirect, "private AppData backup pair rejected");
    redirect(result.source, target(settings), SaveFileKind::primary);
    redirect(result.target, target(settings, L"S0000.co2.bak"), SaveFileKind::backup);
    denied(decide(target(settings, L"S0000.sl2", other_account)));
}

void absolutized_fallback_cannot_escape_the_bound_namespace() {
    for (const auto path : {L"C:\\Game\\Sekiro\\S0000.sl2", L"C:\\Game\\Sekiro\\76561198000000001\\S0000.sl2",
        L"C:\\Game\\S0000.sl2.bak", L"C:\\Game\\s0000.CO2", L"D:\\elsewhere\\S0000.co2.bak",
        L"C:\\Game\\S0000.sl2.", L"C:\\Game\\S0000.sl2 ", L"C:\\Game\\S0000.sl2:stream"}) denied(decide(path));
    passthrough(decide(L"C:\\another-game\\OtherSave.sl2"));
}

void ambiguous_paths_are_not_normalized_into_permission() {
    const auto settings = config();
    const auto tree = settings.appdata_root + L"\\Sekiro\\";
    for (const auto& path : {tree + L"\\" + std::wstring(account) + L"\\S0000.sl2",
        tree + L".\\" + std::wstring(account) + L"\\S0000.sl2",
        tree + std::wstring(account) + L"\\..\\" + std::wstring(account) + L"\\S0000.sl2",
        tree + std::wstring(account) + L"\\..\\..\\other\\S0000.sl2",
        settings.appdata_root + L"\\other\\..\\Sekiro\\" + std::wstring(account) + L"\\S0000.sl2",
        settings.appdata_root + L"\\Sekiro.\\" + std::wstring(account) + L"\\S0000.sl2",
        std::wstring(L"\\\\?\\") + source(settings), std::wstring(L"\\??\\") + source(settings),
        std::wstring(L"C:\\SYNTH~1\\ROAMIN~1\\Sekiro\\") + std::wstring(account) + L"\\S0000.sl2"}) denied(decide(path));
}

void malformed_text_does_not_authorize_a_save() {
    const auto settings = config();
    auto path = source(settings);
    path += wchar_t{};
    path += L"ignored";
    denied(decide(path));
    path = source(settings, L"S0000.sl2");
    path.insert(path.find_last_of(L'\\') + 1, 1, static_cast<wchar_t>(0xd800));
    denied(decide(path));
    denied(decide(source(settings, L"S0000.sl2\\child")));
}

void declared_context_is_revalidated() {
    const auto settings = config();
    std::array<SaveProcessContext, 6> contexts;
    contexts.fill(context());
    contexts[0].cooperative_profile_opt_in = false;
    contexts[1].is_selected_game_process = false;
    contexts[2].diagnostic_only = true;
    contexts[3].process_id = 43;
    contexts[4].observed_executable_sha256 = {};
    contexts[5].observed_executable_sha256[31] ^= 1;
    for (const auto& facts : contexts) {
        const auto result = decide_native_save_path(source(settings), settings, facts, account);
        denied(result);
        require(result.error == NativePathError::invalid_binding, "invalid declared context accepted");
        denied(decide_native_save_path(target(settings), settings, facts, account));
    }
}

void invalid_binding_and_configuration_fail_closed() {
    const auto settings = config();
    for (const auto identifier : {L"", L"0", L"076561198000000001", L"other", L"18446744073709551616",
        L"76561198000000001\\..", L"76561198000000001\\S0000.sl2"})
        denied(decide_native_save_path(source(settings), settings, context(), identifier));
    std::array<SavePolicyConfig, 5> invalid;
    invalid.fill(settings);
    invalid[0].extension = L"SL2";
    invalid[1].coop_profile_root.clear();
    invalid[2].coop_profile_root = settings.appdata_root;
    invalid[3].expected_executable_sha256 = {};
    invalid[4].destination_mode = static_cast<DestinationMode>(99);
    for (const auto& item : invalid) denied(decide_native_save_path(source(settings), item, context(), account));
}

void configured_extension_is_preserved() {
    auto settings = config();
    settings.extension = L"Profile42";
    redirect(decide(source(settings), settings), target(settings, L"S0000.Profile42"), SaveFileKind::primary);
    redirect(decide(target(settings, L"s0000.pROFILE42.bak"), settings),
        target(settings, L"S0000.Profile42.bak"), SaveFileKind::backup);
    denied(decide(target(settings, L"S0000.co2"), settings));
    denied(decide(L"S0000.Profile42", settings));
}

void same_directory_mode_is_still_only_a_lexical_decision() {
    auto settings = config();
    settings.destination_mode = DestinationMode::same_account_directory;
    settings.coop_profile_root = settings.appdata_root;
    redirect(decide(source(settings), settings), target(settings), SaveFileKind::primary);
    redirect(decide(target(settings), settings), target(settings), SaveFileKind::primary);
}

void backup_length_failure_cannot_produce_an_empty_redirect() {
    auto settings = config();
    settings.extension = L"x";
    settings.appdata_root = L"C:\\synthetic";
    const auto suffix_size = source(settings).size() - settings.appdata_root.size();
    const auto desired_root_size = 32765u - suffix_size;
    while (desired_root_size - settings.appdata_root.size() > 256)
        settings.appdata_root += L"\\" + std::wstring(255, L'a');
    const auto remaining = desired_root_size - settings.appdata_root.size();
    require(remaining >= 2 && remaining <= 256, "invalid long-path fixture construction");
    settings.appdata_root += L"\\" + std::wstring(remaining - 1, L'b');
    require(source(settings).size() == 32765, "long primary boundary fixture changed");
    redirect(decide(source(settings), settings), target(settings, L"S0000.x"), SaveFileKind::primary);
    // The reversed lexical validation fits, but the original backup source
    // exceeds the source-path limit. It must not become redirect with no path.
    denied(decide(target(settings, L"S0000.x.bak"), settings));
}

void copy_pairs_resolve_both_sides_before_io() {
    const auto settings = config();
    const auto result = decide_native_save_pair(source(settings), source(settings, L"S0000.sl2.bak"),
        settings, context(), account);
    require(result.action == NativePathAction::redirect && result.error == NativePathError::none, "copy pair rejected");
    redirect(result.source, target(settings), SaveFileKind::primary);
    redirect(result.target, target(settings, L"S0000.co2.bak"), SaveFileKind::backup);
    const auto restore = decide_native_save_pair(target(settings, L"S0000.co2.bak"), source(settings),
        settings, context(), account);
    require(restore.action == NativePathAction::redirect, "mixed logical/physical bound pair rejected");
    redirect(restore.source, target(settings, L"S0000.co2.bak"), SaveFileKind::backup);
    redirect(restore.target, target(settings), SaveFileKind::primary);
}

void unrelated_pairs_do_not_claim_other_games() {
    const auto result = decide_native_save_pair(L"C:\\another-game\\OtherSave.sl2", L"C:\\another-game\\OtherSave.sl2.bak",
        config(), context(), account);
    require(result.action == NativePathAction::passthrough && result.error == NativePathError::none,
        "unrelated pair was redirected or denied");
    passthrough(result.source);
    passthrough(result.target);
}

void mixed_pairs_are_denied_and_clear_both_destinations() {
    const auto settings = config();
    for (const auto& external : {std::wstring(L"C:\\other\\OtherSave.sl2.bak"),
        std::wstring(L"E:\\different-root\\Sekiro\\") + std::wstring(account) + L"\\OtherSave.co2.bak"}) {
        auto result = decide_native_save_pair(source(settings), external, settings, context(), account);
        denied(result);
        require(result.error == NativePathError::mixed_namespaces, "mixed root reason lost");
        result = decide_native_save_pair(external, source(settings), settings, context(), account);
        denied(result);
    }
}

void cross_account_pairs_fail_even_if_each_tree_is_valid() {
    const auto settings = config();
    auto result = decide_native_save_pair(source(settings), source(settings, L"S0000.sl2.bak", other_account),
        settings, context(), account);
    denied(result);
    require(result.error == NativePathError::account_mismatch, "cross-account pair reason lost");
    result = decide_native_save_pair(target(settings, L"S0000.co2", other_account), target(settings, L"S0000.co2.bak"),
        settings, context(), account);
    denied(result);
}

void same_destination_is_denied_after_redirection() {
    const auto settings = config();
    for (const auto& to : {source(settings), source(settings, L"s0000.SL2"), target(settings)}) {
        const auto result = decide_native_save_pair(source(settings), to, settings, context(), account);
        denied(result);
        require(result.error == NativePathError::same_destination, "same physical destination not recognized");
    }
}

void denied_second_side_clears_previously_accepted_first_side() {
    const auto settings = config();
    const auto result = decide_native_save_pair(source(settings), L".\\S0000.sl2.bak", settings, context(), account);
    denied(result);
    require(result.error == NativePathError::ambiguous_save_path, "second-side denial reason lost");
}

} // namespace

int main() {
    // Pure lexical fixtures: no directories, files, game, AppData or Steam access.
    const std::vector<std::pair<std::string_view, std::function<void()>>> cases{
        {"primary_and_backup_redirect", primary_and_backup_redirect},
        {"already_redirected_inputs_are_idempotent", already_redirected_inputs_are_idempotent},
        {"case_mixed_separators_and_unicode", case_mixed_separators_and_unicode},
        {"account_is_fixed_for_both_namespaces", account_is_fixed_for_both_namespaces},
        {"absolute_unrelated_files_remain_unrelated", absolute_unrelated_files_remain_unrelated},
        {"relative_and_device_save_candidates_are_denied", relative_and_device_save_candidates_are_denied},
        {"known_save_extensions_never_passthrough_inside_trees", known_save_extensions_never_passthrough_inside_trees},
        {"private_appdata_solo_names_redirect_to_cooperative_extension", private_appdata_solo_names_redirect_to_cooperative_extension},
        {"absolutized_fallback_cannot_escape_the_bound_namespace", absolutized_fallback_cannot_escape_the_bound_namespace},
        {"ambiguous_paths_are_not_normalized_into_permission", ambiguous_paths_are_not_normalized_into_permission},
        {"malformed_text_does_not_authorize_a_save", malformed_text_does_not_authorize_a_save},
        {"declared_context_is_revalidated", declared_context_is_revalidated},
        {"invalid_binding_and_configuration_fail_closed", invalid_binding_and_configuration_fail_closed},
        {"configured_extension_is_preserved", configured_extension_is_preserved},
        {"same_directory_mode_is_still_only_a_lexical_decision", same_directory_mode_is_still_only_a_lexical_decision},
        {"backup_length_failure_cannot_produce_an_empty_redirect", backup_length_failure_cannot_produce_an_empty_redirect},
        {"copy_pairs_resolve_both_sides_before_io", copy_pairs_resolve_both_sides_before_io},
        {"unrelated_pairs_do_not_claim_other_games", unrelated_pairs_do_not_claim_other_games},
        {"mixed_pairs_are_denied_and_clear_both_destinations", mixed_pairs_are_denied_and_clear_both_destinations},
        {"cross_account_pairs_fail_even_if_each_tree_is_valid", cross_account_pairs_fail_even_if_each_tree_is_valid},
        {"same_destination_is_denied_after_redirection", same_destination_is_denied_after_redirection},
        {"denied_second_side_clears_previously_accepted_first_side", denied_second_side_clears_previously_accepted_first_side}
    };
    std::size_t failures{};
    for (const auto& [name, test] : cases) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    std::cout << "SAVE_NATIVE_POLICY_RESULT " << (failures == 0 ? "PASS" : "FAIL")
        << " cases=" << cases.size() << " failed=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
