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
// Scripts also ask for presentation (sounds at objects): those requests go into the same per-script
// buffers and are merged the same way into a list the driver reads after the phase
// (presentation_requests); they never reach the simulation, the state hash, or the log.
//
// A driver's tick is: battle.step(), then BattleScripts::run(battle), then the players' orders.

#include "engine/battle.hpp"
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
    // What scripts read of a type (Battle::type_info), or nullptr.
    [[nodiscard]] const ScriptTypeInfo* type_info(std::uint32_t type) const noexcept;
    // A land battle (it has ground), else a space battle.
    [[nodiscard]] bool land() const noexcept;
    // Snapshot positions of the living entities of a type, in snapshot order.
    [[nodiscard]] std::span<const std::uint32_t> entities_of_type(std::uint32_t type) const noexcept;
    // Living placed entities whose hint matches, ignoring case, in map order.
    [[nodiscard]] std::vector<EntityId> hinted(std::string_view hint) const;
    // Entities that appeared since the last rebuild, in snapshot order.
    [[nodiscard]] std::span<const EntityId> new_entities() const noexcept;
    [[nodiscard]] const SimSnapshot& snapshot() const noexcept;

private:
    std::uint32_t intern_type(const Battle& battle, std::string_view name);

    const SimSnapshot* snapshot_{};
    // Entity slot -> snapshot position + 1 (0: no entity in that slot).
    std::vector<std::uint32_t> slot_positions_;
    // Entity slot -> (generation, type id) of the entity last seen there, so a type name is looked
    // up once per entity, not once per tick.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> slot_types_;
    std::vector<std::string> type_names_;
    // Per type id: the battle's ScriptTypeInfo (its storage keeps addresses), or nullptr.
    std::vector<const ScriptTypeInfo*> type_infos_;
    bool land_{};
    // Folded type name -> type id.
    std::unordered_map<std::string, std::uint32_t> type_index_;
    std::vector<std::vector<std::uint32_t>> type_entities_;
    // Spawnable types already interned (the battle only adds to them).
    std::size_t spawnable_seen_{};
    // The battle's hinted placed entities with folded hints (fixed for a battle).
    std::vector<std::pair<std::string, EntityId>> hints_;
    bool hints_read_{};
    std::vector<EntityId> new_entities_;
};

class BattleScriptContext;

// A sound a script asked for (GameObject.Play_SFX_Event): the sound event, the object, and where it
// stood in the view the script read.
struct ScriptSoundRequest
{
    std::string event;
    EntityId object;
    Vector3 position;
};

// Wall-clock cost of the script phases run so far (measurement, never read by the simulation).
struct BattleScriptStats
{
    // Scripts started (game-object scripts included) and scripts that could not be.
    std::uint64_t started{};
    std::uint64_t failed{};
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
    // The script phase after a tick: rebuilds the view; ends the scripts of objects that are gone
    // and starts the game-object scripts (Battle::type_info) of entities that appeared, in
    // snapshot order; advances every script one tick in order; and submits their orders to the
    // battle in the same order. A script that cannot be started is reported once per name.
    void run(Battle& battle);

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const BattleScriptStats& stats() const noexcept;
    // The sounds the scripts asked for in the last run(), in script order, then call order.
    [[nodiscard]] std::span<const ScriptSoundRequest> sound_requests() const noexcept;
    [[nodiscard]] const ScriptHost& host(std::size_t index) const;

private:
    // add() on the current view.
    Result<std::size_t> start(std::string_view script, std::optional<EntityId> object);

    const AssetReader& reader_;
    ScriptHostOptions options_;
    BattleScriptView view_;
    std::vector<std::unique_ptr<BattleScriptContext>> contexts_;
    std::vector<std::string> failed_scripts_;
    std::vector<ScriptSoundRequest> sound_requests_;
    BattleScriptStats stats_;
};

} // namespace eaw
