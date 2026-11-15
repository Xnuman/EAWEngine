#pragma once

// An XML type's recipe (docs/ecs-design.md, 6.5 and migration step 5): what a type gives every
// entity made from it, read from its XML and its model once, and the one function that makes an
// entity of it from per-instance values. Runtime spawns (Battle::spawn_of) and placed map objects
// (build_battle_map) are both made this way, so a type behaves the same however it appears.

#include "math/matrix.hpp"
#include "math/vector.hpp"
#include "assets/model_geometry.hpp"
#include "simulation/combat.hpp"
#include "simulation/simulation.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eaw {

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

// A type's XML obstacle rectangle in its own frame (type_obstacle_shape): half extents along its x
// (forward) and y, and its center.
struct ObstacleShape
{
    float half_x{};
    float half_y{};
    float center_x{};
    float center_y{};
};

struct TypeRecipe
{
    std::string type_name;
    float max_speed{};
    // Walks on the ground on land maps (Space_Layer Land or None); flyers keep a height instead.
    bool follows_ground{};
    // XML Hover_Offset: height above the surface for flyers (air whales 250, snowspeeders 50).
    float hover_offset{};
    // XML Layer_Z_Adjust: the height a unit spawned in space takes, the space plane being 0
    // (recorded in the FoC debug build: units staged at 0 sit at their type's value, a corvette -20,
    // a Nebulon-B -90, an MC80 -290; EaW-Revised S-10 to S-14, S-32 to S-34).
    float layer_z_adjust{};
    // Passability types its MovementClass may enter (MovementClassTypeDefs.xml).
    std::uint32_t passable_types{};
    // Health, shields, armor, and weapon from XML (combat_content.hpp).
    CombatProfile combat;
    // Separation footprint of a mobile type unplaced (its soft radius, else its model's bounds
    // through its Scale_Factor) and turn rate (XML Max_Rate_Of_Turn); see EntitySpawn.
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
    // XML FormationRaggedness (FormationTeam::raggedness) when it stands in a team.
    float formation_raggedness{};
    CrushProfile crush;
    Survivors survivors;
    ScriptTypeInfo script;
    // What a placement needs to size the type: its soft footprint radius (0 when it has none), its
    // model's drawn bounds, and its Scale_Factor.
    float soft_radius{};
    ModelBounds bounds;
    float scale{1.0F};
    // An immobile type that blocks the ground on land maps: the passability it stamps, and its XML
    // rectangle (the model bounds stand in without one).
    std::optional<std::uint32_t> obstacle_passability;
    std::optional<ObstacleShape> obstacle_shape;
    // The radius ships keep clear of an immobile type in space (its space obstacle radius, else its
    // soft radius; 0 when ships may enter it).
    float space_obstacle_radius{};
};

// What differs between entities of one recipe: where and how one is placed, its owner, its look,
// and whether the battle has a ground it can follow. `placement` is a placed object's transform
// (its matrix sizes and orients it); runtime spawns have none.
struct SpawnInstance
{
    Vector3 position;
    float heading{};
    std::uint32_t owner{};
    std::uint32_t visual{};
    bool ground_valid{};
    std::optional<Matrix4> placement;
};

// An entity of `recipe` made with `instance`'s values: the one construction path of entities from
// XML types.
[[nodiscard]] EntitySpawn make_spawn(const TypeRecipe& recipe, const SpawnInstance& instance);

// The transform of an object of `scale` placed at `position` facing `heading` (radians about +Z,
// as model_forward_heading reads it).
[[nodiscard]] Matrix4 placement_transform(const Vector3& position, float heading, float scale) noexcept;

} // namespace eaw
