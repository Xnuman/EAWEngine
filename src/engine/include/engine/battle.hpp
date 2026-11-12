#pragma once

// Tactical battles above the simulation. A Battle sets up a SimulationWorld from a built map and
// takes orders in replayable terms (XML type names, owner slots, points, entity ids), turning each
// into simulation commands applied at the next tick boundary and recording it with that tick, so
// the same map, data, and orders reproduce the same battle. The sandbox and headless runs share it.

#include "core/status.hpp"
#include "simulation/combat.hpp"
#include "simulation/ground_map.hpp"
#include "simulation/simulation.hpp"
#include "engine/visual_scene_builder.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
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

// `point` moved onto the ground grid, then, when its cell's passability is not among
// `passable_types`, to the nearest cell that is (nearest_enterable); unchanged without passability
// data. Spawn orders place ground units with it.
[[nodiscard]] Vector3 open_ground(const GroundMap& ground, Vector3 point, std::uint32_t passable_types);

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
    // `entities` leave the battle at once, without dying (no death effects or corpses).
    remove,
    // `entities` move to `point` as a formation, or to `target` while it lives, engaging hostiles
    // on the way (SimCommandKind::attack_move).
    attack_move,
    // `entities` guard `point`, or `target` while it lives (SimCommandKind::guard).
    guard,
    // `flag` on or off for `entities`: no damage at all; damage cannot kill; no movement or turning.
    set_invulnerable,
    set_cannot_be_killed,
    suspend_locomotor,
    // `entities` take `amount` hull damage, past shields and armor.
    damage,
    // `entities` move to `point` at once, facing `heading` when given.
    teleport,
};

struct BattleCommand
{
    BattleOrder order{BattleOrder::move};
    std::vector<EntityId> entities;
    Vector3 point;
    EntityId target;
    std::string type;
    std::uint32_t count{1};
    std::uint32_t owner{};
    Stance stance{Stance::aggressive};
    // set_invulnerable, set_cannot_be_killed, suspend_locomotor.
    bool flag{};
    // damage.
    float amount{};
    // teleport: the heading to face (radians), or the entity's own.
    std::optional<float> heading;
};

// Who gave an order. Player orders are recorded in the battle log; script orders are not, because a
// replay runs the same scripts again and they give the same orders (the lockstep model: only player
// input is exchanged, everything else is computed). Player orders will be checked against the
// owner the player commands once the player layer exists; scripts may order any entity.
enum class BattleOrigin : std::uint8_t
{
    player,
    script,
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
    // Placed entities that carry a hint, in map order (Find_Hint).
    [[nodiscard]] std::span<const std::pair<EntityId, std::string>> hinted_entities() const noexcept;
    // What scripts read of a type (its game-object script, its categories), by XML type name ignoring
    // case; nullptr for types the battle has no such data for.
    [[nodiscard]] const ScriptTypeInfo* type_info(std::string_view type_name) const;
    [[nodiscard]] std::span<const SpawnableType> spawnable() const noexcept;
    // Adds spawnable types at runtime (a SpawnablePackage's types, with mesh indices already in
    // the scene's mesh library and member indices relative to `types`); returns the index of the
    // first. Their visual keys follow the existing types (spawnable_visual).
    std::size_t add_spawnable(std::span<const SpawnableType> types);

    // Validates an order, queues its simulation commands for the next tick, records it when a player
    // gave it, and returns its serial: entities it applies to report it as their
    // EntitySnapshot::command_serial until a later order replaces it. Unknown types, counts above
    // 1000, owners beyond the hostility table, non-finite points, type names with whitespace (they
    // could not be recorded), and orders without entities are errors that queue and record nothing.
    Result<std::uint64_t> submit(const BattleCommand& command, BattleOrigin origin = BattleOrigin::player);
    // Runs one tick; the survivors of objects destroyed in it are ordered spawned for the next
    // (script orders, so replays reproduce them without recording).
    void step();
    [[nodiscard]] const BattleLog& log() const noexcept;

private:
    [[nodiscard]] Result<SimCommand> translate(const BattleCommand& command) const;
    [[nodiscard]] EntitySpawn spawn_of(std::size_t type_index, Vector3 position, std::uint32_t owner) const;

    SimulationWorld world_;
    std::vector<SpawnableType> spawnable_;
    std::vector<std::optional<EntityId>> map_ids_;
    std::vector<std::pair<EntityId, std::string>> hinted_;
    // Folded type name -> what scripts read of it, from the map's entities and the spawnable types.
    std::unordered_map<std::string, ScriptTypeInfo> type_info_;
    BattleLog log_;
    std::uint64_t next_serial_{1};
    // Type key (game_object_name_crc) -> what its destruction leaves, from the map's entities and the
    // spawnable types.
    std::unordered_map<std::uint32_t, Survivors> survivors_;
    // The next company team id (after the map's; FormationTeam::id).
    mutable std::uint32_t next_team_{1};
};

} // namespace eaw
