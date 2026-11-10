#pragma once

// The XML object types a sandbox scenario can spawn: every named definition with a land or space
// model, taken from the winning XML of the mounted layers (base game, expansion, and mods).

#include "assets/game_object_catalog.hpp"
#include "vfs/virtual_file_system.hpp"
#include "assets/xml_asset_database.hpp"

#include <string>
#include <vector>

namespace eaw {

struct SpawnTypeInfo
{
    std::string name;
    // XML element type (GroundVehicle, SpaceUnit, ...).
    std::string kind;
    bool land_model{};
    bool space_model{};
    // A unit or structure kind rather than a prop, marker, effect, or backdrop.
    bool unit{};
};

// Sorted by name (case-insensitive); one entry per name, from the first definition with a model.
[[nodiscard]] std::vector<SpawnTypeInfo> list_spawn_types(const AssetReader& reader);
// The same from an already built database and its catalog (the engine keeps them cached).
[[nodiscard]] std::vector<SpawnTypeInfo> list_spawn_types(const XmlAssetDatabase& database, const GameObjectCatalog& catalog);

} // namespace eaw
