#pragma once

#include "core/status.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

enum class GameVariant
{
    empire_at_war,
    forces_of_corruption,
};

struct GameProfile
{
    std::filesystem::path root;
    std::filesystem::path runtime_root;
    std::filesystem::path data_root;
    GameVariant variant{GameVariant::forces_of_corruption};
};

[[nodiscard]] std::string_view to_string(GameVariant variant) noexcept;
[[nodiscard]] std::optional<GameVariant> parse_game_variant(std::string_view text);
[[nodiscard]] std::optional<GameProfile> make_game_profile(const std::filesystem::path& root, GameVariant variant);

// Whether a folder given as the game's root is an installation (GameData\Data and
// corruption\Data). The error says why not, and names a mod folder given by mistake (a folder with
// Data that is not the game) as one. Callers check an explicit root with it first: discover() only
// adds a root that is an installation and otherwise finds others, which would silently replace a
// wrong explicit root.
[[nodiscard]] Status check_installation_root(const std::filesystem::path& root);

class InstallationLocator final
{
public:
    [[nodiscard]] std::vector<std::filesystem::path> discover(const std::optional<std::filesystem::path>& explicit_root = std::nullopt) const;
};

// Returns archive paths in low-to-high override order.
[[nodiscard]] std::vector<std::filesystem::path> discover_meg_archives(const std::filesystem::path& runtime_root);

} // namespace eaw
