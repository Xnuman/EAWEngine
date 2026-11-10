#pragma once

// Scripts in battles: the bridge between the script host and a Battle. Scripts read the battle
// through engine objects (a game object, its type and owner, a position, the block an order
// returns) and give orders that become validated BattleCommands. The script phase of a tick follows
// fixed rules, so it gives the same result on one thread or several:
//
// - every script reads the same immutable view: the snapshot of the tick that just ran and indices
//   built over it once, before any script runs;
// - everything a script does to the battle goes into its own buffer, merged after all scripts ran,
//   in the order the scripts were added (never in the order they finished), and applied in the
//   next tick;
// - script orders are not recorded in the battle log: a replay runs the same scripts again.
//
// A driver's tick is: battle.step(), then BattleScripts::run(battle), then the players' orders.

#include "battle.hpp"
#include "script/script_host.hpp"
#include "vfs/virtual_file_system.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eaw {

// What scripts read during one script phase: the last snapshot, entities by slot and by type, and
// the battle's object types under stable ids. Rebuilt after every tick; never changed while scripts
// run.
class BattleScriptView final
{
public:
    void rebuild(const Battle& battle);

    [[nodiscard]] const EntitySnapshot* entity(EntityId id) const noexcept;
    // Type ids follow the order types were first met (spawnable types, then entities); a name
    // matches ignoring case.
    [[nodiscard]] std::optional<std::uint32_t> type_id(std::string_view name) const;
    [[nodiscard]] std::string_view type_name(std::uint32_t type) const;
    [[nodiscard]] std::optional<std::uint32_t> type_of(EntityId id) const noexcept;
    // Snapshot positions of the living entities of a type, in snapshot order.
    [[nodiscard]] std::span<const std::uint32_t> entities_of_type(std::uint32_t type) const noexcept;
    [[nodiscard]] const SimSnapshot& snapshot() const noexcept;

private:
    std::uint32_t intern_type(std::string_view name);

    const SimSnapshot* snapshot_{};
    // Entity slot -> snapshot position + 1 (0: no entity in that slot).
    std::vector<std::uint32_t> slot_positions_;
    // Entity slot -> (generation, type id) of the entity last seen there, so a type name is looked
    // up once per entity, not once per tick.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> slot_types_;
    std::vector<std::string> type_names_;
    // Folded type name -> type id.
    std::unordered_map<std::string, std::uint32_t> type_index_;
    std::vector<std::vector<std::uint32_t>> type_entities_;
    // Spawnable types already interned (the battle only adds to them).
    std::size_t spawnable_seen_{};
};

class BattleScriptContext;

// Wall-clock cost of the script phases run so far (measurement, never read by the simulation).
struct BattleScriptStats
{
    std::uint64_t phases{};
    double total_seconds{};
    double worst_phase_seconds{};
};

class BattleScripts final
{
public:
    // `reader` resolves the scripts; `options` are each host's (its recorder and provider are set
    // by BattleScripts).
    BattleScripts(const AssetReader& reader, ScriptHostOptions options = {});
    ~BattleScripts();
    BattleScripts(const BattleScripts&) = delete;
    BattleScripts& operator=(const BattleScripts&) = delete;

    // Starts a script as the framework does: load, Base_Definitions (else Definitions), `Object`
    // bound to `object` for a game object's script, then `main` as a thread. The script reads the
    // battle as it was last viewed (run() rebuilds the view). Returns the script's index.
    Result<std::size_t> add(const Battle& battle, std::string_view script, std::optional<EntityId> object = std::nullopt);
    // The script phase after a tick: rebuilds the view, advances every script one tick in order,
    // and submits their orders to the battle in the same order.
    void run(Battle& battle);

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const BattleScriptStats& stats() const noexcept;
    [[nodiscard]] const ScriptHost& host(std::size_t index) const;

private:
    const AssetReader& reader_;
    ScriptHostOptions options_;
    BattleScriptView view_;
    std::vector<std::unique_ptr<BattleScriptContext>> contexts_;
    BattleScriptStats stats_;
};

} // namespace eaw
