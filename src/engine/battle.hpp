#pragma once

// Tactical battles above the simulation. A Battle sets up a SimulationWorld from a built map and
// takes orders in replayable terms (XML type names, owner slots, points, entity ids), turning each
// into simulation commands applied at the next tick boundary and recording it with that tick, so
// the same map, data, and orders reproduce the same battle. The sandbox and headless runs share it.

#include "eaw/combat.hpp"
#include "eaw/ground_map.hpp"
#include "eaw/simulation.hpp"
#include "eaw/status.hpp"
#include "visual_scene_builder.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace eaw {

// Visual key of entities spawned by order from spawnable type `type_index`; presentation draws
// them by it. Map entities keep their scene mesh index as key, which stays below the base.
inline constexpr std::uint32_t spawnable_visual_base = 0x80000000U;
[[nodiscard]] constexpr std::uint32_t spawnable_visual(const std::size_t type_index) noexcept
{
    return spawnable_visual_base + static_cast<std::uint32_t>(type_index);
}

// What a battle needs from a built map (VisualMapAsset holds it; tests build it directly).
struct BattleMap
{
    // Placed entities, spawned in order when the battle starts.
    std::span<const MapEntitySpawn> entities;
    // Types that spawn orders may name, by XML type name.
    std::span<const SpawnableType> spawnable;
    // Ground of land maps (none on space maps).
    const GroundMap* ground{};
    DamageTable damage_table;
    Hostility hostility;
};

// The map's placed entities, spawnable types, ground, and damage table, with the provisional
// owner-slot hostility (combat_content.hpp).
[[nodiscard]] BattleMap battle_map(const VisualMapAsset& asset);

enum class BattleOrder : std::uint8_t
{
    // `count` groups of `type` for `owner` around `point`: single types on a grid 40 units apart,
    // companies and squadrons as their members in formation, groups 160 units apart.
    spawn,
    // `entities` move to `point` as one formation (SimCommandKind::formation_move).
    move,
    // `entities` attack `target`.
    attack,
    // `entities` stop moving and attacking.
    stop,
    // `entities` take `stance`.
    set_stance,
};

struct BattleCommand
{
    BattleOrder order{BattleOrder::move};
    std::vector<EntityId> entities;
    SimVector3 point;
    EntityId target;
    std::string type;
    std::uint32_t count{1};
    std::uint32_t owner{};
    Stance stance{Stance::aggressive};
};

// An order and the tick it was given at: it applies in the step after that many ticks ran.
struct BattleLogEntry
{
    std::uint64_t tick{};
    BattleCommand command;
};

struct BattleLog
{
    // Virtual path of the map (informational; a replay names its map itself).
    std::string map;
    std::vector<BattleLogEntry> entries;
    // Where the recorded battle ended: its tick and state hash (a replay runs to that tick and
    // compares the hash).
    std::optional<std::uint64_t> end_tick;
    std::optional<std::uint64_t> end_hash;
};

class Battle final
{
public:
    // Sets up the world: the map's ground, damage table, and hostility, then its placed entities in
    // order (entities the world rejects are logged as warnings and skipped), then one tick so a
    // first snapshot exists. The spawnable types are copied.
    Battle(const BattleMap& map, std::string map_name, double tick_seconds = 1.0 / 30.0);

    [[nodiscard]] SimulationWorld& world() noexcept;
    [[nodiscard]] const SimulationWorld& world() const noexcept;
    // Id of each placed entity in map order (empty where the world rejected it).
    [[nodiscard]] std::span<const std::optional<EntityId>> map_entity_ids() const noexcept;
    [[nodiscard]] std::span<const SpawnableType> spawnable() const noexcept;

    // Validates an order, queues its simulation commands for the next tick, and records it. Unknown
    // types, counts above 1000, owners beyond the hostility table, non-finite points, type names
    // with whitespace (they could not be recorded), and orders without entities are errors that
    // queue and record nothing.
    Status submit(const BattleCommand& command);
    void step();
    [[nodiscard]] const BattleLog& log() const noexcept;

private:
    [[nodiscard]] Result<SimCommand> translate(const BattleCommand& command) const;
    [[nodiscard]] EntitySpawn spawn_of(std::size_t type_index, SimVector3 position, std::uint32_t owner) const;

    SimulationWorld world_;
    std::vector<SpawnableType> spawnable_;
    std::vector<std::optional<EntityId>> map_ids_;
    BattleLog log_;
};

} // namespace eaw
