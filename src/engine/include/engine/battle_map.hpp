#pragma once

// What a battle knows of a map: gameplay data only, apart from how anything is drawn (the scene,
// meshes, and each entity's look stay in visual_scene_builder.hpp). Built from the map, the XML
// types, and the models' skeletons and geometry; never from drawing.

#include "simulation/simulation.hpp"

#include <cstdint>
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

} // namespace eaw
