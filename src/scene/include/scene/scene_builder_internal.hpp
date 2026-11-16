#pragma once

// Shared pieces of the visual scene builders (scene_materials, scene_models, scene_terrain,
// object_type_properties, visual_map_builder); not part of the engine's public interface.

#include "math/matrix.hpp"
#include "math/vector.hpp"
#include "vfs/virtual_file_system.hpp"
#include "formats/ted_map.hpp"
#include "assets/asset_cache.hpp"
#include "assets/game_object_catalog.hpp"
#include "assets/legacy_material.hpp"
#include "assets/map_object_layout.hpp"
#include "assets/model.hpp"
#include "assets/model_geometry.hpp"
#include "assets/resource_loaders.hpp"
#include "assets/resource_manager.hpp"
#include "assets/xml_asset_database.hpp"
#include "battle/battle_map_build.hpp"
#include "battle/combat_content.hpp"
#include "battle/object_types.hpp"
#include "scene/visual_scene_builder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eaw::scene_build {

// Bounds that any point extends: every axis starts inverted.
constexpr Vector3 empty_bounds_minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
constexpr Vector3 empty_bounds_maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

// Explicit preview limit (not a format limit): larger grids are skipped with a diagnostic until
// terrain streaming exists.
inline constexpr std::uint32_t maximum_terrain_dimension = 4096;

// Particle effects a build loaded, with their own deduplicated texture list (effect texture
// indices point into it; the caller registers it with the renderer, which renumbers them).
struct ParticleLibrary
{
    // Folded model path -> effect (null when the file is missing or not a particle system).
    std::unordered_map<std::string, std::shared_ptr<const ParticleEffect>> effects;
    std::vector<VisualTexture> textures;
    std::unordered_map<std::string, std::uint32_t> texture_indices;
};

// Accumulates models into one scene with a shared, deduplicated texture table.
struct SceneBuilder
{
    const AssetReader& vfs;
    VisualScene& scene;
    std::unordered_map<std::string, std::uint32_t> texture_indices;
    AssetCache* cache{};
    // Headless builds skip decoding what only drawing needs (MapBuildOptions).
    bool textures{true};
    bool animation_clips{true};
    // Particle effects of model proxies (filled when textures are built; headless builds skip them),
    // normalized under this fidelity profile.
    ParticleLibrary particles;
    ParticleFidelity particle_fidelity{ParticleFidelity::legacy};
    // Folded model path -> bones that the hardpoints of types using the model name in
    // Damage_Particles (their proxies are damage effects, not shown on intact ships) and in
    // Damage_Decal (their meshes are scorch marks, hidden on intact ships).
    std::unordered_map<std::string, std::unordered_set<std::uint32_t>> damage_bones;
    std::unordered_map<std::string, std::unordered_set<std::uint32_t>> decal_bones;
    // Folded model path -> Engine_Particles bones (proxies under them are engine effects).
    std::unordered_map<std::string, std::unordered_set<std::uint32_t>> engine_bones;
    // Folded model path -> the model whose clips it plays (XML Land_Model_Anim_Override_Name of the
    // first type that appended it).
    std::unordered_map<std::string, std::string> animation_sources;
    // Folded model path -> its shield flash mesh (append_shield_flash), once per model.
    std::unordered_map<std::string, std::optional<std::uint32_t>> shield_flash_meshes;
};

// The builder's resources: its reader and, when there is one, the shared cache.
[[nodiscard]] inline ResourceManager resources(const SceneBuilder& builder) noexcept
{
    return ResourceManager(builder.vfs, builder.cache);
}

// A decoded DDS for an ALO texture reference, or why it is unavailable (cached either way: the
// failed lookups are dependencies, so adding the file later invalidates the entry).
struct CachedTexture
{
    std::optional<VisualTexture> texture;
    std::string error;
};

} // namespace eaw::scene_build

namespace eaw {

// How a decoded texture loads: its path is the ALO's reference to it.
template <> struct ResourceLoader<scene_build::CachedTexture>
{
    using Value = scene_build::CachedTexture;
    static constexpr std::string_view kind = "texture";
    [[nodiscard]] static std::string key(std::string_view reference);
    [[nodiscard]] static Value load(const AssetReader& reader, std::string_view reference);
};

} // namespace eaw

namespace eaw::scene_build {

// scene_materials.cpp: textures, materials, lighting, and team colors.

const LegacyTextureBinding* base_color_texture(const LegacyMaterial& material);

// Loads (once per scene) the DDS for an ALO texture reference and returns its scene texture
// index, or invalid_visual_texture with a warning when it is missing or unsupported.
std::uint32_t scene_texture(SceneBuilder& builder, const std::string& reference);

// The decoded DDS for a texture reference through the cache (or directly without one).
std::shared_ptr<const CachedTexture> cached_texture(const AssetReader& vfs, AssetCache* cache, const std::string& reference);

// scene_particles.cpp: particle effects.

// The particle effect a model reference names (resolved like a model path; a proxy name without
// directory or extension resolves under Data/Art/Models), loaded once per build with its textures
// added to builder.particles; null with a warning when it is missing or malformed, and null without
// one for a mesh ALO (proxies may name meshes).
std::shared_ptr<const ParticleEffect> particle_effect(SceneBuilder& builder, const std::string& reference);

// A texture added to builder.particles by its ALO-style reference (once), or invalid_visual_texture
// with a warning naming `context`.
std::uint32_t particle_library_texture(SceneBuilder& builder, const std::string& reference, const std::string& context);

// Reads the projectile visuals `keys` name into `table` and, when any exists, loads the bolt atlas
// into builder.particles (its index in `atlas`).
void load_projectile_visuals(SceneBuilder& builder, const GameObjectCatalog& catalog, std::span<const std::uint32_t> keys, ProjectileVisualTable& table, std::uint32_t& atlas);
// Gives the table's model projectiles (no bolt) their look: the mesh of their model, appended to the
// scene once per model (`meshes` deduplicates, as for types), or the particle system it names, and
// their Scale_Factor. Call before the rigs are built, so model proxies (trails) get attachments.
void load_projectile_models(SceneBuilder& builder, const GameObjectCatalog& catalog, bool land, std::unordered_map<std::string, std::optional<std::uint32_t>>& meshes, ProjectileVisualTable& table);
// scene_debris.cpp: the debris props `combats`' hardpoints break off (Death_Breakoff_Prop), their
// models joining the scene's meshes by `meshes`, and the Particle objects they play.
void load_debris_types(SceneBuilder& builder, const GameObjectCatalog& catalog, std::span<const CombatProfile* const> combats, bool land,
                       std::unordered_map<std::string, std::optional<std::uint32_t>>& meshes, DebrisTable& table);
void collect_debris_effect_keys(const DebrisTable& table, std::vector<std::uint32_t>& keys);
// Resolves XML Particle objects (by name CRC) to the effect their land or space model names and their
// Scale_Factor; undefined objects and unloadable effects stay in the table with no effect.
// On land, an object's scorch mark is resolved through `database`'s SurfaceFX and TerrainDecalFX, and
// the decal atlas is loaded into builder.particles when any has one (its index in `decal_atlas`).
void load_effect_objects(SceneBuilder& builder, const GameObjectCatalog& catalog, const XmlAssetDatabase& database, std::span<const std::uint32_t> keys, bool land, ParticleEffectObjectTable& table,
                         std::uint32_t& decal_atlas);

// Notes the Damage_Particles and Damage_Decal bones of a type's hardpoints for the model at
// `model_path`.
void note_damage_bones(SceneBuilder& builder, const std::string& model_path, const CombatProfile& combat);

// The particle effects attached to a model's proxies (those not hidden by default). Proxies under a
// noted damage bone are marked as hardpoint damage effects.
std::vector<VisualEffectAttachment> proxy_effects(SceneBuilder& builder, const Model& model, const std::string& model_path);

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
Color player_team_color(std::uint32_t player) noexcept;

// scene_models.cpp: models and scene bounds.

// The decoded model, or why it could not be decoded (cached either way: a failed decode depends on
// the file, so replacing it invalidates the entry).
std::shared_ptr<const Result<Model>> cached_model(SceneBuilder& builder, const std::string& path);

// Appends one ALO model as a new mesh (its draws, vertices, indices, and any new textures) and
// returns the mesh index. Vertices stay in bind-pose model space with model-wide bone indices.
// With `animated_visibility`, the mesh's palette has two halves: [0, N) the skin matrices and
// [N, 2N) the same matrices collapsed to a point while their bone is hidden. Rigid attachments
// reference the second half, so meshes on bones hidden in the bind pose are included and appear
// when an animation shows their bone. Without it (maps share one bind-pose palette), such meshes
// are omitted.
// A model that cannot be appended leaves no geometry, indices, or materials behind.
// A model attached rigidly to a bone of the model it joins: an XML hardpoint's Model_To_Attach at
// its Attachment_Bone (turrets, gun batteries, shield generators drawn as their own models).
// `attachments` are appended into the same mesh, placed at their bone's bind pose and bound to that
// bone (so they follow its pose and visibility).
// Which meshes of a model append_model draws: the ones shown (drawn_mesh), or its shield flash: the
// hidden mesh named "shield" (capital ships, base shield generators) that presentation shows when the
// shield takes a hit.
enum class ModelPart : std::uint8_t
{
    drawn,
    shield_flash,
};

Result<std::uint32_t> append_model(SceneBuilder& builder, const Model& model, std::string_view name, bool animated_visibility = false, std::span<const ModelAttachment> attachments = {},
                                   ModelPart part = ModelPart::drawn);
// The model's shield flash as its own mesh (named after the model, so it takes no rig, effects, or
// clips of its own: it is drawn in the model's rest pose); none when the model has no shield mesh.
std::optional<std::uint32_t> append_shield_flash(SceneBuilder& builder, const std::string& model_path);

// Decodes and appends a model referenced by a map (placed object, backdrop, or spawn type), with its
// attachments. Particle ALOs and malformed models are reported once as scene warnings and yield
// nothing.
std::optional<std::uint32_t> append_scene_model(SceneBuilder& builder, const std::string& model_path, std::span<const ModelAttachment> attachments = {});

// visual_map_builder.cpp: how what the walk built is drawn.

// Appends the looks of `types` from looks.size() on (their meshes join the builder's scene once per
// model and attachments, by `meshes`); a type whose model could not be drawn is hidden, with a
// warning. On `land` they carry their land model's damage states.
void add_spawnable_looks(SceneBuilder& builder, const GameObjectCatalog& catalog, std::unordered_map<std::string, std::optional<std::uint32_t>>& meshes, std::span<const SpawnableType> types,
                         std::span<const BuiltModel> models, std::vector<SpawnableLook>& looks, bool land);

// A rig per scene mesh (its model, inverse bind pose, role clips, and rest palette range), the
// concatenated rest palette, every instance's bone offset, and the scene's palette size.
void build_map_rigs(SceneBuilder& builder, std::vector<VisualRig>& rigs, std::vector<Matrix4>& rest_palette);

// Expands the scene bounds by every instance's transformed mesh bounds and the terrain.
void update_scene_bounds(VisualScene& scene);

// type_looks.cpp: what XML object types say about how they are drawn.

// XML LOD_Bias: how much farther the type keeps its detail (1 when missing or not positive).
float type_lod_bias(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// XML Bank_Turn_Angle in radians (degrees in the file; fighters 40, capital ships 5), at most 90
// degrees; default_bank_limit when absent.
float type_bank_limit(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// A type's AnimationProfile (Movement_Animation_Speed, Walk_Animation_Speed, Idle_Anim_00_Rate_Mod,
// Allow_Idle_When_Moving, the turret tags); rates that are missing or not positive are 1.
AnimationProfile type_animation_profile(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// XML Is_Marker.
bool type_is_marker(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// XML Land_Model_Anim_Override_Name (or Space_Model_Anim_Override_Name), inherited through
// Variant_Of_Existing_Type: the model whose clips the type plays instead of its own (27 stock uses:
// civilians, slaves, and field commanders on RI_INFANTRY, EI_TROOPER, or NI_GMale_Urban_A, Ewok
// variants on NI_EWOK). Empty when the type plays its own model's clips.
std::string type_animation_source(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// Records a type's animation source for a model it appended (the first type wins).
void note_animation_source(SceneBuilder& builder, const std::string& model_path, const GameObjectCatalog& catalog, const XmlDefinition& definition);
// The corpse style of a type from its normal death clone (DeathStyle).
DeathStyle type_death_style(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// Whether shadows fall on the type's model: not on backdrops (XML In_Background: planets, nebulas).
bool type_receives_shadows(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// XML Occlusion_Silhouette_Enabled: the type shows through what hides it.
bool type_silhouette(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// Adds the trails `type` lays while it moves (its SurfaceFX_Name entries whose default surface names a
// dynamic track: one layer per tread bone, else one under the unit) to `table`, loading their
// textures into builder.particles; a type already in the table is left as it is.
void add_track_layers(SceneBuilder& builder, const GameObjectCatalog& catalog, const SurfaceEffectCatalog& surfaces, const DynamicTrackCatalog& tracks, const XmlDefinition& type,
                      TrackLayerTable& table);

// scene_terrain.cpp: land terrain, liquid surfaces, and tracks.

// Builds the land heightfield as independent triangles whose vertices carry the triangle's corner
// layers. Each TED layer becomes a material; its texture tiles world X/Y once per tile_size.
void append_terrain(SceneBuilder& builder, const TedMapData& map);

// Water, lava, and ice surfaces: a flat quad over the terrain at the map's water level. Texture
// roles follow the corpus: for water the first texture (0x1d) is a bump map and the second (0x1e)
// a plain color fallback; lava and ice store their color texture first. Tiling (one repeat per
// 200 world units) and scroll speeds are engine choices; the related TED fields are preserved but
// not interpreted yet.
std::optional<std::uint32_t> append_water(SceneBuilder& builder, const TedMapData& map);

// Whether the map's water is open sea, where enhanced water's waves run (VisualLightRig::ocean): a
// list of ocean planets matched in the map's file name (Kamino, Manaan; the owner's choice,
// 2026-11-16: the share of the map under water cannot tell a sea from a swamp or a lake). The
// Graphics panel's waves setting overrides it; content made for this engine will say so in metadata.
[[nodiscard]] bool ocean_map(std::string_view map_path);

// Tracks become unlit, alpha-blended ribbons draped over the terrain: the centerline is the
// control-point polyline (sampled at least every terrain cell), each point's width spans the
// ribbon, and opacity follows the start/middle/end keys. Texture V repeats once per average
// width along the track; the header's scroll values animate it.
std::size_t append_tracks(SceneBuilder& builder, const TedMapData& map);

} // namespace eaw::scene_build
