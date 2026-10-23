#pragma once

#include <filesystem>
#include <string>

namespace skcoop::launcher {

struct SteamInstall {
    std::filesystem::path steam_root;
    std::filesystem::path library_root;
    std::filesystem::path manifest_path;
    std::filesystem::path executable;
    std::string build_id;
};

// Empty root reads HKCU\Software\Valve\Steam\SteamPath. An explicit root is
// useful for inspection and offline fixtures. Metadata is bounded and parsed;
// this does not authenticate the executable or pin any path against changes.
// Only appmanifest_814380.acf selects an install: sibling test copies are never
// guessed from directory names. Ambiguous/incomplete installations throw.
SteamInstall inspect_steam_install(const std::filesystem::path& steam_root = {});

}
