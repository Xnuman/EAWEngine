#pragma once

// Deterministic tactical simulation core. No graphics, platform, or file access: the world advances
// on a fixed tick, mutates only while applying commands and running systems inside step(), and
// publishes immutable snapshots for presentation.
//
// Determinism contract (same build, same platform): identical spawns and commands submitted in the
// same order produce bit-identical state (see state_hash). Iteration follows dense storage order,
// which depends only on the spawn/destroy history. Floating-point behavior across compilers or CPUs
// is not yet guaranteed; cross-machine lockstep will need a stricter numeric policy.

#include "eaw/status.hpp"

#include "eaw/combat.hpp"
#include "eaw/ground_map.hpp"
#include "eaw/pathfinding.hpp"
#include "eaw/simulation_types.hpp"
#include "eaw/spatial_grid.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace eaw {

// Everything needed to create an entity. `type_name` is the XML object type (informational for
// now); `visual` is an opaque presentation key chosen by the caller (for example a mesh index).
struct EntitySpawn
{
    std::string type_name;
    std::uint32_t owner{};
    SimVector3 position;
    // Rotation about +Z in radians.
    float heading{};
    // World units per second; zero means the entity cannot move.
    float max_speed{};
    std::uint32_t visual{};
    bool selectable{true};
    // Moves over the world's ground (height from the surface) instead of in a straight 3D line.
    bool follows_ground{};
    // Passability types a ground entity may enter (its movement class; see ground_map.hpp). Zero
    // means unconstrained.
    std::uint32_t passable_types{};
    // Health, shields, armor, and weapon; a default profile cannot be damaged and does not fight.
    CombatProfile combat;
};

enum class SimCommandKind : std::uint8_t
{
    // Move to `target`; clears an attack order.
    move,
    // Stop moving and attacking.
    stop,
    destroy,
    // Attack `target_entity`: armed entities chase it until it is in range and fire until it dies.
    attack,
};

struct SimCommand
{
    SimCommandKind kind{SimCommandKind::move};
    std::vector<EntityId> entities;
    // Destination for move commands.
    SimVector3 target;
    // Victim of attack commands.
    EntityId target_entity;
};

// Snapshot of one entity after a tick.
struct EntitySnapshot
{
    EntityId id;
    SimVector3 position;
    float heading{};
    std::uint32_t visual{};
    std::uint32_t owner{};
    bool moving{};
    bool selectable{};
    float max_speed{};
    bool follows_ground{};
    float health{};
    float max_health{};
    float shield{};
    float max_shield{};
    // The entity its weapon is aimed at (invalid when none).
    EntityId target;
};

struct ProjectileSnapshot
{
    SimVector3 position;
    // Unit direction of flight.
    SimVector3 direction;
    std::uint32_t owner{};
};

struct SimSnapshot
{
    std::uint64_t tick{};
    std::vector<EntitySnapshot> entities;
    std::vector<ProjectileSnapshot> projectiles;
};

// Converts real elapsed time into whole simulation ticks. At most `max_ticks_per_advance` ticks are
// returned per call (excess time is dropped), so a long stall cannot trigger an ever-growing
// backlog of ticks.
class FixedStepClock final
{
public:
    explicit FixedStepClock(double ticks_per_second = 30.0, std::uint32_t max_ticks_per_advance = 8);

    // Adds elapsed real time and returns how many ticks should run now.
    std::uint32_t advance(double seconds);
    // Fraction (0..1) of the next tick already elapsed, for interpolating presentation.
    [[nodiscard]] float interpolation() const noexcept;
    [[nodiscard]] double tick_seconds() const noexcept;
    [[nodiscard]] std::uint64_t dropped_ticks() const noexcept;

private:
    double tick_seconds_;
    std::uint32_t max_ticks_;
    double accumulator_{};
    std::uint64_t dropped_{};
};

class SimulationWorld final
{
public:
    struct Stats
    {
        std::uint64_t tick{};
        std::size_t entities{};
        std::uint64_t commands_applied{};
        // Commands or command targets that named destroyed or unknown entities.
        std::uint64_t stale_references{};
        // Ground moves stopped at a cell their class may not enter (a route that no longer fits).
        std::uint64_t blocked_moves{};
        // Ground move orders planned through the pathfinder, how many of them end short of the
        // ordered point, and the nodes they expanded.
        std::uint64_t path_searches{};
        std::uint64_t partial_paths{};
        std::uint64_t path_expansions{};
        std::uint64_t shots_fired{};
        std::uint64_t hits{};
        std::uint64_t kills{};
        std::size_t projectiles{};
    };

    explicit SimulationWorld(double tick_seconds = 1.0 / 30.0);

    // Spawning is immediate and intended for setup between ticks (map load, reinforcements). Spawn
    // data comes from map and mod content: a non-finite position, heading, or speed, or full entity
    // storage, returns an error and spawns nothing.
    [[nodiscard]] Result<EntityId> spawn(const EntitySpawn& spawn);
    // Ground for entities that follow it. A move order for a ground entity with passable_types is
    // planned when it is applied (GridPathfinder): the entity follows the waypoints, ending at the
    // ordered point or, when that cannot be reached, at the closest point found. A ground entity
    // also stops before a cell its class may not enter; one standing on such a cell may leave it.
    void set_ground(GroundMap ground);
    [[nodiscard]] const GroundMap& ground() const noexcept;
    // Damage multipliers by damage and armor type, and which owners fight each other. Armed
    // entities target the nearest damageable hostile within range on their own; an attack command
    // names the target instead.
    void set_damage_table(DamageTable table);
    void set_hostility(const Hostility& hostility) noexcept;
    [[nodiscard]] const Hostility& hostility() const noexcept;

    // Queues a command for the next step(); commands apply in submission order.
    void submit(SimCommand command);
    // Applies queued commands, runs the systems once, and publishes a new snapshot.
    void step();

    [[nodiscard]] bool alive(EntityId id) const noexcept;
    // The points an entity is still moving through, in order, ending at its destination (empty
    // when it is not moving or unknown). For debugging and presentation; not part of snapshots.
    [[nodiscard]] std::vector<SimVector3> route(EntityId id) const;
    [[nodiscard]] const SimSnapshot& snapshot() const noexcept;
    [[nodiscard]] Stats stats() const noexcept;
    // FNV-1a over every component, in storage order: equal hashes mean identical simulation state.
    [[nodiscard]] std::uint64_t state_hash() const noexcept;

private:
    struct CombatState
    {
        CombatProfile profile;
        float health{};
        float shield{};
        EntityId target;
        // The target came from an attack command: chase it out of range instead of dropping it.
        bool ordered{};
        // Ticks until the next burst may start, until the next shot of a burst, until the next
        // automatic target search, and until an ordered chase replans.
        std::uint32_t cooldown{};
        std::uint32_t pulses_left{};
        std::uint32_t pulse_timer{};
        std::uint32_t retarget_timer{};
        std::uint32_t chase_timer{};
    };
    struct Projectile
    {
        SimVector3 position;
        SimVector3 aim;
        SimVector3 direction;
        EntityId target;
        std::uint32_t owner{};
        float damage{};
        std::uint16_t damage_type{};
        bool damages_shield{};
        bool damages_health{};
        float speed{};
        float flown{};
        float max_flight{};
    };

    [[nodiscard]] std::optional<std::size_t> dense_index(EntityId id) const noexcept;
    void apply(const SimCommand& command);
    void destroy(std::size_t dense);
    void run_movement();
    void plan_move(std::size_t index, const SimVector3& target);
    void move_over_ground(std::size_t index, float dt);
    void run_combat();
    void update_target(std::size_t index);
    void run_weapon(std::size_t index);
    void launch(std::size_t shooter, std::size_t victim);
    void apply_damage(std::size_t victim, float damage, std::uint16_t damage_type, bool damages_shield, bool damages_health);
    void run_projectiles();
    void remove_dead();
    [[nodiscard]] bool damageable(std::size_t index) const noexcept;
    [[nodiscard]] std::uint32_t ticks_for(float seconds) const noexcept;
    void publish();

    double tick_seconds_;
    std::uint64_t tick_{};
    // Slot table: generation per slot and the slot's position in dense storage.
    std::vector<std::uint32_t> generations_;
    std::vector<std::uint32_t> slot_dense_;
    std::vector<std::uint32_t> free_slots_;
    // Dense structure-of-arrays components.
    std::vector<EntityId> ids_;
    std::vector<SimVector3> positions_;
    std::vector<float> headings_;
    std::vector<float> max_speeds_;
    std::vector<std::optional<SimVector3>> move_targets_;
    std::vector<std::uint32_t> owners_;
    std::vector<std::uint32_t> visuals_;
    std::vector<std::uint8_t> selectable_;
    std::vector<std::string> type_names_;
    std::vector<std::uint8_t> follows_ground_;
    GroundMap ground_;
    std::vector<std::uint32_t> passable_types_;
    // Waypoints before the move target, last first (back() is the next one to reach).
    std::vector<std::vector<SimVector3>> paths_;
    GridPathfinder pathfinder_;
    std::uint64_t blocked_moves_{};
    std::uint64_t path_searches_{};
    std::uint64_t partial_paths_{};
    std::uint64_t path_expansions_{};
    std::vector<CombatState> combat_;
    std::vector<Projectile> projectiles_;
    SpatialGrid grid_;
    std::vector<std::uint32_t> query_;
    DamageTable damage_table_;
    Hostility hostility_;
    std::uint64_t shots_fired_{};
    std::uint64_t hits_{};
    std::uint64_t kills_{};
    std::vector<SimCommand> pending_;
    SimSnapshot snapshot_;
    std::uint64_t commands_applied_{};
    std::uint64_t stale_references_{};
};

// Presentation helper: blends two consecutive snapshots by `alpha` (0 = previous, 1 = current).
// Entities missing from `previous` (just spawned) use their current state.
[[nodiscard]] std::vector<EntitySnapshot> interpolate_snapshots(const SimSnapshot& previous, const SimSnapshot& current, float alpha);

} // namespace eaw
