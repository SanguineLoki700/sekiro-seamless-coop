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
struct LaunchStatus { bool ready{},finished{},completed{};std::uint32_t pid{},hooks{},actor_phase{},actor_error{},network{},network_error{},waiting{};std::uint64_t sent{},received{};bool paused{}; };
using LaunchObserver=std::function<void(const LaunchStatus&)>;
int direct_protected_launch(const std::filesystem::path& config,const std::filesystem::path& runtime_directory={},const LaunchObserver& observer={});
std::filesystem::path prepare_direct_lan(const std::filesystem::path& internal_directory,std::uint32_t role,std::string_view code);
// Creates a new private profile and a controlled LAN config, never overwrites
// a save, never edits Steam settings and never starts the game.
std::wstring prepare_lan_test(const std::filesystem::path& internal_directory,
                             std::uint32_t role,std::string_view code);
}
