#pragma once

#include "eaw/game_installation.hpp"
#include "eaw/virtual_file_system.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace eaw {

struct AssetMountReport {
    std::vector<std::string> warnings;
};

// Builds the same low-to-high data precedence used by the engine. Mod paths are
// accepted in user-facing high-to-low priority order (first mod wins).
[[nodiscard]] AssetMountReport mount_game_assets(
    VirtualFileSystem& vfs,
    const GameProfile& profile,
    std::span<const std::filesystem::path> mods = {});

} // namespace eaw

