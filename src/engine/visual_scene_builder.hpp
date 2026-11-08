#pragma once

#include "eaw/ala_animation.hpp"
#include "eaw/simulation.hpp"
#include "terrain_height.hpp"
#include "eaw/alo_model.hpp"
#include "eaw/ted_map.hpp"
#include "eaw/visual_scene.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

class AssetReader;
class AssetCache;

// What a unit is doing, as far as choosing its animation goes. Clips are found by the stock naming
// convention <MODEL>_<ROLE>_<NN>.ALA beside the model (IDLE 539 stock files, MOVE 158, WALKMOVE 53,
// ATTACK 160, ATTACKIDLE 108, DIE 301); NN numbers variants. Other roles (TURNL, CROUCHIDLE,
// FW_DIE, ...) are not used yet.
enum class AnimationRole : std::uint8_t
{
    idle,
    move,
    walk_move,
    attack,
    attack_idle,
    die,
};

// The role a clip file name (without directory and extension) plays for `model_stem`, and its
// variant number; nullopt for other files and roles. Case-insensitive.
struct ClipName
{
    AnimationRole role{};
    std::uint32_t variant{};
};
[[nodiscard]] std::optional<ClipName> parse_clip_name(std::string_view file_stem, std::string_view model_stem);

struct VisualClip
{
    AnimationRole role{};
    std::uint32_t variant{};
    std::string path;
    std::shared_ptr<const AlaAnimation> animation;
};

// A map mesh's skeleton for animation: the model, the inverse of its bind pose (empty when the bind
// pose cannot be inverted; the mesh then stays in its rest pose), where its rest palette starts in
// the scene palette, and its clips sorted by role and variant. A rest palette is the mesh's bind
// pose: identity skin matrices, then the visibility half with attachments of bones hidden in the
// bind pose collapsed (see sample_skin_palette).
struct VisualRig
{
    std::shared_ptr<const AloModel> model;
    std::vector<AloMatrix4> inverse_bind_pose;
    std::uint32_t rest_offset{};
    // Palette entries the mesh uses (its rest palette's size).
    std::uint32_t palette_size{};
    std::vector<VisualClip> clips;
};

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
// Alamo models face -Y: every stock muzzle and fire bone sits at negative Y and points along -Y
// (T2B, AT-ST, X-wing, Star Destroyer; see docs/compatibility-research.md). An entity's heading is
// the direction of its model's forward axis about +Z, so an unrotated model has this heading.
inline constexpr float identity_model_heading = -1.57079632679489662F;

// The heading of a row-vector model transform: the direction of its forward (-Y) axis about +Z.
[[nodiscard]] float model_forward_heading(const std::array<float, 16>& transform) noexcept;

inline constexpr float provisional_max_speed_scale = 30.0F;
// XML Max_Rate_Of_Turn read as degrees per 30 Hz frame, like Max_Speed: infantry 16 (480 degrees per
// second), tanks 12-20, capital ships 0.4-1.5 (12-45 degrees per second). Provisional.
inline constexpr float provisional_turn_rate_scale = 30.0F * 3.14159265358979F / 180.0F;

// An XML object type that can be spawned at runtime: its model is part of the map scene's meshes.
struct SpawnableType
{
    std::string type_name;
    std::uint32_t mesh{};
    float max_speed{};
    std::array<float, 4> team_color{1.0F, 1.0F, 1.0F, 1.0F};
    // Walks on the ground on land maps (Space_Layer Land or None); flyers keep a height instead.
    bool follows_ground{};
    // XML Hover_Offset: height above the surface for flyers (air whales 250, snowspeeders 50).
    float hover_offset{};
    // Passability types its MovementClass may enter (MovementClassTypeDefs.xml).
    std::uint32_t passable_types{};
    // Health, shields, armor, and weapon from XML (combat_content.hpp).
    CombatProfile combat;
    // Separation footprint (from the model bounds) and turn rate (XML Max_Rate_Of_Turn); see
    // EntitySpawn.
    float radius{};
    float turn_rate{};
    // Fighters' minimum flight speed (EntitySpawn::min_speed); 0 for other types.
    float min_speed{};
    // Aims with its body (no TURRET behavior); applies when it walks on the ground.
    bool aims_with_body{};
    // XML FormationOrder and FormationSpacing (formation.hpp).
    std::uint32_t formation_order{2};
    float formation_spacing{0.5F};
    // A company (Company_Units): the spawnable types of its members, in list order, spawned
    // together in formation. Empty for ordinary types; a company's own mesh is unused.
    std::vector<std::size_t> members;
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
    // Land companies arranged in formation, and their members.
    std::size_t expanded_companies{};
    // Immobile obstacles whose footprints the simulation stamps into the ground.
    std::size_t obstacle_footprints{};
    std::size_t company_members{};
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
    // Ground for the simulation and CPU queries (land maps; invalid for space).
    GroundMap ground;
    // Damage multipliers from GameConstants.xml, with ids matching the entities' combat profiles.
    DamageTable damage_table;
    // One rig per scene mesh (same index), and the scene palette of every mesh's rest pose; each
    // instance's bone_offset is its mesh's rest offset. Upload it with the scene (the renderer
    // resets its palette to identity on load).
    std::vector<VisualRig> rigs;
    std::vector<std::array<float, 16>> rest_palette;
};

// Builders read through `reader` (pass a RecordingAssetReader to collect the scene's file
// dependencies). With a cache, decoded models, textures, maps, and the XML object catalog are
// reused across builds until their source files change. A model or map that cannot be built (not
// found, malformed, or without renderable geometry) returns an error naming its virtual path;
// problems that only drop parts of a scene are listed in its warnings instead.
[[nodiscard]] Result<VisualModelAsset> build_visual_model(const AssetReader& reader, std::string_view model_path, AssetCache* cache = nullptr);
// `spawn_types` names XML object types whose models are added to the scene's meshes so they can be
// spawned at runtime; types without a model for the map's context are reported as warnings.
// What a map build decodes besides geometry and game data. Headless battles need neither textures
// nor animation clips; the scene then has no textures and its rigs no clips.
struct MapBuildOptions
{
    bool textures{true};
    bool animation_clips{true};
};
[[nodiscard]] Result<VisualMapAsset> build_visual_map(const AssetReader& reader, std::string_view map_path, AssetCache* cache = nullptr, std::span<const std::string> spawn_types = {},
                                                      const MapBuildOptions& options = {});
// A spawnable type built on its own, to add to a loaded map at runtime: its meshes as a fragment for
// D3d12Renderer::add_meshes (no instances or terrain), their rigs and rest palette (rest offsets
// relative to it), and the types: the requested one last, after any company or squadron members,
// with mesh indices relative to the fragment and member indices relative to `types`.
struct SpawnablePackage
{
    VisualScene meshes;
    std::vector<VisualRig> rigs;
    std::vector<std::array<float, 16>> rest_palette;
    std::vector<SpawnableType> types;
};
// Builds a type for a land or space map through the same cached XML catalog, movement classes, and
// combat data as build_visual_map (an error when the type has no model for that context).
[[nodiscard]] Result<SpawnablePackage> build_spawnable_package(const AssetReader& reader, std::string_view type_name, bool land, AssetCache* cache = nullptr, const MapBuildOptions& options = {});
// Names of the unit and structure types with a model for land or space maps, sorted
// case-insensitively (through the cached XML catalog).
[[nodiscard]] std::vector<std::string> spawnable_type_names(const AssetReader& reader, bool land, AssetCache* cache = nullptr);
[[nodiscard]] std::optional<std::string> default_model_animation(const AssetReader& reader, std::string_view model_path);
// Every <MODEL>_*.ALA beside the model, sorted by virtual path.
[[nodiscard]] std::vector<std::string> model_animations(const AssetReader& reader, std::string_view model_path);
// Skin matrices for the model's bones (bind pose without an animation). With a visibility palette,
// a second half repeats them, collapsed to a point for bones the pose hides.
[[nodiscard]] std::vector<std::array<float, 16>> sample_skin_palette(const VisualModelAsset& asset, const AlaAnimation* animation, float seconds);
// Appends a rig's palette at `seconds` of `animation` (rig.palette_size entries) to `out`. Without
// an inverse bind pose it appends the rest pose.
void append_rig_palette(const VisualRig& rig, const AlaAnimation& animation, float seconds, std::vector<std::array<float, 16>>& out);
// Appends a rig's palette for a local pose (a blend of clips; see ala_blend_pose).
void append_rig_local_palette(const VisualRig& rig, std::span<const AlaLocalBone> pose, std::vector<std::array<float, 16>>& out);

} // namespace eaw
