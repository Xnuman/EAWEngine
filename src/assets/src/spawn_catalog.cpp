#include "assets/spawn_catalog.hpp"
#include "assets/map_object_layout.hpp"

#include "assets/game_object_catalog.hpp"
#include "assets/xml_asset_database.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <unordered_set>

namespace eaw {
namespace {

// Element types of units and structures in the stock XML (the owner breakdown of the map-objects
// audit); everything else with a model is props, markers, effects, and backdrops.
// Companies and squadrons spawn their members.
constexpr std::array<std::string_view, 19> unit_kinds{
    "genericherounit", "groundbuildable", "groundinfantry",   "groundstructure", "groundvehicle", "herounit",   "indigenous_unit", "mobile_defense_unit", "secondarystructure", "spacebuildable",
    "spacestructure",  "spaceunit",       "specialstructure", "starbase",        "transportunit", "uniqueunit", "herocompany",     "groundcompany",       "squadron",
};

} // namespace

std::vector<SpawnTypeInfo> list_spawn_types(const AssetReader& reader)
{
    const auto database = XmlAssetDatabase::build(reader);
    const auto catalog = GameObjectCatalog::build(database);
    return list_spawn_types(database, catalog);
}

std::vector<SpawnTypeInfo> list_spawn_types(const XmlAssetDatabase& database, const GameObjectCatalog& catalog)
{
    std::vector<SpawnTypeInfo> types;
    std::unordered_set<std::string> seen;
    for (const auto& definition : database.definitions())
    {
        if (definition.name.empty())
        {
            continue;
        }
        const bool land = catalog.model(definition, GameModelContext::land).has_value();
        const bool space = catalog.model(definition, GameModelContext::space).has_value();
        if ((!land && !space) || !seen.insert(ascii_lower(definition.name)).second)
        {
            continue;
        }
        const auto kind = ascii_lower(definition.type);
        SpawnTypeInfo info{definition.name, definition.type, land, space, std::ranges::find(unit_kinds, kind) != unit_kinds.end(), {}};
        if (const auto* affiliation = catalog.field(definition, "Affiliation").field)
        {
            for (const auto name : split_type_names(affiliation->text))
            {
                info.affiliations.emplace_back(name);
            }
        }
        types.push_back(std::move(info));
    }
    std::ranges::sort(types,
                      [](const SpawnTypeInfo& left, const SpawnTypeInfo& right)
                      {
                          return ascii_lower(left.name) < ascii_lower(right.name);
                      });
    return types;
}

} // namespace eaw
