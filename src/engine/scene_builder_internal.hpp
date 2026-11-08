#pragma once

// Shared pieces of the visual scene builders (scene_materials, scene_models, scene_terrain,
// object_type_properties, visual_map_builder); not part of the engine's public interface.

#include "asset_cache.hpp"
#include "visual_scene_builder.hpp"

#include "eaw/alo_model.hpp"
#include "eaw/game_object_catalog.hpp"
#include "eaw/legacy_material.hpp"
#include "eaw/ted_map.hpp"
#include "eaw/virtual_file_system.hpp"
#include "eaw/xml_asset_database.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eaw::scene_build {

// Accumulates models into one scene with a shared, deduplicated texture table.
struct SceneBuilder
{
    const AssetReader& vfs;
    VisualScene& scene;
    std::unordered_map<std::string, std::uint32_t> texture_indices;
    AssetCache* cache{};
};

// Loads through the cache when there is one; otherwise decodes directly.
template <class T, class Loader> std::shared_ptr<const T> cached(AssetCache* cache, const std::string_view kind, const std::string_view key, const AssetReader& reader, Loader&& loader)
{
    if (cache != nullptr)
    {
        return cache->load<T>(kind, key, reader, std::forward<Loader>(loader));
    }
    return std::make_shared<const T>(loader(reader));
}

// A decoded DDS for an ALO texture reference, or why it is unavailable (cached either way: the
// failed lookups are dependencies, so adding the file later invalidates the entry).
struct CachedTexture
{
    std::optional<VisualTexture> texture;
    std::string error;
};

// The XML object catalog borrows its database, so both live behind one stable allocation.
struct CachedCatalog
{
    std::unique_ptr<XmlAssetDatabase> database;
    GameObjectCatalog catalog;
};

// Terrain scale established from the stock corpus: vertices are 20 world units apart (column ->
// +X, row -> +Y) and stored elevation is 20x the world height, which places map objects within a
// few world units of the interpolated ground.
inline constexpr float terrain_sample_spacing = 20.0F;

inline constexpr float terrain_elevation_scale = 20.0F;

// Explicit preview limit (not a format limit): larger grids are skipped with a diagnostic until
// terrain streaming exists.
inline constexpr std::uint32_t maximum_terrain_dimension = 4096;

inline constexpr std::string_view movement_class_path = "Data/XML/MovementClassTypeDefs.xml";

// Movement classes from Data/XML/MovementClassTypeDefs.xml (the winning layer, so mods apply): class
// name (folded) -> passability types it may enter, plus problems to report as scene warnings.
struct MovementClasses
{
    std::unordered_map<std::string, std::uint32_t> classes;
    std::vector<std::string> warnings;
};

// scene_materials.cpp: textures, materials, lighting, and team colors.

// ASCII lowercase copy (case-insensitive keys).
std::string fold_ascii(std::string value);

const LegacyTextureBinding* base_color_texture(const LegacyMaterial& material);

// Loads (once per scene) the DDS for an ALO texture reference and returns its scene texture
// index, or invalid_visual_texture with a warning when it is missing or unsupported.
std::uint32_t scene_texture(SceneBuilder& builder, const std::string& reference);

// Translates a legacy material profile plus its authored parameters into the engine-native
// legacy-mode surface description (see docs/legacy-shader-mapping.md for the source behavior).
VisualMaterial make_visual_material(SceneBuilder& builder, const LegacyMaterial& material);

// Converts an environment's lighting block. Each light's direction comes from its azimuth (about
// +Z) and elevation (above the XY plane) and points toward the light; colors are scaled by their
// intensity. The highlight uses the specular color scaled by the key intensity. The direction
// convention and the specular scaling are inferences pending comparison with the original game.
VisualLightRig environment_light_rig(const TedEnvironment& environment);

// Whether a light rig decoded from map data is usable (finite values, non-zero directions).
bool light_rig_valid(const VisualLightRig& rig) noexcept;

// Fallback colors by player slot for types without a faction affiliation. Index 0 is neutral.
std::array<float, 4> player_team_color(std::uint32_t player) noexcept;

// scene_models.cpp: models and scene bounds.

// The decoded model, or why it could not be decoded (cached either way: a failed decode depends on
// the file, so replacing it invalidates the entry).
std::shared_ptr<const Result<AloModel>> cached_model(SceneBuilder& builder, const std::string& path);

// Appends one ALO model as a new mesh (its draws, vertices, indices, and any new textures) and
// returns the mesh index. Vertices stay in bind-pose model space with model-wide bone indices.
// With `animated_visibility`, the mesh's palette has two halves: [0, N) the skin matrices and
// [N, 2N) the same matrices collapsed to a point while their bone is hidden. Rigid attachments
// reference the second half, so meshes on bones hidden in the bind pose are included and appear
// when an animation shows their bone. Without it (maps share one bind-pose palette), such meshes
// are omitted.
// A model that cannot be appended leaves no geometry, indices, or materials behind.
Result<std::uint32_t> append_model(SceneBuilder& builder, const AloModel& model, std::string_view name, bool animated_visibility = false);

// Decodes and appends a model referenced by a map (placed object, backdrop, or spawn type). Particle
// ALOs and malformed models are reported once as scene warnings and yield nothing.
std::optional<std::uint32_t> append_scene_model(SceneBuilder& builder, const std::string& model_path);

// Expands the scene bounds by every instance's transformed mesh bounds and the terrain.
void update_scene_bounds(VisualScene& scene);

// Separation footprint of a mesh placed with `transform` (row-vector): a quarter of the summed X and
// Y extents of its bounds, scaled by the transform's horizontal scale. An engine choice until
// collision meshes and UnitCollisionClass are interpreted.
float mesh_footprint_radius(const VisualScene& scene, std::uint32_t mesh, const std::array<float, 16>& transform);

// The ground rectangle of a mesh placed with `transform`: its bounds' X/Y extent, rotated and scaled
// with the placement, with the given passability type. An engine choice until collision meshes
// are read.
GroundFootprint mesh_ground_footprint(const VisualScene& scene, std::uint32_t mesh, const std::array<float, 16>& transform, std::uint32_t passability_type);

// scene_terrain.cpp: land terrain, liquid surfaces, and tracks.

// Builds the land heightfield as independent triangles whose vertices carry the triangle's corner
// layers. Each TED layer becomes a material; its texture tiles world X/Y once per tile_size.
void append_terrain(SceneBuilder& builder, const TedMap& map);

// Water, lava, and ice surfaces: a flat quad over the terrain at the map's water level. Texture
// roles follow the corpus: for water the first texture (0x1d) is a bump map and the second (0x1e)
// a plain color fallback; lava and ice store their color texture first. Tiling (one repeat per
// 200 world units) and scroll speeds are engine choices; the related TED fields are preserved but
// not interpreted yet.
std::optional<std::uint32_t> append_water(SceneBuilder& builder, const TedMap& map);

// Tracks become unlit, alpha-blended ribbons draped over the terrain: the centerline is the
// control-point polyline (sampled at least every terrain cell), each point's width spans the
// ribbon, and opacity follows the start/middle/end keys. Texture V repeats once per average
// width along the track; the header's scroll values animate it.
std::size_t append_tracks(SceneBuilder& builder, const TedMap& map);

// object_type_properties.cpp: simulation-relevant fields of XML object types.

// Whether a type walks on the ground of land maps. XML Space_Layer is "Land" for ground vehicles,
// infantry, and heroes, and "None" for walkers such as Ewoks; air units (snowspeeders, air whales)
// use "Frigate", "Capital", or "Corvette", and birds or space-only types have none. This reading is
// a corpus inference (see docs/compatibility-research.md).
bool type_follows_ground(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// Reads movement_class_path from the winning layer; problems become warnings.
MovementClasses load_movement_classes(const AssetReader& reader);

// Passability types a type may enter: its MovementClass's entry, or Clear | Shield (what most
// ground classes allow) when the type names no known class.
std::uint32_t type_passable_types(const GameObjectCatalog& catalog, const XmlDefinition& definition, const std::unordered_map<std::string, std::uint32_t>& classes);

// A numeric field through the variant chain; 0 when it is missing or not a finite number.
float type_float_field(const GameObjectCatalog& catalog, const XmlDefinition& definition, std::string_view field);

// XML FormationOrder (default 2, the most common) and FormationSpacing (default 0.5, likewise).
std::uint32_t type_formation_order(const GameObjectCatalog& catalog, const XmlDefinition& definition);
float type_formation_spacing(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// The passability type an immobile placed object stamps on the ground, or nothing when it does not
// block: its XML OverridePassability (walls: RebelWall, EmpireWall), otherwise Obstacle for
// structures (GroundStructure, SpecialStructure, SecondaryStructure) and for types whose
// UnitCollisionClass names an obstacle (High, Medium, or Low Obstacle). Build pads and other
// props do not block. A provisional reading of the stock data.
std::optional<std::uint32_t> type_obstacle_passability(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// XML Custom_Soft_Footprint_Radius (infantry 3.5), or 0 when it is missing.
float type_soft_footprint_radius(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// XML Max_Rate_Of_Turn in radians per second (provisional_turn_rate_scale); 0 turns instantly.
float type_turn_rate(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// Minimum flight speed of fighters and bombers (SpaceBehavior names FIGHTER_LOCOMOTOR): XML
// Min_Speed on the Max_Speed scale, or half of `max_speed` without one; 0 for every other type.
float type_min_flight_speed(const GameObjectCatalog& catalog, const XmlDefinition& definition, float max_speed);

// XML Max_Speed in world units per second (the provisional scale in visual_scene_builder.hpp);
// unparsable values are 0 with a warning.
float type_max_speed(const GameObjectCatalog& catalog, const XmlDefinition& definition, std::vector<std::string>& warnings);

} // namespace eaw::scene_build
