#pragma once

#include "assets/pose.hpp"
#include "assets/spawn_catalog.hpp"
#include "eaw/simulation.hpp"
#include "terrain_height.hpp"
#include "formats/ted_map.hpp"
#include "eaw/visual_scene.hpp"
#include "particle_effects.hpp"
#include "projectile_visuals.hpp"

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
    // A death by being run over (CRUSHED_nn clips; a Damage_Crush death clone's
    // Specific_Death_Anim_Type Crushed).
    crushed,
};

// The role a clip file name (without directory and extension) plays for `model_stem`, and its
// variant number; nullopt for other files and roles. Case-insensitive.
struct ClipName
{
    AnimationRole role{};
    std::uint32_t variant{};
};
[[nodiscard]] std::optional<ClipName> parse_clip_name(std::string_view file_stem, std::string_view model_stem);

// How a type's clips play (presentation only). Rates multiply the engine's own playback: the move
// clip by XML Movement_Animation_Speed, the walk clip by Walk_Animation_Speed, the idle clip by
// Idle_Anim_00_Rate_Mod (props: 8 spins the huge asteroid masses); a type without a move or walk
// clip keeps its idle clip while moving when Allow_Idle_When_Moving (birds) instead of standing in
// its rest pose.
struct AnimationProfile
{
    float move_rate{1.0F};
    float walk_rate{1.0F};
    float idle_rate{1.0F};
    bool idle_when_moving{};
    // A turret that turns toward the unit's target (XML Turret_Bone_Name, Barrel_Bone_Name: the
    // bones' names as game_object_name_crc, 0 none): the turret bone's subtree yaws about the
    // model's up axis, the barrel's pitches, at `turret_rate` radians per second (Turret_Rotate_Speed
    // per 30 Hz frame, a guess like Max_Rate_Of_Turn; 5 without it), within half of
    // Turret_Rotate_Extent_Degrees and Turret_Elevate_Extent_Degrees each way (a guess; pi: free).
    std::uint32_t turret_bone{};
    std::uint32_t barrel_bone{};
    float turret_rate{};
    float turret_yaw_limit{3.14159265F};
    float turret_pitch_limit{1.04719755F};
};

// A turn of a bone's subtree in model space about the bone's posed position (turret aiming).
struct BoneTurn
{
    std::uint32_t bone{};
    Vector3 axis;
    float radians{};
};

struct VisualClip
{
    AnimationRole role{};
    std::uint32_t variant{};
    std::string path;
    std::shared_ptr<const Animation> animation;
};

// A map mesh's skeleton for animation: the model, the inverse of its bind pose (empty when the bind
// pose cannot be inverted; the mesh then stays in its rest pose), where its rest palette starts in
// the scene palette, and its clips sorted by role and variant. A rest palette is the mesh's bind
// pose: identity skin matrices, then the visibility half with attachments of bones hidden in the
// bind pose collapsed (see sample_skin_palette).
// A particle effect attached to a model bone (an ALO proxy naming a particle system). The effect's
// texture indices point into the build's particle texture list.
struct VisualEffectAttachment
{
    std::shared_ptr<const ParticleEffect> effect;
    std::uint32_t bone{};
    std::string proxy;
    // A hardpoint's damage effect (the proxy hangs under the bone its XML Damage_Particles names):
    // that bone, shown once the hardpoint is damaged (not driven yet), else -1.
    std::int32_t damage_bone{-1};
    // The mesh emit-from-mesh emitters spawn on (the mesh on the proxy bone's parent, in the proxy
    // bone's space), when the effect has such emitters and the model has that mesh.
    std::shared_ptr<const ParticleEmitMesh> emit_mesh;
    // An engine effect (named pe_*, or under a hardpoint's Engine_Particles bone): it glows with the
    // unit's speed.
    bool engine{};
};

struct VisualRig
{
    std::shared_ptr<const Skeleton> skeleton;
    std::vector<Matrix4> inverse_bind_pose;
    std::uint32_t rest_offset{};
    // Palette entries the mesh uses (its rest palette's size).
    std::uint32_t palette_size{};
    std::vector<VisualClip> clips;
    // Particle effects on the model's visible proxies.
    std::vector<VisualEffectAttachment> effects;
    // Per bone: hidden while the entity is intact (a hardpoint's Damage_Decal bone and its children,
    // whose scorch meshes appear once that hardpoint is destroyed). Applied to the palette's
    // visibility half; empty when nothing is hidden.
    std::vector<bool> intact_hidden;
};

struct VisualModelAsset
{
    VisualScene scene;
    Skeleton skeleton;
    std::vector<Matrix4> inverse_bind_pose;
    // The scene's palette has a second, visibility-collapsed half for rigid attachments (see
    // sample_skin_palette).
    bool visibility_palette{};
};

// A stock or mod map's placed objects as one instanced scene (no terrain yet). Counts describe
// placed objects that could not be drawn and why.
// A placed map object as a simulation entity: its spawn data and the scene instance that showed it
// at load (mesh, original transform, colors), which presentation re-poses from snapshots.
// How a unit's corpse looks, from the death clone it leaves under normal damage (XML Death_Clone
// "Damage_Normal, <type>", else its first entry): the clone's Specific_Death_Anim_Index picks the
// DIE variant and its Death_Persistence_Duration how long the wreck stays, in seconds (the AT-ST's
// stays 30 s). Without a clone a corpse plays a DIE variant chosen by its id and stays 4 s after it.
// A clip a death plays: its role and variant.
struct DeathClip
{
    AnimationRole role{AnimationRole::die};
    std::uint32_t variant{};
};

struct DeathStyle
{
    std::optional<std::uint32_t> die_variant;
    // The clip of a death by crushing: the Damage_Crush death clone's Specific_Death_Anim_Type
    // (Crushed, or DIE) and Specific_Death_Anim_Index.
    std::optional<DeathClip> crushed;
    std::optional<float> persistence_seconds;
    // Leaves no corpse: removed the moment it dies (XML Remove_Upon_Death: fighters, debris,
    // satellites, most props).
    bool removed{};
};

// Units an object's destruction leaves (XML Destruction_Survivors: crews, prisoners): each listed
// type, `count` groups of it, owned by the destroyed object's owner. Every stock entry has weight
// 1.0, so each spawns; a count range (Minimum_/Maximum_Destruction_Survivor_Count, one prison)
// spawns its minimum. Both are engine choices.
struct Survivors
{
    std::vector<std::string> types;
    std::uint32_t count{1};
};

// What scripts read of an object type beyond its name: its game-object script (XML Lua_Script;
// empty: none), its categories (XML CategoryMask, folded: "infantry", "landhero"), and its combat
// rating (XML AI_Combat_Power, which Get_Combat_Rating presumably reads; 0 when absent).
struct ScriptTypeInfo
{
    std::string lua_script;
    std::vector<std::string> categories;
    float combat_rating{};
};

// The most a flyer banks in a turn when its type names no Bank_Turn_Angle: 60 degrees (an engine
// choice).
inline constexpr float default_bank_limit = 1.04719755F;

struct MapEntitySpawn
{
    EntitySpawn spawn;
    std::uint32_t instance{};
    DeathStyle death;
    ScriptTypeInfo script;
    // The name scripts find the placed object by (TedPlacedObject::hint; company members share
    // their company's).
    std::string hint;
    // Presentation: the most a flyer banks in a turn (type_bank_limit, radians).
    float bank_limit{default_bank_limit};
    Survivors survivors;
    // Not drawn: an editor marker (XML Is_Marker: flags, script points, skirmish base positions)
    // that stays an entity so scripts can find it.
    bool hidden{};
    AnimationProfile animation;
};

// XML Max_Speed values are small (infantry 0.8, AT-AT 0.5, TIE fighter 4.5), consistent with world
// units per logic frame. The logic rate is unmeasured; until it is, speeds are scaled as if per 1/30 s.
// This is a provisional engine assumption, not compatibility evidence.
// Alamo models face -Y: every stock muzzle and fire bone sits at negative Y and points along -Y
// (T2B, AT-ST, X-wing, Star Destroyer; see docs/compatibility-research.md). An entity's heading is
// the direction of its model's forward axis about +Z, so an unrotated model has this heading.
inline constexpr float identity_model_heading = -1.57079632679489662F;

// The heading of a row-vector model transform: the direction of its forward (-Y) axis about +Z.
[[nodiscard]] float model_forward_heading(const Matrix4& transform) noexcept;

inline constexpr float provisional_max_speed_scale = 30.0F;
// XML Max_Rate_Of_Turn read as degrees per 30 Hz frame, like Max_Speed: infantry 16 (480 degrees per
// second), tanks 12-20, capital ships 0.4-1.5 (12-45 degrees per second). Provisional.
inline constexpr float provisional_turn_rate_scale = 30.0F * 3.14159265358979F / 180.0F;
// A turret's turn rate without Turret_Rotate_Speed (degrees per frame; the stock values run 2 to
// 10; an engine choice).
inline constexpr float default_turret_rotate_speed = 5.0F;
// XML OverrideAcceleration/OverrideDeceleration read as Max_Speed units per 30 Hz frame per frame
// (Calamari cruiser .02: full speed in 2.5 seconds; T2B .2: in 0.4 seconds). A guess.
inline constexpr float provisional_acceleration_scale = provisional_max_speed_scale * 30.0F;

// How a company's team moves its members. A company forms teams of its Create_Team_Type container
// (Stormtrooper_Team for Imperial_Stormtrooper_Squad); in the original the container moves and its
// members follow it, so the container's Max_Speed (.8 against the member's 1), Max_Rate_Of_Turn,
// and acceleration are the members' (data; the engine has no team entity, so each member takes
// them). Unset fields leave the member's own. Space squadrons are not teams in this sense: their
// fighters fly on their own.
// How a company splits into teams (XML Max_Squad_Size; teams of 1 or less are no teams) and how
// a team stands (its Create_Team_Type container's ContainerArrangement and FormationSpacing).
struct TeamLayout
{
    std::uint32_t size{};
    TeamArrangement arrangement{TeamArrangement::dartboard};
    float spacing{0.5F};
};

// Gives `members` (a company's, in member order) their team ids, from `next_team` on, `layout.size`
// per team, with each member's raggedness; advances `next_team` past them.
inline void assign_teams(std::span<FormationMember> members, std::span<const float> raggedness, const TeamLayout& layout, std::uint32_t& next_team) noexcept
{
    if (layout.size <= 1U)
    {
        return;
    }
    for (std::size_t index = 0; index < members.size(); ++index)
    {
        members[index].team = {next_team + static_cast<std::uint32_t>(index / layout.size), layout.arrangement, layout.spacing, index < raggedness.size() ? raggedness[index] : 0.0F};
    }
    next_team += static_cast<std::uint32_t>((members.size() + layout.size - 1U) / layout.size);
}

struct TeamMovement
{
    std::optional<float> max_speed;
    std::optional<float> turn_rate;
    std::optional<float> acceleration;
    std::optional<float> deceleration;
};

inline void apply_team_movement(const TeamMovement& team, EntitySpawn& spawn) noexcept
{
    spawn.max_speed = team.max_speed.value_or(spawn.max_speed);
    spawn.turn_rate = team.turn_rate.value_or(spawn.turn_rate);
    spawn.movement.acceleration = team.acceleration.value_or(spawn.movement.acceleration);
    spawn.movement.deceleration = team.deceleration.value_or(spawn.movement.deceleration);
}

// An XML object type that can be spawned at runtime: its model is part of the map scene's meshes.
struct SpawnableType
{
    std::string type_name;
    std::uint32_t mesh{};
    float max_speed{};
    std::array<float, 4> team_color{1.0F, 1.0F, 1.0F, 1.0F};
    // XML LOD_Bias (VisualInstance::lod_bias).
    float lod_bias{1.0F};
    // Walks on the ground on land maps (Space_Layer Land or None); flyers keep a height instead.
    bool follows_ground{};
    // XML Hover_Offset: height above the surface for flyers (air whales 250, snowspeeders 50).
    float hover_offset{};
    // XML Scale_Factor: the size its model is drawn at (and its model-derived footprint and
    // hardpoint positions).
    float scale{1.0F};
    // Passability types its MovementClass may enter (MovementClassTypeDefs.xml).
    std::uint32_t passable_types{};
    // Health, shields, armor, and weapon from XML (combat_content.hpp).
    CombatProfile combat;
    // Separation footprint (from the model bounds) and turn rate (XML Max_Rate_Of_Turn); see
    // EntitySpawn.
    float radius{};
    float turn_rate{};
    // Acceleration and turning (type_movement).
    MovementProfile movement;
    // Fighters' minimum flight speed (EntitySpawn::min_speed); 0 for other types.
    float min_speed{};
    // Aims with its body (no TURRET behavior); applies when it walks on the ground.
    bool aims_with_body{};
    DeathStyle death;
    // Presentation: the most it banks in a turn when it flies (type_bank_limit, radians), and how
    // its clips play.
    float bank_limit{default_bank_limit};
    AnimationProfile animation;
    // XML FormationOrder and FormationSpacing (formation.hpp).
    std::uint32_t formation_order{2};
    float formation_spacing{0.5F};
    bool formation_solo{};
    CrushProfile crush;
    Survivors survivors;
    ScriptTypeInfo script;
    // A company (Company_Units): the spawnable types of its members, in list order, spawned
    // together in formation. Empty for ordinary types; a company's own mesh is unused.
    std::vector<std::size_t> members;
    // A company's team movement, applied to its members, and how it splits into teams.
    TeamMovement team;
    TeamLayout team_layout;
    // XML FormationRaggedness (FormationTeam::raggedness) when it stands in a team.
    float formation_raggedness{};
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
    // The next company team id (FormationTeam::id; battles continue from it).
    std::uint32_t next_team{1};
    // Immobile obstacles whose footprints the simulation stamps into the ground.
    std::size_t obstacle_footprints{};
    // Immobile space objects ships steer around (their Space_Obstacle_Radius).
    std::size_t space_obstacles{};
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
    std::vector<Matrix4> rest_palette;
    // Textures of the rigs' and named effects' particle effects, as in SpawnablePackage.
    std::vector<VisualTexture> particle_textures;
    // The effects MapBuildOptions::effects named, in order (null where one could not be loaded).
    std::vector<std::shared_ptr<const ParticleEffect>> effects;
    // How shots look, as in SpawnablePackage.
    ProjectileVisualTable projectile_visuals;
    std::uint32_t projectile_atlas{invalid_visual_texture};
    // The Particle objects shot impacts and deaths name, as in SpawnablePackage.
    ParticleEffectObjectTable effect_objects;
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
    // Particle effects to load by name (model references, as proxies name them) into
    // VisualMapAsset::effects, for viewing (sandbox --effect).
    std::vector<std::string> effects;
    // How particle effects are interpreted (docs/architecture.md, "Content contract versus legacy
    // implementation").
    ParticleFidelity particle_fidelity{ParticleFidelity::legacy};
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
    std::vector<Matrix4> rest_palette;
    std::vector<SpawnableType> types;
    // Textures of the rigs' particle effects (register with D3d12Renderer::add_textures and renumber
    // the effects with renumber_effect_textures).
    std::vector<VisualTexture> particle_textures;
    // How the types' shots look, and the bolt atlas's index in particle_textures (or
    // invalid_visual_texture).
    ProjectileVisualTable projectile_visuals;
    std::uint32_t projectile_atlas{invalid_visual_texture};
    // The XML Particle objects the types' shot impacts and deaths name (effects index
    // particle_textures).
    ParticleEffectObjectTable effect_objects;
};
// Builds a type for a land or space map through the same cached XML catalog, movement classes, and
// combat data as build_visual_map (an error when the type has no model for that context).
[[nodiscard]] Result<SpawnablePackage> build_spawnable_package(const AssetReader& reader, std::string_view type_name, bool land, AssetCache* cache = nullptr, const MapBuildOptions& options = {});
// The unit, structure, and prop types with a model for land or space maps (no death clones,
// markers, effects, or backdrops), sorted case-insensitively (through the cached XML catalog).
[[nodiscard]] std::vector<SpawnTypeInfo> spawnable_types(const AssetReader& reader, bool land, AssetCache* cache = nullptr);
// Points the rigs' effects at renderer texture indices: `renderer_textures[i]` is where the
// renderer placed particle texture i (D3d12Renderer::add_textures). Effects shared between rigs stay
// shared.
void renumber_effect_textures(std::span<VisualRig> rigs, std::span<std::shared_ptr<const ParticleEffect>> effects, std::span<const std::uint32_t> renderer_textures,
                              ParticleEffectObjectTable* objects = nullptr, ProjectileVisualTable* projectiles = nullptr);
[[nodiscard]] std::optional<std::string> default_model_animation(const AssetReader& reader, std::string_view model_path);
// Every <MODEL>_*.ALA beside the model, sorted by virtual path.
[[nodiscard]] std::vector<std::string> model_animations(const AssetReader& reader, std::string_view model_path);
// Skin matrices for the model's bones (bind pose without an animation). With a visibility palette,
// a second half repeats them, collapsed to a point for bones the pose hides.
[[nodiscard]] std::vector<Matrix4> sample_skin_palette(const VisualModelAsset& asset, const Animation* animation, float seconds);
// Appends a rig's palette at `seconds` of `animation` (rig.palette_size entries) to `out`. Without
// an inverse bind pose it appends the rest pose.
void append_rig_palette(const VisualRig& rig, const Animation& animation, float seconds, std::vector<Matrix4>& out);
// Appends a rig's palette for a local pose (a blend of clips; see blend_pose), its bones turned by
// `turns` in order (each bone's subtree about the bone's posed position).
void append_rig_local_palette(const VisualRig& rig, std::span<const LocalBone> pose, std::vector<Matrix4>& out, std::span<const BoneTurn> turns = {});

} // namespace eaw
