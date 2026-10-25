#pragma once
#include <filesystem>
#include <string_view>
#include <string>
#include <cstdint>

namespace skcoop::launcher {
// Inspection is read-only. Launch is only accepted through Steam's own
// original Sekiro command; multiplayer/gameplay remains unavailable.
int check_protected_launch(const std::filesystem::path& config);
int steam_protected_launch(const std::filesystem::path& config,
                           std::wstring_view original_command);
// Creates a new private profile and a controlled LAN config, never overwrites
// a save, never edits Steam settings and never starts the game.
std::wstring prepare_lan_test(const std::filesystem::path& internal_directory,
                             std::uint32_t role,std::string_view code);
}
