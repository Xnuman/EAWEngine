#include "eaw/spawn_catalog.hpp"

#include "eaw/game_object_catalog.hpp"
#include "eaw/xml_asset_database.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <unordered_set>

namespace eaw {
namespace {

std::string fold(std::string text)
{
    std::ranges::transform(text, text.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return text;
}

// Element types of units and structures in the stock XML (the owner breakdown of the map-objects
// audit); everything else with a model is props, markers, effects, and backdrops.
constexpr std::array<std::string_view, 17> unit_kinds{
    "genericherounit", "groundbuildable", "groundinfantry", "groundstructure",  "groundvehicle", "herounit",      "indigenous_unit", "mobile_defense_unit", "secondarystructure",
    "spacebuildable",  "spacestructure",  "spaceunit",      "specialstructure", "starbase",      "transportunit", "uniqueunit",      "herocompany",
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
        if ((!land && !space) || !seen.insert(fold(definition.name)).second)
        {
            continue;
        }
        const auto kind = fold(definition.type);
        types.push_back({definition.name, definition.type, land, space, std::ranges::find(unit_kinds, kind) != unit_kinds.end()});
    }
    std::ranges::sort(types,
                      [](const SpawnTypeInfo& left, const SpawnTypeInfo& right)
                      {
                          return fold(left.name) < fold(right.name);
                      });
    return types;
}

} // namespace eaw
