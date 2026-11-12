#pragma once

// Deterministic tactical simulation core. No graphics, platform, or file access: the world advances
// on a fixed tick, mutates only while applying commands and running systems inside step(), and
// publishes immutable snapshots for presentation.
//
// Determinism contract (same build, same platform): identical spawns and commands submitted in the
// same order produce bit-identical state (see state_hash). Iteration follows dense storage order,
// which depends only on the spawn/destroy history. Floating-point behavior across compilers or CPUs
// is not yet guaranteed; cross-machine lockstep will need a stricter numeric policy.

#include "core/random.hpp"
#include "core/status.hpp"

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
#include <string_view>
#include <vector>

namespace eaw {

// The ground an immobile obstacle occupies: a rectangle (half extents along and across its heading)
// whose cells take `passability` while the entity exists (a wall's own type, so classes allowed on
// that type still cross it, or Obstacle).
struct GroundFootprint
{
    Vector3 center;
    float half_length{};
    float half_width{};
    // Rotation about +Z in radians.
    float heading{};
    std::uint32_t passability{passability::obstacle};
};

// Who runs over whom. A moving ground crusher kills each hostile entity it may crush whose center
// its footprint radius covers: a crusher (the vehicle collision classes, an engine choice) crushes
// squashable entities (infantry: XML Is_Squashable), a supercrusher (XML Is_Supercrusher: the
// AT-ATs, the Gargantuan, the HTT) also those squashable by a supercrusher (vehicles and walls:
// Is_Squashable_By_Supercrusher). Whether the original spares friendly units is unverified; the
// engine crushes only hostile ones.
enum class Crusher : std::uint8_t
{
    none,
    crusher,
    supercrusher,
};

struct CrushProfile
{
    Crusher crusher{Crusher::none};
    bool squashable{};
    bool squashable_by_supercrusher{};
};

// How a unit acts on its own, between and during orders (the behavior layer; orders always apply).
// Engine-native: the original's stances are not mapped yet.
enum class Stance : std::uint8_t
{
    // Targets the nearest hostile in range on its own; fighters fly attack runs at it.
    aggressive,
    // Fires at hostiles in range but never moves for them: fighters keep circling or flying their order.
    hold_position,
    // Never picks targets or fires on its own; only an attack order makes it engage.
    hold_fire,
};

// How a mover gets up to speed and turns (EntitySpawn::movement). The default profile changes speed
// at once and turns at its turn rate at any speed.
struct MovementProfile
{
    // World units per second squared to speed up and to slow down; zero changes speed at once. A
    // mover that slows down brakes ahead of its destination so it stops there.
    float acceleration{};
    float deceleration{};
    // Below this fraction of its travel speed a moving entity turns at stopped_turn_rate instead of
    // its turn rate, and it keeps at least this fraction of its speed while turning.
    float turn_speed_fraction{};
    // Turn rate (radians per second) below turn_speed_fraction. Zero cannot turn there: the entity
    // first gathers speed straight ahead, and it moves along its heading (space ships).
    float stopped_turn_rate{};
    // A mover whose way is more than this off its heading (radians) stops and turns before moving
    // off; zero turns while moving (ground units stop at corners this sharp).
    float turn_in_place_angle{};
};

// Everything needed to create an entity. `type_name` is the XML object type (informational for
// now); `visual` is an opaque presentation key chosen by the caller (for example a mesh index).
struct EntitySpawn
{
    std::string type_name;
    std::uint32_t owner{};
    Vector3 position;
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
    MovementProfile movement;
    // Formation rank (lower stands in front) and spacing (see formation.hpp).
    std::uint32_t formation_order{2};
    float formation_spacing{0.5F};
    // Takes no slot in a formation move: goes to the ordered point on its own, at its own speed
    // (XML FormationGrouping Solo: walkers, the largest vehicles, air units, large creatures).
    bool formation_solo{};
    // The company team it stands with in formations (formation.hpp).
    FormationTeam formation_team;
    CrushProfile crush;
    // Ground occupied while the entity exists (structures, walls, obstacle props).
    std::optional<GroundFootprint> footprint;
    // Flight (fighters): above zero the entity never stops. It flies along its heading at no less than
    // this speed, steering toward its target (or its destination, which it circles on arrival) at
    // its turn rate, so attack runs overshoot and come back.
    float min_speed{};
    Stance stance{Stance::aggressive};
    // Aims with its body instead of a turret (infantry): standing, it turns to face its target, and
    // it fires only while standing and facing the target within body_aim_tolerance.
    bool aims_with_body{};
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
    // Sets the entities' `stance`; hold_fire also drops targets they picked on their own.
    set_stance,
    // Creates `spawns` in order (reinforcements, debug spawns, scripted arrivals); spawns the world
    // rejects are counted in Stats::rejected_spawns.
    spawn,
    // Move like formation_move to `target` (or to `target_entity` while it lives, re-read as it
    // moves), engaging hostiles met on the way (StandingOrder::attack_move).
    attack_move,
    // Stay near `target` (or `target_entity`, followed as it moves) and engage hostiles that come
    // near it (StandingOrder::guard).
    guard,
    // `flag`: the entities take no damage at all (or take it again).
    set_invulnerable,
    // `flag`: damage cannot take the entities' health below unkillable_health_floor (or can again).
    set_cannot_be_killed,
    // `flag`: the entities do not move or turn; their orders wait until it is cleared.
    suspend_locomotor,
    // The entities' hulls take `amount` directly, past shields and armor (an entity it brings to
    // zero dies as in combat).
    damage,
    // The entities move to `target` at once, facing `heading` when given; their movement ends.
    teleport,
};

// What an entity keeps doing besides its current movement and target: nothing, an attack-move, or a
// guard. Every move, stop, attack, and formation move clears it.
enum class StandingOrder : std::uint8_t
{
    none,
    attack_move,
    guard,
};

// Standing order tuning (engine choices relative to the weapon's range, to be checked in play):
// hostiles are engaged within acquire_range_factor x range of an attack-mover, or within the guard
// radius of a guarded anchor; a chase breaks off beyond leash_range_factor x range from where it
// began (attack-move) or from the anchor (guard); the guard radius is the weapon's range, at least
// minimum_guard_radius world units; a guard returns once farther than half the radius from its
// anchor, and a moving destination or anchor is followed once it is farther than follow_tolerance
// from where the unit is heading.
inline constexpr float acquire_range_factor = 1.25F;
inline constexpr float leash_range_factor = 2.0F;
inline constexpr float minimum_guard_radius = 150.0F;
inline constexpr float follow_tolerance = 50.0F;

// The lowest health of an entity that cannot be killed (Set_Cannot_Be_Killed): one hit point, or a
// hundredth of its health when that is less (an engine choice).
inline constexpr float unkillable_health_floor = 1.0F;
// Seed of the combat random stream (where shots stray); an arbitrary fixed value, so a battle
// replays the same shots.
inline constexpr std::uint64_t combat_random_seed = 0x5eed0fba771e5ULL;
inline constexpr float unkillable_health_fraction = 0.01F;

// Space avoidance (engine choices to check in play): in the original, ships neither push each
// other nor overlap; they steer around units in their way, and pass above or below units at
// another height or much smaller than themselves (owner's observation). Each tick a moving ship
// tries avoid_course_candidates courses, the straight one to its target first and then turning
// right and left by avoid_course_step, and flies the first that keeps its clearance (both radii
// plus avoid_margin_fraction of its own) from every neighbour for avoid_lookahead_seconds (at
// least the time to travel avoid_min_lookahead_radii of its radius), the neighbours moving straight to
// their targets; when none does, the course whose contact comes latest (or that opens the gap of a
// ship already too close). A destination inside a still ship's clearance moves to the nearest free
// of avoid_arrival_candidates points on that clearance (plus avoid_arrival_slack). Ships at
// another height (more than both radii) or with a radius below avoid_small_fraction of its own are
// flown past.
inline constexpr float avoid_lookahead_seconds = 3.0F;
inline constexpr float avoid_min_lookahead_radii = 4.0F;
inline constexpr float avoid_margin_fraction = 0.25F;
inline constexpr float avoid_small_fraction = 0.2F;
inline constexpr std::uint32_t avoid_course_candidates = 31;
inline constexpr float avoid_course_step = 0.19634954F; // pi / 16, 11.25 degrees
inline constexpr std::uint32_t avoid_arrival_candidates = 16;
inline constexpr float avoid_arrival_slack = 0.05F;
// Formation spacing of ships in space, at least (an engine choice; FormationSpacing is a ground
// field in the stock data): slots 4.5 radii apart leave a ship room to pass between two stopped ones
// with its avoidance clearance (2.25 radii each side), so late arrivals can reach slots ahead.
inline constexpr float space_formation_spacing = 1.25F;

struct SimCommand
{
    SimCommandKind kind{SimCommandKind::move};
    std::vector<EntityId> entities;
    // Destination for move commands.
    Vector3 target;
    // Victim of attack commands.
    EntityId target_entity;
    // Stance of set_stance commands.
    Stance stance{Stance::aggressive};
    // Entities of spawn commands.
    std::vector<EntitySpawn> spawns;
    // set_invulnerable, set_cannot_be_killed, suspend_locomotor: on or off.
    bool flag{};
    // damage: the hull damage.
    float amount{};
    // teleport: the heading to face (radians), or the entity's own.
    std::optional<float> heading;
    // The order this command carries out, numbered by whoever issued it (Battle; 0 for none). Each
    // entity a move, stop, attack, or formation move applies to remembers it as its
    // EntitySnapshot::command_serial, so the issuer can tell whether its order is still the one the
    // entity follows. Bookkeeping, not part of the state hash.
    std::uint64_t serial{};
};

// Snapshot of one entity after a tick.
// How far off a body-aiming entity's heading its target may be for it to fire (20 degrees, after
// the stock Crouch_Move_Fire_Angle_Cutoff; an engine choice).
inline constexpr float body_aim_tolerance = 0.349066F;

struct EntitySnapshot
{
    EntityId id;
    Vector3 position;
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
    // Flyers: nose angle above the horizontal (radians), and how fast the heading turned in the last
    // tick (radians per second, counter-clockwise positive; presentation banks by it). Zero for
    // everything else.
    float pitch{};
    float yaw_rate{};
    // How fast the entity actually moved in the last tick (world units per second, in 3D, including
    // separation pushes); zero for a spawn's first tick. Derived from positions, so not part of the
    // state hash.
    float speed{};
    // Flyers: roll left from a half-loop that has not unwound yet (radians; presentation adds it to
    // the bank). Zero otherwise.
    float roll{};
    // Tick in which the entity last launched a shot (0: never); for animation and effects, not part
    // of the state hash.
    std::uint64_t last_shot_tick{};
    // SimCommand::serial of the last move, stop, attack, formation move, attack-move, or guard
    // applied to the entity (0: none yet); not part of the state hash.
    std::uint64_t command_serial{};
    // The standing order the entity follows; none once an attack-move arrived or a guarded object died.
    StandingOrder order{StandingOrder::none};
};

struct ProjectileSnapshot
{
    Vector3 position;
    // Unit direction of flight.
    Vector3 direction;
    std::uint32_t owner{};
    // Presentation only (not hashed): the weapon's projectile visual key, and the speed in world
    // units per second (to place the shot between ticks).
    std::uint32_t visual{};
    float speed{};
    // Presentation only (not hashed): the shot's number in launch order, the same in every snapshot
    // while it flies (effects that follow a shot, such as a missile's trail, are keyed by it).
    std::uint32_t serial{};
    // Presentation only (not hashed): how far it has flown and how far its aim point is, for drawn
    // flight shapes (grenade arcs, rocket sways).
    float flown{};
    float remaining{};
};

// An entity combat destroyed in the last tick, where and how it last stood (for death animations
// and effects; a derived output, not part of the state hash).
struct DeathSnapshot
{
    EntityId id;
    Vector3 position;
    float heading{};
    float pitch{};
    std::uint32_t visual{};
    std::uint32_t owner{};
    // Presentation key of the effect its death plays (CombatProfile::death_effect), or 0.
    std::uint32_t death_effect{};
    // Its type (CombatProfile::type_key), for what its death leaves (survivors).
    std::uint32_t type_key{};
    // Presentation (not hashed): it was run over (its crushed death clip).
    bool crushed{};
};

// What a shot did when it ended.
enum class ImpactKind : std::uint8_t
{
    // Shields took all of it.
    shield,
    // It reached the hull (or a hardpoint).
    hull,
    // It reached the hull, but armor reduced it (provisional: an armor modifier below 1).
    hull_reduced,
    // It flew its maximum distance without hitting anything.
    expired,
};

// A shot that ended in the last tick, for impact effects (a derived output, not part of the state
// hash). Instant weapons report their hits too.
struct ImpactSnapshot
{
    Vector3 position;
    // Unit direction the shot was flying.
    Vector3 direction;
    // The weapon's projectile visual key (WeaponProfile::projectile_visual).
    std::uint32_t visual{};
    ImpactKind kind{ImpactKind::hull};
};

// A shot launched in the last tick (each pulse of each weapon), for fire sounds and muzzle effects
// (a derived output, not part of the state hash).
struct ShotSnapshot
{
    EntityId shooter;
    Vector3 origin;
    // The weapon's WeaponProfile::fire_sound and launch_effect, or 0.
    std::uint32_t sound{};
    std::uint32_t effect{};
};

// An entity that began spinning away in the last tick (CombatProfile::spin_chance), for its sound
// (a derived output, not part of the state hash).
struct SpinSnapshot
{
    EntityId id;
    Vector3 position;
};

// A hardpoint destroyed in the last tick, for its destruction sound and effects (a derived output,
// not part of the state hash).
struct HardpointDeathSnapshot
{
    // The entity it belongs to and its index in the entity's CombatProfile::hardpoints.
    EntityId entity;
    std::uint32_t hardpoint{};
    Vector3 position;
    // HardpointProfile::death_sound, death_effect, and type_key, or 0.
    std::uint32_t sound{};
    std::uint32_t effect{};
    std::uint32_t type_key{};
};

struct SimSnapshot
{
    std::uint64_t tick{};
    std::vector<EntitySnapshot> entities;
    std::vector<ProjectileSnapshot> projectiles;
    // Entities killed in this tick, in removal order (destroy commands are not deaths).
    std::vector<DeathSnapshot> deaths;
    // Shots that ended in this tick, in the order they ended.
    std::vector<ImpactSnapshot> impacts;
    // Shots launched in this tick, in launch order.
    std::vector<ShotSnapshot> shots;
    // Hardpoints destroyed in this tick, in the order they fell.
    std::vector<HardpointDeathSnapshot> hardpoint_deaths;
    std::vector<SpinSnapshot> spins;
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
        // Entities run over by crushers.
        std::uint64_t crushed{};
        std::size_t projectiles{};
        // Overlapping ground pairs pushed apart in the last tick.
        std::size_t separated_pairs{};
        // Group members waiting for the group in the last tick, and members released from their
        // group because they stopped making progress.
        std::size_t waiting_members{};
        std::uint64_t released_members{};
        // Entities of spawn commands the world rejected (invalid data or full storage).
        std::uint64_t rejected_spawns{};
    };

    explicit SimulationWorld(double tick_seconds = 1.0 / 30.0);

    // Spawning is immediate and intended for setup before the first tick (map load); during a battle
    // spawns go through spawn commands, so they apply at a tick boundary like every order. Spawn
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
    // entities target the nearest damageable hostile within range on their own (unless their stance
    // is hold_fire); an attack command names the target instead.
    void set_damage_table(DamageTable table);
    void set_hostility(const Hostility& hostility) noexcept;
    [[nodiscard]] const Hostility& hostility() const noexcept;

    // Queues a command for the next step(); commands apply in submission order.
    void submit(SimCommand command);
    // Applies queued commands, runs the systems once, and publishes a new snapshot.
    void step();

    [[nodiscard]] bool alive(EntityId id) const noexcept;
    // XML type name an entity was spawned with (empty when it is not alive).
    [[nodiscard]] std::string_view type_name(EntityId id) const noexcept;
    // The points an entity is still moving through, in order, ending at its destination (empty
    // when it is not moving or unknown); a ship's avoidance detour comes first. For debugging and
    // presentation; not part of snapshots.
    [[nodiscard]] std::vector<Vector3> route(EntityId id) const;
    [[nodiscard]] const SimSnapshot& snapshot() const noexcept;
    [[nodiscard]] Stats stats() const noexcept;
    // FNV-1a over every gameplay component, in storage order: equal hashes mean identical simulation
    // state. Presentation keys (EntitySpawn::visual) and derived outputs (snapshot speed) are left
    // out, so how a scene was built for drawing never changes a battle's hash.
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
            // Shots fired (alternates the muzzles of a weapon with two).
            std::uint32_t shots{};
        };
        std::vector<WeaponState> weapons;
        std::vector<float> hardpoint_health;
        // Ticks until the next automatic target search, and until an ordered chase replans.
        std::uint32_t retarget_timer{};
        std::uint32_t chase_timer{};
        // Ticks on the current automatic target (for CombatProfile::stickiness_seconds).
        std::uint32_t target_ticks{};
        // Ticks left of a spin away (CombatProfile::spin_chance); 0 when not spinning.
        std::uint32_t spin_ticks{};
        // Presentation (not hashed): the killing blow was a crushing (DeathSnapshot::crushed).
        bool crushed{};
        // Scripted conditions: no damage at all, and health that damage cannot take below the floor.
        bool invulnerable{};
        bool cannot_be_killed{};
    };
    struct Projectile
    {
        Vector3 position;
        Vector3 aim;
        Vector3 direction;
        EntityId target;
        // The victim's hardpoint the shot aims at, or -1 for its hull.
        std::int32_t target_hardpoint{-1};
        std::uint32_t owner{};
        float damage{};
        std::uint16_t damage_type{};
        bool damages_shield{};
        bool damages_health{};
        AreaBlast blast;
        float speed{};
        float flown{};
        // Ticks left (WeaponProfile::max_lifetime; 0: none) and the reacquisition range.
        std::uint32_t lifetime_ticks{};
        float scan_range{};
        float max_flight{};
        // Where the shot lands relative to its target's aim point, and whether that is outside the
        // target (WeaponProfile::inaccuracy).
        Vector3 stray;
        bool misses{};
        // Presentation key (WeaponProfile::projectile_visual) and launch number; not hashed.
        std::uint32_t visual{};
        std::uint32_t serial{};
    };

    // What the behavior layer asks a locomotor for this tick. Ground and straight-line movers follow
    // their move target and route (navigation); flyers steer at `point`, or hold their heading.
    struct SteeringIntent
    {
        std::optional<Vector3> point;
        bool hold_heading{};
        // Flyers: reverse through a vertical half-loop (the locomotor finishes it once started).
        bool loop{};
    };

    [[nodiscard]] std::optional<std::size_t> dense_index(EntityId id) const noexcept;
    // Behavior layer (simulation_behavior.cpp): orders from commands, then each tick what every
    // unit wants from its order, stance, and target, as a move request or a steering intent.
    void apply(const SimCommand& command);
    void run_behavior();
    // An attack order's victim when it is still a valid one.
    [[nodiscard]] std::optional<std::size_t> attack_victim(std::size_t index) const noexcept;
    // Ground and straight-line movers: chase an ordered target until in range, then hold.
    void pursue_ordered_target(std::size_t index);
    // Attack-move and guard: when to engage (a scan for hostiles), when to break off, and where to
    // go; the chase itself is the ordered-attack pursuit.
    void run_standing_order(std::size_t index);
    // Ends an engagement the standing order started (the target and its order are dropped).
    void disengage(std::size_t index);
    // Hull damage after shields and armor, under the scripted conditions (invulnerable: none; cannot
    // be killed: not below the floor); returns what the hull took.
    float wound(std::size_t index, float damage);
    void teleport(std::size_t index, const Vector3& point, std::optional<float> heading);
    // Flyers: steer at the target (attack runs, or dogfights against flyers), the destination, or
    // the point they circle; reverse through a half-loop when that point is far behind.
    void plan_flight(std::size_t index, float dt);
    void destroy(std::size_t dense);
    void run_movement();
    // Requests a route to `target` (queued for the search budget when it needs a search).
    void plan_move(std::size_t index, const Vector3& target);
    // Plans now: a full search from the entity's position.
    void plan_route_now(std::size_t index, const Vector3& target);
    // Sets an entity's route from planned waypoints (the last is its destination).
    void set_route(std::size_t index, const std::vector<PathPoint>& waypoints, float height);
    // Works through queued path requests until the tick's search budget is spent.
    void process_path_queue();
    struct PathRequest;
    // Plans a shared request's members until the tick's budget is spent; false when members remain.
    [[nodiscard]] bool plan_shared_route(PathRequest& request, std::uint64_t spent_before, bool first_in_tick);
    void plan_shared_member(const PathRequest& request, std::size_t index, const Vector3& goal);
    void apply_formation_move(const SimCommand& command);
    // Attack-move and guard commands: their standing orders, and the first move.
    void apply_standing_order(const SimCommand& command);
    // Remaining route length of every moving entity (zero for the rest).
    void measure_remaining_routes();
    // Marks group members that are ahead of their group to wait this tick, and releases members
    // that stopped making progress.
    void update_group_cohesion();
    // Speed this tick: the entity's own, or its group's when that is slower.
    [[nodiscard]] float travel_speed(std::size_t index) const noexcept;
    // Locomotors (simulation_locomotion.cpp): physical movement only.
    void move_over_ground(std::size_t index, float dt);
    // Never below the minimum speed; turns and pitches toward the intent's point at the turn rate
    // (pitch limited to flight_max_pitch) and moves along the nose.
    void fly(std::size_t index, float dt);
    // Turns toward `desired` (radians) at the entity's turn rate (its stopped turn rate when moving
    // slower than its profile's turn speed) and returns how much of its speed it may use: 1 when
    // facing the way, falling to 0 at 90 degrees or more off.
    [[nodiscard]] float steer(std::size_t index, float desired, float dt);
    // The speed a mover wants this tick, `distance` from its destination, after steer returned
    // `facing`: the travel speed shaped by its movement profile (turning in place, keeping speed in
    // a turn, braking ahead of the destination).
    [[nodiscard]] float wanted_speed(std::size_t index, float facing, float distance) const noexcept;
    // Speeds up or slows down toward `wanted` at the profile's rates; returns this tick's speed.
    float drive(std::size_t index, float wanted, float dt) noexcept;
    // Movers that cannot turn while slow move along their heading, not straight at their target,
    // until within settle_distance of it.
    [[nodiscard]] bool moves_along_heading(std::size_t index) const noexcept;
    [[nodiscard]] float settle_distance(std::size_t index) const noexcept;
    void run_separation();
    // Crushing (simulation_locomotion.cpp): after movement, before separation pushes the victims out.
    void run_crushing();
    // Navigation (simulation_navigation.cpp): local avoidance of space ships (detours_).
    void plan_space_avoidance();
    // A space ship's step without the part that would take it deeper into a ship it touches (both
    // radii), so ships slide along each other instead of overlapping; other steps unchanged.
    [[nodiscard]] Vector3 keep_apart(std::size_t index, Vector3 step);
    // Stamps (or removes) an entity's footprint into the ground's passability.
    void stamp_footprint(std::size_t index, bool add);
    void run_combat();
    void update_target(std::size_t index);
    void run_weapon(std::size_t index);
    void launch(std::size_t shooter, std::size_t victim, const WeaponProfile& weapon, const LocalPoint& mount);
    // Returns what the shot did (shields only, hull, or hull with armor reducing it).
    ImpactKind apply_damage(std::size_t victim, float damage, std::uint16_t damage_type, bool damages_shield, bool damages_health, std::int32_t hardpoint);
    // A point of an entity's own frame in world space.
    [[nodiscard]] Vector3 world_point(std::size_t index, const LocalPoint& point) const noexcept;
    [[nodiscard]] bool hardpoint_alive(std::size_t index, std::size_t hardpoint) const noexcept;
    // Whether any hardpoint of the kind survives (true when the entity has none of that kind).
    [[nodiscard]] bool keeps(std::size_t index, HardpointProfile::Kind kind) const noexcept;
    // The victim's live targetable hardpoint nearest to `from`, or -1.
    [[nodiscard]] std::int32_t aim_hardpoint(std::size_t victim, const Vector3& from) const noexcept;
    [[nodiscard]] Vector3 aim_point(std::size_t victim, std::int32_t hardpoint) const noexcept;
    // Where a shot of `weapon` at `victim` lands relative to its aim point (WeaponProfile::inaccuracy;
    // draws from combat_random_), and whether that misses the victim.
    [[nodiscard]] Vector3 stray(const WeaponProfile& weapon, std::size_t victim);
    [[nodiscard]] bool strays_off(std::size_t victim, const Vector3& stray) const noexcept;
    // The entity a stray shot of `owner` landing at `point` hits instead of its target: the nearest
    // live, damageable, collidable entity hostile to `owner` whose footprint holds the point.
    [[nodiscard]] std::optional<std::size_t> stray_victim(const Vector3& point, std::uint32_t owner, std::size_t target) const noexcept;
    // The radius strays_off measures against (0: unknown size).
    [[nodiscard]] float target_size(std::size_t index) const noexcept;
    // Damages the hostiles of `owner` around `point` (AreaBlast).
    void area_blast(const Vector3& point, std::uint32_t owner, const AreaBlast& blast, std::uint16_t damage_type, bool damages_shield, bool damages_health);
    // Whether one of `index`'s weapons may fire at `candidate`'s categories (or it has none).
    [[nodiscard]] bool engages(std::size_t index, std::size_t candidate) const noexcept;
    // The target `index` prefers among the damageable hostiles within `reach` of `center` (in the
    // ground plane) that `accepts`: the lowest rank of its targeting priorities, then the nearest,
    // then the lower entity id. Defined in simulation_internal.hpp.
    template <typename Accepts> [[nodiscard]] std::optional<std::size_t> choose_target(std::size_t index, const Vector3& center, float reach, const Accepts& accepts);
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
    std::vector<Vector3> positions_;
    std::vector<float> headings_;
    std::vector<float> max_speeds_;
    std::vector<std::optional<Vector3>> move_targets_;
    std::vector<std::uint32_t> owners_;
    std::vector<std::uint32_t> visuals_;
    std::vector<std::uint8_t> selectable_;
    std::vector<std::string> type_names_;
    std::vector<std::uint8_t> follows_ground_;
    GroundMap ground_;
    std::vector<std::uint32_t> passable_types_;
    // Waypoints before the move target, last first (back() is the next one to reach).
    std::vector<std::vector<Vector3>> paths_;
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
    std::vector<std::optional<Vector3>> loiter_points_;
    // Flyers: current pitch, the height of the plane they cruise in (their spawn height), and the
    // heading change of the last tick per second.
    std::vector<float> pitches_;
    std::vector<float> cruise_heights_;
    std::vector<float> yaw_rates_;
    // Speed of the last tick (EntitySnapshot::speed), and positions before the tick's movement.
    std::vector<float> speeds_;
    std::vector<std::uint64_t> last_shot_ticks_;
    std::vector<std::uint64_t> command_serials_;
    // Standing orders (gameplay state, hashed): the kind, the destination or anchor point and object
    // (invalid: the point), whether the current target came from the order and where that chase
    // began, and the ticks to the next scan for hostiles (staggered by entity index).
    struct StandingOrderState
    {
        StandingOrder kind{StandingOrder::none};
        Vector3 point;
        EntityId entity;
        bool engaging{};
        Vector3 engage_origin;
        std::uint32_t scan_timer{};
    };
    std::vector<StandingOrderState> standing_orders_;
    // Suspend_Locomotor: the entity neither moves nor turns (hashed).
    std::vector<std::uint8_t> locomotor_suspended_;
    std::vector<Vector3> tick_start_positions_;
    // Flyers inside a half-loop, and the roll they unwind after one.
    std::vector<std::uint8_t> looping_;
    std::vector<float> rolls_;
    std::vector<Stance> stances_;
    std::vector<std::uint8_t> aims_with_body_;
    // Rebuilt by the behavior layer every tick before movement.
    std::vector<SteeringIntent> intents_;
    std::vector<std::uint32_t> formation_orders_;
    std::vector<float> formation_spacings_;
    std::vector<std::uint8_t> formation_solos_;
    std::vector<FormationTeam> formation_teams_;
    std::vector<CrushProfile> crushes_;
    // Group speed of a formation move until the entity arrives; zero when none.
    std::vector<float> speed_limits_;
    std::vector<MovementProfile> movements_;
    // The locomotor's own speed, which accelerates and brakes (speeds_ is the measured one).
    std::vector<float> drive_speeds_;
    std::vector<Vector3> separation_push_;
    // Space ships: the point steered at this tick to pass a unit in the way (navigation's local
    // avoidance); recomputed every tick from the state, so not hashed.
    std::vector<std::optional<Vector3>> detours_;
    // A queued route request: one entity, or the ground members of a formation move that share a
    // movement class (planned on one shared path to `group_target`).
    struct PathRequest
    {
        std::vector<EntityId> members;
        std::vector<Vector3> goals;
        std::vector<std::uint32_t> serials;
        Vector3 group_target;
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
    // Deaths of the current tick (SimSnapshot::deaths).
    std::vector<DeathSnapshot> deaths_;
    std::vector<ImpactSnapshot> impacts_;
    std::vector<ShotSnapshot> shots_;
    std::vector<HardpointDeathSnapshot> hardpoint_deaths_;
    std::vector<SpinSnapshot> spins_;
    // Where shots stray (WeaponProfile::inaccuracy); part of the state.
    Pcg32 combat_random_{combat_random_seed};
    // Launch number of the next shot (ProjectileSnapshot::serial; presentation only, not hashed).
    std::uint32_t next_projectile_serial_{1};
    SpatialGrid grid_;
    std::vector<std::uint32_t> query_;
    // Space avoidance's scratch: a ship's neighbours, and the largest ship radius of the tick's
    // plan (0 when no ship steers; keep_apart then has nothing to do).
    std::vector<std::uint32_t> neighbours_;
    float largest_ship_radius_{};
    DamageTable damage_table_;
    Hostility hostility_;
    std::uint64_t shots_fired_{};
    std::uint64_t hits_{};
    std::uint64_t kills_{};
    std::uint64_t hardpoints_destroyed_{};
    std::uint64_t crushed_{};
    std::vector<SimCommand> pending_;
    SimSnapshot snapshot_;
    std::uint64_t commands_applied_{};
    std::uint64_t stale_references_{};
    std::uint64_t rejected_spawns_{};
};

// Presentation helper: blends two consecutive snapshots by `alpha` (0 = previous, 1 = current).
// Entities missing from `previous` (just spawned) use their current state.
[[nodiscard]] std::vector<EntitySnapshot> interpolate_snapshots(const SimSnapshot& previous, const SimSnapshot& current, float alpha);

} // namespace eaw
