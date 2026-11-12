#pragma once

// What a battle knows of a map: gameplay data only, apart from how anything is drawn (the scene,
// meshes, and each entity's and type's look stay in visual_scene_builder.hpp). Built from the map,
// the XML types, and the models' skeletons and geometry; never from drawing.

#include "core/status.hpp"
#include "assets/model_geometry.hpp"
#include "assets/spawn_catalog.hpp"
#include "simulation/combat.hpp"
#include "simulation/formation.hpp"
#include "simulation/ground_map.hpp"
#include "simulation/simulation.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

class AssetCache;
class AssetReader;

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

// A placed object as the battle sees it: what it spawns as, what its scripts read, the name
// scripts find it by (TedPlacedObject::hint; company members share their company's), and what its
// destruction leaves.
struct MapEntity
{
    EntitySpawn spawn;
    ScriptTypeInfo script;
    std::string hint;
    Survivors survivors;
};

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

// How a company's team moves its members. A company forms teams of its Create_Team_Type container
// (Stormtrooper_Team for Imperial_Stormtrooper_Squad); in the original the container moves and its
// members follow it, so the container's Max_Speed (.8 against the member's 1), Max_Rate_Of_Turn,
// and acceleration are the members' (data; the engine has no team entity, so each member takes
// them). Unset fields leave the member's own. Space squadrons are not teams in this sense: their
// fighters fly on their own.
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

// An XML object type that can be spawned at runtime, as the battle sees it (its look is a
// SpawnableLook at the same index).
struct SpawnableType
{
    std::string type_name;
    float max_speed{};
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
    // Acceleration and turning (type_movement).
    MovementProfile movement;
    // Fighters' minimum flight speed (EntitySpawn::min_speed); 0 for other types.
    float min_speed{};
    // Aims with its body (no TURRET behavior); applies when it walks on the ground.
    bool aims_with_body{};
    // XML FormationOrder and FormationSpacing (formation.hpp).
    std::uint32_t formation_order{2};
    float formation_spacing{0.5F};
    bool formation_solo{};
    CrushProfile crush;
    Survivors survivors;
    ScriptTypeInfo script;
    // A company (Company_Units): the spawnable types of its members, in list order, spawned
    // together in formation. Empty for ordinary types.
    std::vector<std::size_t> members;
    // A company's team movement, applied to its members, and how it splits into teams.
    TeamMovement team;
    TeamLayout team_layout;
    // XML FormationRaggedness (FormationTeam::raggedness) when it stands in a team.
    float formation_raggedness{};
};

// A map built for a battle (build_battle_map): what BattleMap views, and what the build counted.
// Built without a scene; a drawn map (VisualMapAsset) holds one beside its look.
struct BattleMapAsset
{
    // The map's virtual path.
    std::string name;
    bool land{};
    // Placements the object layout could not use: without an XML type, without a model for the
    // map's mode, or naming a missing model file (MapObjectLayout).
    std::size_t untyped{};
    std::size_t without_model{};
    std::size_t missing_model_files{};
    std::size_t expanded_members{};
    std::size_t unexpanded_containers{};
    std::size_t company_members{};
    // Placements whose model has no drawable geometry, so they are no entities (model_geometry).
    std::size_t placements_without_geometry{};
    // Land companies arranged in formation.
    std::size_t expanded_companies{};
    // The next company team id (FormationTeam::id; battles continue from it).
    std::uint32_t next_team{1};
    // Immobile obstacles whose footprints the simulation stamps into the ground.
    std::size_t obstacle_footprints{};
    // Immobile space objects ships steer around (their Space_Obstacle_Radius).
    std::size_t space_obstacles{};
    // One entity per placed object whose model has drawable geometry (not backdrops), in
    // placement order.
    std::vector<MapEntity> entities;
    // Types requested for runtime spawning (and what destruction leaves) that resolved to a model,
    // in request order.
    std::vector<SpawnableType> spawnable;
    // Where the placed entities' models reach (their drawn bounds as placed): the frame of a map
    // without ground.
    ModelBounds extent;
    // Ground for the simulation and CPU queries (land maps; invalid for space).
    GroundMap ground;
    // Damage multipliers from GameConstants.xml, with ids matching the entities' combat profiles.
    DamageTable damage_table;
    std::vector<std::string> warnings;
};

// Builds the battle side of a map from the TED, the XML types, and the models' skeletons and
// geometry (through `cache` when given): no textures, meshes, or effects. `spawn_types` are the
// XML types spawn orders may name; what their destruction leaves joins them.
[[nodiscard]] Result<BattleMapAsset> build_battle_map(const AssetReader& reader, std::string_view map_path, AssetCache* cache = nullptr, std::span<const std::string> spawn_types = {});

// The unit, structure, and prop types with a model for land or space maps (no death clones,
// markers, effects, or backdrops), sorted case-insensitively (through the cached XML catalog).
[[nodiscard]] std::vector<SpawnTypeInfo> spawnable_types(const AssetReader& reader, bool land, AssetCache* cache = nullptr);

// What a battle needs from a built map (a BattleMapAsset; tests build it directly).
struct BattleMap
{
    // Placed entities, spawned in order when the battle starts.
    std::span<const MapEntity> entities;
    // Types that spawn orders may name, by XML type name.
    std::span<const SpawnableType> spawnable;
    // Ground of land maps (none on space maps).
    const GroundMap* ground{};
    DamageTable damage_table;
    Hostility hostility;
};

// The map's placed entities, spawnable types, ground, and damage table, with the provisional
// owner-slot hostility (combat_content.hpp).
[[nodiscard]] BattleMap battle_map(const BattleMapAsset& asset);

} // namespace eaw
