#pragma once

// How a battle's entities and spawnable types are drawn, beside what the battle knows of them
// (battle_map.hpp): their mesh or scene instance, team color, scale, death, bank, and clips.

#include "math/vector.hpp"
#include "render/color.hpp"
#include "presentation/visual_rig.hpp"

#include <cstdint>
#include <optional>

namespace eaw {

// A clip a death plays: its role and variant.
struct DeathClip
{
    AnimationRole role{AnimationRole::die};
    std::uint32_t variant{};
};

// How a unit's corpse looks, from the death clone it leaves under normal damage (XML Death_Clone
// "Damage_Normal, <type>", else its first entry): the clone's Specific_Death_Anim_Index picks the
// DIE variant and its Death_Persistence_Duration how long the wreck stays, in seconds (the AT-ST's
// stays 30 s). Without a clone a corpse plays a DIE variant chosen by its id and stays 4 s after it.
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

// The most a flyer banks in a turn when its type names no Bank_Turn_Angle: 60 degrees (an engine
// choice).
inline constexpr float default_bank_limit = 1.04719755F;

// How a placed object (a MapEntity, same index) is drawn: its scene instance, its death, the most a
// flyer banks in a turn (type_bank_limit, radians), whether it is hidden (an editor marker, XML
// Is_Marker: flags, script points, skirmish base positions, which stays an entity so scripts can
// find it, or an entity whose model could not be added to the scene; `instance` is then unused), and
// how its clips play.
struct MapEntityLook
{
    std::uint32_t instance{};
    DeathStyle death;
    float bank_limit{default_bank_limit};
    bool hidden{};
    AnimationProfile animation;
};

// How a spawnable type (a SpawnableType, same index) is drawn: its mesh in the map scene, its team
// color, XML LOD_Bias (VisualInstance::lod_bias), XML Scale_Factor (the size its model is drawn
// at; its model-derived footprint and hardpoint positions use it too), its death, the most it
// banks in a turn when it flies (type_bank_limit, radians), how its clips play, and whether it is
// hidden (its model could not be added to the scene; `mesh` is then unused). A company's mesh is its
// first member's (unused: companies spawn as their members).
struct SpawnableLook
{
    std::uint32_t mesh{};
    bool hidden{};
    Color team_color{white};
    float lod_bias{1.0F};
    float scale{1.0F};
    DeathStyle death;
    float bank_limit{default_bank_limit};
    AnimationProfile animation;
};

// A decoration (XML Is_Decoration): a placed object that is drawn and animated but never simulated.
// Presentation poses it from `position` and `heading` like an idle entity of `owner`.
struct Decoration
{
    MapEntityLook look;
    Vector3 position;
    float heading{};
    std::uint32_t owner{};
};

} // namespace eaw
