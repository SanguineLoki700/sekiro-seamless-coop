#pragma once
#include <filesystem>
#include <string_view>
#include <string>
#include <cstdint>
#include <functional>

namespace skcoop::launcher {
// Inspection is read-only. Both launch paths retain exact game identity,
// independent installation and save locks. Steam/game retain authentication.
int check_protected_launch(const std::filesystem::path& config);
int steam_protected_launch(const std::filesystem::path& config,
                           std::wstring_view original_command);
struct LaunchStatus {
    bool ready{},finished{},completed{};
    std::uint32_t pid{},hooks{},actor_phase{},actor_error{},network{},network_error{},waiting{};
    std::uint64_t sent{},received{};
    bool paused{};
    std::uint32_t action_error{};
    std::uint64_t actions_captured{},actions_replayed{};
    bool guard_rearm{},guarding{};
    // Exact basename of the already-created host receipt, never a user path.
    std::string receipt_name;
    std::uint32_t native_controller_phase{},native_controller_fault{};
    std::uint64_t native_controller_commands{},native_controller_consumed{},native_controller_guard_ms{},
        native_controller_displacement_mm{},native_controller_manual_events{},native_controller_primary_writes{};
    std::int32_t native_controller_state_code{};
    bool native_controller_output_bound{};
};
using LaunchObserver=std::function<void(const LaunchStatus&)>;
int direct_protected_launch(const std::filesystem::path& config,const std::filesystem::path& runtime_directory={},const LaunchObserver& observer={});
std::filesystem::path prepare_direct_lan(const std::filesystem::path& internal_directory,std::uint32_t role,std::string_view code,bool combat_preview=false);
// Automatic local native-command diagnostic; no LAN role, ticket or server.
// Reuses the independently owned movement-v2 profile and all launch guards.
std::filesystem::path prepare_direct_native(const std::filesystem::path& internal_directory);
// Creates a new private profile and a controlled LAN config, never overwrites
// a save, never edits Steam settings and never starts the game.
std::wstring prepare_lan_test(const std::filesystem::path& internal_directory,
                             std::uint32_t role,std::string_view code);
}
