#pragma once

#include "vfs/game_installation.hpp"
#include "vfs/virtual_file_system.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace eaw {

struct AssetMountReport
{
    std::vector<std::string> warnings;
};

// Builds the same low-to-high data precedence used by the engine. Mod paths are
// accepted in user-facing high-to-low priority order (first mod wins).
// Mods are mounted above the game, the first one highest. A relative mod path is looked up as the
// game does with MODPATH (Steam launch options: MODPATH=Mods\Name), under the runtime root
// (corruption\ or GameData\) first, then as given (the working directory).
[[nodiscard]] AssetMountReport mount_game_assets(VirtualFileSystem& vfs, const GameProfile& profile, std::span<const std::filesystem::path> mods = {});

// Where a mod path points: an absolute one as it is; a relative one under the profile's runtime
// root when a Data folder is there, else as given.
[[nodiscard]] std::filesystem::path resolve_mod_path(const GameProfile& profile, const std::filesystem::path& mod);

} // namespace eaw
