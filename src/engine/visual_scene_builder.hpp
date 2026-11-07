#pragma once

#include "eaw/ala_animation.hpp"
#include "eaw/simulation.hpp"
#include "terrain_height.hpp"
#include "eaw/alo_model.hpp"
#include "eaw/ted_map.hpp"
#include "eaw/visual_scene.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
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
    // The scene's palette has a second, visibility-collapsed half for rigid attachments (see
    // sample_skin_palette).
    bool visibility_palette{};
};

// A stock or mod map's placed objects as one instanced scene (no terrain yet). Counts describe
// placed objects that could not be drawn and why.
// A placed map object as a simulation entity: its spawn data and the scene instance that showed it
// at load (mesh, original transform, colors), which presentation re-poses from snapshots.
struct MapEntitySpawn
{
    EntitySpawn spawn;
    std::uint32_t instance{};
};

// XML Max_Speed values are small (infantry 0.8, AT-AT 0.5, TIE fighter 4.5), consistent with world
// units per logic frame. The logic rate is unmeasured; until it is, speeds are scaled as if per 1/30 s.
// This is a provisional engine assumption, not compatibility evidence.
inline constexpr float provisional_max_speed_scale = 30.0F;

// An XML object type that can be spawned at runtime: its model is part of the map scene's meshes.
struct SpawnableType
{
    std::string type_name;
    std::uint32_t mesh{};
    float max_speed{};
    std::array<float, 4> team_color{1.0F, 1.0F, 1.0F, 1.0F};
};

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
    // One entity per drawn placed object (not backdrops), in scene instance order.
    std::vector<MapEntitySpawn> entities;
    // Types requested for runtime spawning that resolved to a model, in request order.
    std::vector<SpawnableType> spawnable;
    // Ground surface for CPU queries (land maps; invalid for space).
    TerrainHeightField terrain_heights;
};

// Builders read through `reader` (pass a RecordingAssetReader to collect the scene's file
// dependencies). With a cache, decoded models, textures, maps, and the XML object catalog are
// reused across builds until their source files change. A model or map that cannot be built (not
// found, malformed, or without renderable geometry) returns an error naming its virtual path;
// problems that only drop parts of a scene are listed in its warnings instead.
[[nodiscard]] Result<VisualModelAsset> build_visual_model(const AssetReader& reader, std::string_view model_path, AssetCache* cache = nullptr);
// `spawn_types` names XML object types whose models are added to the scene's meshes so they can be
// spawned at runtime; types without a model for the map's context are reported as warnings.
[[nodiscard]] Result<VisualMapAsset> build_visual_map(const AssetReader& reader, std::string_view map_path, AssetCache* cache = nullptr, std::span<const std::string> spawn_types = {});
[[nodiscard]] std::optional<std::string> default_model_animation(const AssetReader& reader, std::string_view model_path);
// Every <MODEL>_*.ALA beside the model, sorted by virtual path.
[[nodiscard]] std::vector<std::string> model_animations(const AssetReader& reader, std::string_view model_path);
// Skin matrices for the model's bones (bind pose without an animation). With a visibility palette,
// a second half repeats them, collapsed to a point for bones the pose hides.
[[nodiscard]] std::vector<std::array<float, 16>> sample_skin_palette(const VisualModelAsset& asset, const AlaAnimation* animation, float seconds);

} // namespace eaw
