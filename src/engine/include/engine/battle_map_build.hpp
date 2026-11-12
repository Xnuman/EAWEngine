#pragma once

// The walk behind build_battle_map, which the visual build shares: what it reads, and the model each
// entity and spawnable type was built from, so drawing starts from the same models.

#include "math/matrix.hpp"
#include "formats/ted_map.hpp"
#include "assets/game_object_catalog.hpp"
#include "assets/map_object_layout.hpp"
#include "assets/model.hpp"
#include "assets/model_geometry.hpp"
#include "assets/resource_manager.hpp"
#include "engine/battle_map.hpp"
#include "engine/combat_content.hpp"
#include "engine/object_types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace eaw {

// The model a placed entity or spawnable type was built from, which drawing it starts from: its
// path (empty for a company, drawn as its members) and hardpoint attachments, its XML type (null for
// an untyped placement), and, for a placed entity, its transform (the placement's at the type's
// Scale_Factor, moved into its company's formation) and player slot.
struct BuiltModel
{
    std::string path;
    std::vector<ModelAttachment> attachments;
    const XmlDefinition* type{};
    Matrix4 transform{Matrix4::identity()};
    float scale{1.0F};
    std::uint32_t player{};
};

// What the walk reads: the map and the XML content it was built against.
struct BattleMapSources
{
    const TedMapData& map;
    const GameObjectCatalog& catalog;
    const MapObjectLayout& layout;
    const MovementClasses& movement;
    const CombatContent& combat;
};

// The walk behind build_battle_map: fills `asset` (its name already set) and, per entity and per
// spawnable type (same index), the model it was built from.
void build_battle_map(ResourceManager resources, const BattleMapSources& sources, std::span<const std::string> spawn_types, BattleMapAsset& asset, std::vector<BuiltModel>& entity_models,
                      std::vector<BuiltModel>& spawnable_models);

// What adding a spawnable type needs: the resources its model loads through, the XML catalog and
// movement classes, the model context (land or space), a combat profile source (one per type), and
// the spawnable list to extend with the model of each (same index).
struct SpawnableBuild
{
    ResourceManager resources;
    const GameObjectCatalog& catalog;
    const std::unordered_map<std::string, std::uint32_t>& movement_classes;
    GameModelContext context{};
    bool land{};
    std::function<const CombatProfile&(const XmlDefinition&, const Model*)> profile_of;
    std::vector<SpawnableType>& spawnable;
    std::vector<BuiltModel>& models;
    std::vector<std::string>& warnings;
    // GameConstants' Object_Max_Speed_Multiplier for the battle's mode (CombatContent).
    float speed_multiplier{1.0F};
};

// Adds an XML type (by name, case-insensitive) to build.spawnable, or, for a company or squadron,
// its members and then itself; returns its index (an existing entry is reused) or nullopt with a
// warning when it has no model for the context, and nullopt when its model has nothing to draw.
std::optional<std::size_t> add_spawnable_type(SpawnableBuild& build, const std::string& type_name, int depth);

} // namespace eaw
