#pragma once

// Shared pieces of the visual scene builders (scene_materials, scene_models, scene_terrain,
// object_type_properties, visual_map_builder); not part of the engine's public interface.

#include "assets/asset_cache.hpp"
#include "assets/resource_loaders.hpp"
#include "assets/resource_manager.hpp"
#include "combat_content.hpp"
#include "visual_scene_builder.hpp"

#include "assets/game_object_catalog.hpp"
#include "assets/legacy_material.hpp"
#include "formats/ted_map.hpp"
#include "vfs/virtual_file_system.hpp"
#include "assets/xml_asset_database.hpp"
#include "assets/model.hpp"
#include "math/matrix.hpp"
#include "math/vector.hpp"

#include <functional>
#include <unordered_set>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eaw::scene_build {

// A skin matrix that collapses every vertex of a hidden bone onto the origin, so it rasterizes
// nothing.
constexpr Matrix4 collapsed_bone{std::array<float, 16>{0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F}};

// Bounds that any point extends: every axis starts inverted.
constexpr Vector3 empty_bounds_minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
constexpr Vector3 empty_bounds_maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

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

} // namespace eaw::scene_build

namespace eaw {

// How the builders' own cached types load. The path of a texture is the ALO's reference to it;
// the catalog, movement classes, and combat content each have one fixed source.
template <> struct ResourceLoader<scene_build::CachedTexture>
{
    using Value = scene_build::CachedTexture;
    static constexpr std::string_view kind = "texture";
    [[nodiscard]] static std::string key(std::string_view reference);
    [[nodiscard]] static Value load(const AssetReader& reader, std::string_view reference);
};

template <> struct ResourceLoader<scene_build::CachedCatalog>
{
    using Value = scene_build::CachedCatalog;
    static constexpr std::string_view kind = "xml-catalog";
    [[nodiscard]] static std::string key(std::string_view path);
    [[nodiscard]] static Value load(const AssetReader& reader, std::string_view path);
};

template <> struct ResourceLoader<scene_build::MovementClasses>
{
    using Value = scene_build::MovementClasses;
    static constexpr std::string_view kind = "movement-classes";
    [[nodiscard]] static std::string key(std::string_view path);
    [[nodiscard]] static Value load(const AssetReader& reader, std::string_view path);
};

template <> struct ResourceLoader<CombatContent>
{
    using Value = CombatContent;
    static constexpr std::string_view kind = "combat-content";
    [[nodiscard]] static std::string key(std::string_view path);
    [[nodiscard]] static Value load(const AssetReader& reader, std::string_view path);
};

} // namespace eaw

namespace eaw::scene_build {

// The XML catalog's source (the whole XML folder) and the combat content's.
inline constexpr std::string_view xml_catalog_path = "Data/XML";
inline constexpr std::string_view combat_content_path = "Data/XML/GameConstants.xml";

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
// Resolves XML Particle objects (by name CRC) to the effect their land or space model names and their
// Scale_Factor; undefined objects and unloadable effects stay in the table with no effect.
void load_effect_objects(SceneBuilder& builder, const GameObjectCatalog& catalog, std::span<const std::uint32_t> keys, bool land, ParticleEffectObjectTable& table);

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
std::array<float, 4> player_team_color(std::uint32_t player) noexcept;

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
struct ModelAttachment
{
    std::string path;
    std::shared_ptr<const Model> model;
    std::uint32_t bone{};
};

// `attachments` are appended into the same mesh, placed at their bone's bind pose and bound to that
// bone (so they follow its pose and visibility).
Result<std::uint32_t> append_model(SceneBuilder& builder, const Model& model, std::string_view name, bool animated_visibility = false, std::span<const ModelAttachment> attachments = {});

// Decodes and appends a model referenced by a map (placed object, backdrop, or spawn type), with its
// attachments. Particle ALOs and malformed models are reported once as scene warnings and yield
// nothing.
std::optional<std::uint32_t> append_scene_model(SceneBuilder& builder, const std::string& model_path, std::span<const ModelAttachment> attachments = {});

// The models a type's hardpoints attach to `host` (XML HardPoints entries with Model_To_Attach and
// an Attachment_Bone the host has), in list order; problems become scene warnings.
std::vector<ModelAttachment> type_hardpoint_models(SceneBuilder& builder, const GameObjectCatalog& catalog, const XmlDefinition& type, const Model* host);

// Key of a mesh built from `model_path` with `attachments` (types sharing a model but not their
// hardpoints get meshes of their own).
std::string attached_mesh_key(const std::string& model_path, std::span<const ModelAttachment> attachments);
// What adding a spawnable type needs: the builder of the scene its meshes join, the XML catalog and
// movement classes, the model context (land or space), the meshes already appended by model path,
// a combat profile source (one per type), and the spawnable list to extend.
struct SpawnableBuild
{
    SceneBuilder& builder;
    const GameObjectCatalog& catalog;
    const std::unordered_map<std::string, std::uint32_t>& movement_classes;
    GameModelContext context{};
    bool land{};
    std::unordered_map<std::string, std::optional<std::uint32_t>>& meshes;
    std::function<const CombatProfile&(const XmlDefinition&, const Model*)> profile_of;
    std::vector<SpawnableType>& spawnable;
    // GameConstants' Object_Max_Speed_Multiplier for the battle's mode (CombatContent).
    float speed_multiplier{1.0F};
};

// Adds an XML type (by name, case-insensitive) to build.spawnable with its model appended to the
// scene, or, for a company or squadron, its members and then itself; returns its index (an
// existing entry is reused) or nullopt with a warning when it has no model for the context.
std::optional<std::size_t> add_spawnable_type(SpawnableBuild& build, const std::string& type_name, int depth);

// A rig per scene mesh (its model, inverse bind pose, role clips, and rest palette range), the
// concatenated rest palette, every instance's bone offset, and the scene's palette size.
void build_map_rigs(SceneBuilder& builder, std::vector<VisualRig>& rigs, std::vector<Matrix4>& rest_palette);

// Expands the scene bounds by every instance's transformed mesh bounds and the terrain.
void update_scene_bounds(VisualScene& scene);

// Separation footprint of a mesh placed with `transform` (row-vector): a quarter of the summed X and
// Y extents of its bounds, scaled by the transform's horizontal scale. An engine choice until
// collision meshes and UnitCollisionClass are interpreted.
float mesh_footprint_radius(const VisualScene& scene, std::uint32_t mesh, const Matrix4& transform);

// The ground rectangle of a mesh placed with `transform`: its bounds' X/Y extent, rotated and scaled
// with the placement, with the given passability type. An engine choice until collision meshes
// are read.
GroundFootprint mesh_ground_footprint(const VisualScene& scene, std::uint32_t mesh, const Matrix4& transform, std::uint32_t passability_type);

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

// Tracks become unlit, alpha-blended ribbons draped over the terrain: the centerline is the
// control-point polyline (sampled at least every terrain cell), each point's width spans the
// ribbon, and opacity follows the start/middle/end keys. Texture V repeats once per average
// width along the track; the header's scroll values animate it.
std::size_t append_tracks(SceneBuilder& builder, const TedMapData& map);

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

// XML LOD_Bias: how much farther the type keeps its detail (1 when missing or not positive).
float type_lod_bias(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// XML FormationOrder (default 2, the most common) and FormationSpacing (default 0.5, likewise).
std::uint32_t type_formation_order(const GameObjectCatalog& catalog, const XmlDefinition& definition);
float type_formation_spacing(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// XML Bank_Turn_Angle in radians (degrees in the file; fighters 40, capital ships 5), at most 90
// degrees; default_bank_limit when absent.
float type_bank_limit(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// XML FormationGrouping Solo (the other stock value is Standard, the default).
bool type_formation_solo(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// The passability type an immobile placed object stamps on the ground, or nothing when it does not
// block: its XML OverridePassability (walls: RebelWall, EmpireWall), otherwise Obstacle for
// structures (GroundStructure, SpecialStructure, SecondaryStructure) and for types whose
// UnitCollisionClass names an obstacle (High, Medium, or Low Obstacle). Build pads and other
// props do not block. A provisional reading of the stock data.
std::optional<std::uint32_t> type_obstacle_passability(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// The ground rectangle a type's XML gives when placed with `placement`: its Obstacle_Width/_Height
// rectangle (from the Obstacle_X/_Y_Offset corner), else its Custom_Hard_X/_YExtent half extents
// (around the _Offset center), read from its Obstacle_Proxy_Type when it names one; nothing when
// it sets neither (the mesh bounds stand in).
std::optional<GroundFootprint> type_ground_footprint(const GameObjectCatalog& catalog, const XmlDefinition& definition, const Matrix4& placement, std::uint32_t passability_type);

// The radius ships keep clear of an immobile space object: its XML Space_Obstacle_Radius when its
// behaviors include SPACE_OBSTACLE (docks, asteroids, wrecks, junk); 0 for asteroid fields,
// nebulae, and ion storms, which ships enter. World units, unscaled (an inference, as for hard
// extents); Space_Obstacle_Offset (0 0 0 in nearly all stock uses) is not read.
float type_space_obstacle_radius(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// XML Custom_Soft_Footprint_Radius (infantry 3.5), else Custom_Footprint_Radius (small space craft:
// 7 or 15; taken as their separation radius, an engine choice), or 0 when both are missing.
float type_soft_footprint_radius(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// A type's XML Scale_Factor (inherited through Variant_Of_Existing_Type), the size its model is drawn
// at; 1 when absent, and 1 with a warning when not a positive number.
float type_scale_factor(const GameObjectCatalog& catalog, const XmlDefinition& definition, std::vector<std::string>& warnings);
// `transform` (row-vector) with its model axes scaled by `scale`, so the model is drawn `scale` times
// its file size about its own origin.
Matrix4 scaled_model_transform(Matrix4 transform, float scale) noexcept;

// XML Max_Rate_Of_Turn in radians per second (provisional_turn_rate_scale); 0 turns instantly.
float type_turn_rate(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// How a type gets up to speed and turns: XML OverrideAcceleration and OverrideDeceleration (or
// Override_Acceleration and Override_Deceleration, the team containers' spelling)
// (provisional_acceleration_scale; absent changes speed at once) and Min_Speed_Fraction_For_Turn. On
// land it turns at Stopped_Rate_Of_Turn (or Max_Rate_Of_Turn) below that fraction and stops to turn
// when its way is more than Close_Enough_Angle_For_Move_Start (degrees; the AT-AT 30) or else
// ground_turn_in_place_angle off; in space it cannot turn below that
// fraction (default_space_turn_speed_fraction without one). The default profile for flyers.
MovementProfile type_movement(const GameObjectCatalog& catalog, const XmlDefinition& definition, bool land, bool flyer);
// Engine choices, not the original's: how far off its way a ground unit stops to turn (the
// owner's observation that land units do not turn while moving off; GameConstants'
// CloseEnoughAngleForMoveStart says 180, which would never stop them, so the two disagree), and the
// turn speed fraction of space ships that do not name one (capital ships).
inline constexpr float ground_turn_in_place_angle = 0.785398163F;
inline constexpr float default_space_turn_speed_fraction = 0.25F;

// The movement of the team `company` forms (its XML Create_Team_Type container): the container's
// Max_Speed (scaled like type_max_speed, times `speed_multiplier`), Max_Rate_Of_Turn, and
// acceleration where it sets them; empty without a team.
TeamMovement type_team_movement(const GameObjectCatalog& catalog, const XmlDefinition& company, float speed_multiplier);

// Minimum flight speed of fighters and bombers (SpaceBehavior names FIGHTER_LOCOMOTOR): XML
// Min_Speed on the Max_Speed scale, or half of `max_speed` without one; 0 for every other type.
float type_min_flight_speed(const GameObjectCatalog& catalog, const XmlDefinition& definition, float max_speed);

// XML Land_Model_Anim_Override_Name (or Space_Model_Anim_Override_Name), inherited through
// Variant_Of_Existing_Type: the model whose clips the type plays instead of its own (27 stock uses:
// civilians, slaves, and field commanders on RI_INFANTRY, EI_TROOPER, or NI_GMale_Urban_A, Ewok
// variants on NI_EWOK). Empty when the type plays its own model's clips.
std::string type_animation_source(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// Records a type's animation source for a model it appended (the first type wins).
void note_animation_source(SceneBuilder& builder, const std::string& model_path, const GameObjectCatalog& catalog, const XmlDefinition& definition);

// The corpse style of a type from its normal death clone (DeathStyle).
DeathStyle type_death_style(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// Whether a type is a death clone, never spawned on its own: another type names it in a
// Death_Clone entry, or it carries Specific_Death_Anim_Index or Death_Persistence_Duration.
bool type_is_death_clone(const GameObjectCatalog& catalog, const XmlDefinition& definition, const std::unordered_set<std::string>& clone_names);
// Folded names of every type some Death_Clone entry names.
std::unordered_set<std::string> death_clone_names(const XmlAssetDatabase& database, const GameObjectCatalog& catalog);

// Whether a type aims with its body: none of its behavior lists (LandBehavior, Behavior) names
// TURRET. Stock infantry list WALK_LOCOMOTOR or LAND_TEAM_INFANTRY_LOCOMOTOR, IDLE, TARGETING, and
// WEAPON without TURRET; vehicles (T2-B, AT-ST) add TURRET. Only meaningful for ground units.
bool type_aims_with_body(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// What scripts read of a type (XML Lua_Script and CategoryMask, through its variant chain).
ScriptTypeInfo type_script_info(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// XML Max_Speed in world units per second (the provisional scale in visual_scene_builder.hpp);
// unparsable values are 0 with a warning.
float type_max_speed(const GameObjectCatalog& catalog, const XmlDefinition& definition, std::vector<std::string>& warnings, float mode_multiplier = 1.0F);

} // namespace eaw::scene_build
