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
#include "eaw/formation.hpp"
#include "eaw/ground_map.hpp"
#include "eaw/pathfinding.hpp"
#include "eaw/simulation_types.hpp"
#include "eaw/spatial_grid.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace eaw {

// The ground an immobile obstacle occupies: a rectangle (half extents along and across its heading)
// whose cells take `passability` while the entity exists (a wall's own type, so classes allowed on
// that type still cross it, or Obstacle).
struct GroundFootprint
{
    SimVector3 center;
    float half_length{};
    float half_width{};
    // Rotation about +Z in radians.
    float heading{};
    std::uint32_t passability{passability::obstacle};
};

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
    // Footprint radius in world units for unit separation; zero takes no space.
    float radius{};
    // Turn rate in radians per second; zero turns instantly.
    float turn_rate{};
    // Formation rank (lower stands in front) and spacing (see formation.hpp).
    std::uint32_t formation_order{2};
    float formation_spacing{0.5F};
    // Ground occupied while the entity exists (structures, walls, obstacle props).
    std::optional<GroundFootprint> footprint;
    // Flight (fighters): above zero the entity never stops. It flies along its heading at no less than
    // this speed, steering toward its target (or its destination, which it circles on arrival) at
    // its turn rate, so attack runs overshoot and come back.
    float min_speed{};
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
    // Move the mobile entities together: each to its slot of a formation centered on `target`
    // (formation.hpp), all at the slowest member's speed, members ahead of the group waiting for
    // the rest. Clears attack orders like move.
    formation_move,
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
    float radius{};
    // Destroyable hardpoints, and how many of them are destroyed.
    std::uint32_t hardpoints{};
    std::uint32_t hardpoints_destroyed{};
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
        // Ground moves whose line reached a cell their class may not enter (pushed off their route, or
        // the ground changed) and were planned again.
        std::uint64_t blocked_moves{};
        // Ground move orders planned through the pathfinder, how many of them end short of the
        // ordered point, and the nodes they expanded.
        std::uint64_t path_searches{};
        std::uint64_t partial_paths{};
        std::uint64_t path_expansions{};
        // Path requests waiting for the per-tick search budget, and formation routes planned on one
        // shared path.
        std::size_t queued_paths{};
        std::uint64_t shared_routes{};
        std::uint64_t shots_fired{};
        std::uint64_t hits{};
        std::uint64_t kills{};
        std::uint64_t hardpoints_destroyed{};
        std::size_t projectiles{};
        // Overlapping ground pairs pushed apart in the last tick.
        std::size_t separated_pairs{};
        // Group members waiting for the group in the last tick, and members released from their
        // group because they stopped making progress.
        std::size_t waiting_members{};
        std::uint64_t released_members{};
    };

    explicit SimulationWorld(double tick_seconds = 1.0 / 30.0);

    // Spawning is immediate and intended for setup between ticks (map load, reinforcements). Spawn
    // data comes from map and mod content: a non-finite position, heading, or speed, or full entity
    // storage, returns an error and spawns nothing.
    [[nodiscard]] Result<EntityId> spawn(const EntitySpawn& spawn);
    // Ground for entities that follow it; entity footprints are stamped into a copy of its
    // passability (and removed when their entity is destroyed). A move order for a ground entity with passable_types is
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
        // Per weapon: ticks until the next burst may start, shots left in the burst, and ticks until
        // the burst's next shot.
        struct WeaponState
        {
            std::uint32_t cooldown{};
            std::uint32_t pulses_left{};
            std::uint32_t pulse_timer{};
            // Victim of the current burst.
            EntityId target;
        };
        std::vector<WeaponState> weapons;
        std::vector<float> hardpoint_health;
        // Ticks until the next automatic target search, and until an ordered chase replans.
        std::uint32_t retarget_timer{};
        std::uint32_t chase_timer{};
    };
    struct Projectile
    {
        SimVector3 position;
        SimVector3 aim;
        SimVector3 direction;
        EntityId target;
        // The victim's hardpoint the shot aims at, or -1 for its hull.
        std::int32_t target_hardpoint{-1};
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
    // Requests a route to `target` (queued for the search budget when it needs a search).
    void plan_move(std::size_t index, const SimVector3& target);
    // Plans now: a full search from the entity's position.
    void plan_route_now(std::size_t index, const SimVector3& target);
    // Sets an entity's route from planned waypoints (the last is its destination).
    void set_route(std::size_t index, const std::vector<PathPoint>& waypoints, float height);
    // Works through queued path requests until the tick's search budget is spent.
    void process_path_queue();
    struct PathRequest;
    // Plans a shared request's members until the tick's budget is spent; false when members remain.
    [[nodiscard]] bool plan_shared_route(PathRequest& request, std::uint64_t spent_before, bool first_in_tick);
    void plan_shared_member(const PathRequest& request, std::size_t index, const SimVector3& goal);
    void apply_formation_move(const SimCommand& command);
    // Remaining route length of every moving entity (zero for the rest).
    void measure_remaining_routes();
    // Marks group members that are ahead of their group to wait this tick, and releases members
    // that stopped making progress.
    void update_group_cohesion();
    // Speed this tick: the entity's own, or its group's when that is slower.
    [[nodiscard]] float travel_speed(std::size_t index) const noexcept;
    void move_over_ground(std::size_t index, float dt);
    void fly(std::size_t index, float dt);
    // Turns toward `desired` (radians) at the entity's turn rate and returns how much of its speed it
    // may use: 1 when facing the way, falling to 0 at 90 degrees or more off.
    [[nodiscard]] float steer(std::size_t index, float desired, float dt);
    void run_separation();
    // Stamps (or removes) an entity's footprint into the ground's passability.
    void stamp_footprint(std::size_t index, bool add);
    void run_combat();
    void update_target(std::size_t index);
    void run_weapon(std::size_t index);
    void launch(std::size_t shooter, std::size_t victim, const WeaponProfile& weapon);
    void apply_damage(std::size_t victim, float damage, std::uint16_t damage_type, bool damages_shield, bool damages_health, std::int32_t hardpoint);
    // A point of an entity's own frame in world space.
    [[nodiscard]] SimVector3 world_point(std::size_t index, const LocalPoint& point) const noexcept;
    [[nodiscard]] bool hardpoint_alive(std::size_t index, std::size_t hardpoint) const noexcept;
    // Whether any hardpoint of the kind survives (true when the entity has none of that kind).
    [[nodiscard]] bool keeps(std::size_t index, HardpointProfile::Kind kind) const noexcept;
    // The victim's live targetable hardpoint nearest to `from`, or -1.
    [[nodiscard]] std::int32_t aim_hardpoint(std::size_t victim, const SimVector3& from) const noexcept;
    [[nodiscard]] SimVector3 aim_point(std::size_t victim, std::int32_t hardpoint) const noexcept;
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
    std::vector<float> radii_;
    std::vector<float> turn_rates_;
    std::vector<float> min_speeds_;
    // Where an idle flyer circles (set when it arrives or first idles).
    std::vector<std::optional<SimVector3>> loiter_points_;
    std::vector<std::uint32_t> formation_orders_;
    std::vector<float> formation_spacings_;
    // Group speed of a formation move until the entity arrives; zero when none.
    std::vector<float> speed_limits_;
    std::vector<SimVector3> separation_push_;
    // A queued route request: one entity, or the ground members of a formation move that share a
    // movement class (planned on one shared path to `group_target`).
    struct PathRequest
    {
        std::vector<EntityId> members;
        std::vector<SimVector3> goals;
        std::vector<std::uint32_t> serials;
        SimVector3 group_target;
        // Shared requests: the shared path once planned, the commander's start and movement class,
        // and the next member to plan (a request can span several ticks).
        bool shared_ready{};
        std::vector<PathPoint> shared_waypoints;
        PathPoint shared_start;
        std::uint32_t mask{};
        std::size_t next_member{};
    };
    std::deque<PathRequest> path_queue_;
    // Serial of the entity's queued request (zero when none); a newer request supersedes it.
    std::vector<std::uint32_t> path_pending_;
    std::uint32_t next_path_serial_{1};
    // Short legs onto and off a shared path search at most this many nodes.
    GridPathfinder local_pathfinder_{4096};
    std::uint64_t shared_routes_{};
    std::vector<std::optional<GroundFootprint>> footprints_;
    std::vector<std::uint8_t> footprint_stamped_;
    // The ground's own passability, and how many footprints cover each cell.
    std::vector<std::uint32_t> ground_passability_;
    std::vector<std::uint16_t> footprint_cover_;
    std::vector<float> remaining_route_;
    // Formation group of a member (zero when none), the remaining route it last made progress at,
    // ticks since then, and whether it waits this tick.
    std::vector<std::uint32_t> group_ids_;
    std::vector<float> group_best_remaining_;
    std::vector<std::uint32_t> group_stall_ticks_;
    std::vector<std::uint8_t> group_waiting_;
    std::uint32_t next_group_id_{1};
    std::size_t waiting_members_{};
    std::uint64_t released_members_{};
    std::size_t separated_pairs_{};
    std::vector<Projectile> projectiles_;
    SpatialGrid grid_;
    std::vector<std::uint32_t> query_;
    DamageTable damage_table_;
    Hostility hostility_;
    std::uint64_t shots_fired_{};
    std::uint64_t hits_{};
    std::uint64_t kills_{};
    std::uint64_t hardpoints_destroyed_{};
    std::vector<SimCommand> pending_;
    SimSnapshot snapshot_;
    std::uint64_t commands_applied_{};
    std::uint64_t stale_references_{};
};

// Presentation helper: blends two consecutive snapshots by `alpha` (0 = previous, 1 = current).
// Entities missing from `previous` (just spawned) use their current state.
[[nodiscard]] std::vector<EntitySnapshot> interpolate_snapshots(const SimSnapshot& previous, const SimSnapshot& current, float alpha);

} // namespace eaw
