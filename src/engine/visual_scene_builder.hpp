#pragma once

#include "eaw/ala_animation.hpp"
#include "eaw/alo_model.hpp"
#include "eaw/ted_map.hpp"
#include "eaw/visual_scene.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

class AssetReader;
class AssetCache;

struct VisualModelAsset
{
    VisualScene scene;
    AloModel model;
    std::vector<AloMatrix4> inverse_bind_pose;
};

// A stock or mod map's placed objects as one instanced scene (no terrain yet). Counts describe
// placed objects that could not be drawn and why.
struct VisualMapAsset
{
    VisualScene scene;
    bool land{};
    std::size_t untyped{};
    std::size_t without_model{};
    std::size_t missing_model_files{};
    std::size_t expanded_members{};
    std::size_t unexpanded_containers{};
    std::size_t unrenderable_placements{};
    // Resolved backdrop layers ("definition (model path)"), in draw order.
    std::vector<std::string> backdrops;
    // Lighting of the active environment, when it carries a complete lighting block.
    std::optional<VisualLightRig> lighting;
    std::string environment_name;
    // Instances colored by their type's affiliated faction versus the player-slot fallback.
    // Liquid surface added over the terrain (none when the map has none or it is not visible).
    TedWaterType water{TedWaterType::none};
    // Track ribbons drawn over the terrain.
    std::size_t tracks{};
    std::size_t faction_colored{};
    std::size_t player_colored{};
};

// Builders read through `reader` (pass a RecordingAssetReader to collect the scene's file
// dependencies). With a cache, decoded models, textures, maps, and the XML object catalog are
// reused across builds until their source files change.
[[nodiscard]] VisualModelAsset build_visual_model(const AssetReader& reader, std::string_view model_path, AssetCache* cache = nullptr);
[[nodiscard]] VisualMapAsset build_visual_map(const AssetReader& reader, std::string_view map_path, AssetCache* cache = nullptr);
[[nodiscard]] std::optional<std::string> default_model_animation(const AssetReader& reader, std::string_view model_path);
[[nodiscard]] std::vector<std::array<float, 16>> sample_skin_palette(const VisualModelAsset& asset, const AlaAnimation* animation, float seconds);

} // namespace eaw
