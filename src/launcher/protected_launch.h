#pragma once
#include <filesystem>
#include <string_view>

namespace skcoop::launcher {
// Inspection is read-only. Launch is only accepted through Steam's own
// original Sekiro command; multiplayer/gameplay remains unavailable.
int check_protected_launch(const std::filesystem::path& config);
int steam_protected_launch(const std::filesystem::path& config,
                           std::wstring_view original_command);
}
