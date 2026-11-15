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
#include "simulation/combat.hpp"
#include "simulation/component_pool.hpp"
#include "simulation/formation.hpp"
#include "simulation/ground_map.hpp"
#include "simulation/orca.hpp"
#include "simulation/pathfinding.hpp"
#include "simulation/simulation_types.hpp"
#include "simulation/spatial_grid.hpp"
#include "simulation/system_view.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <vector>

namespace eaw {

// The ground an immobile obstacle occupies: a rectangle (half extents along and across its heading)
// whose cells take `passability` while the entity exists (a wall's own type, so classes allowed on
// that type still cross it, or Obstacle).
struct GroundFootprint
{
    bool operator==(const GroundFootprint&) const = default;
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
    bool operator==(const CrushProfile&) const = default;
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
    bool operator==(const MovementProfile&) const = default;
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
    // Turn rate (radians per second) while the entity has no move order and turns in place (space
    // ships facing their target); zero turns at its turn rate.
    float turn_in_place_rate{};
    // Turns in place played as clips (animation-led walkers; seconds at the clips' playing rate, left
    // [0] and right [1]): winding up over BEGIN, turning through whole cycles (loops, then at most
    // one HALF and one QUARTER cycle, standing for a half and a quarter of the loop's angle: an
    // inference from their names), and winding down over END. The cycles are the ones that come
    // closest to the turn at stopped_turn_rate, and every rate is scaled so the turn ends exactly
    // on the way, so each clip starts where the last one ends. Zero turn_begin_seconds turns in
    // place at a steady rate; zero HALF or QUARTER seconds composes the turn without them.
    std::array<float, 2> turn_begin_seconds{};
    std::array<float, 2> turn_loop_seconds{};
    std::array<float, 2> turn_end_seconds{};
    std::array<float, 2> turn_half_seconds{};
    std::array<float, 2> turn_quarter_seconds{};
    // Walks played as clips (animation-led walkers with MOVESTART, MOVE, and MOVE_END clips; seconds
    // at the clips' playing rate at top speed): the walker speeds up over walk_start_seconds, its
    // stride (one MOVE loop) covers top speed times walk_stride_seconds, and it stops only where a
    // MOVE_END clip starts, at a whole walk_stop_parts-th of its stride, braking to rest over
    // walk_stop_seconds; nothing turns or sets off before that stop ends. Zero walk_stop_parts
    // changes speed by acceleration and deceleration instead.
    float walk_start_seconds{};
    float walk_stride_seconds{};
    float walk_stop_seconds{};
    std::uint32_t walk_stop_parts{};
    // A walker that raises a leg to walk and turn (the pod walker's IDLE_LEGLIFT and IDLE_LEGDROP:
    // its walk and turn clips start and end with its middle leg raised, IDLE with it down): it
    // raises the leg over walk_raise_seconds before it sets off or turns from standing, and lowers
    // it over walk_lower_seconds once it stands with nothing to do. Zero: no such leg.
    float walk_raise_seconds{};
    float walk_lower_seconds{};
};

// The most MOVE_END clips a walk played as clips stops by (MOVE_ENDONE..MOVE_ENDFOUR, the stock names).
inline constexpr std::uint32_t max_walk_stop_parts = 4;

// What the state hash covers, by subject: SimulationWorld::state_hashes hashes each apart (the total
// is unchanged), so a determinism failure can be traced to the part of the state that differs first.
enum class StateSubject : std::uint8_t
{
    bodies,      // ids, owners, footprints, movement profiles, positions, headings, speeds, move targets
    routes,      // paths, pending and queued route searches, group progress
    combat,      // health, shields, targets, weapons, turrets, stances
    clips,       // turns and walks played as clips
    flight,      // pitch, cruise height, loops, rolls, loiter points
    formations,  // formation orders, spacing, teams
    orders,      // standing orders
    projectiles, // projectiles in flight
    random,      // the combat random stream
    count,
};

inline constexpr std::size_t state_subject_count = static_cast<std::size_t>(StateSubject::count);
inline constexpr std::array<std::string_view, state_subject_count> state_subject_names{"bodies", "routes", "combat", "clips", "flight", "formations", "orders", "projectiles", "random"};
static_assert(!state_subject_names.back().empty(), "every subject needs its name");
using StateHashes = std::array<std::uint64_t, state_subject_count>;

namespace simulation_detail {
struct StateHasher;
}

// The tick's layers, in SimulationWorld::step's order, and seconds per layer (Stats::phase_seconds).
enum class TickPhase : std::uint8_t
{
    commands,
    behavior,
    navigation,
    movement,
    contact,
    combat,
    resolution,
    publish,
    count,
};

inline constexpr std::size_t tick_phase_count = static_cast<std::size_t>(TickPhase::count);
inline constexpr std::array<std::string_view, tick_phase_count> tick_phase_names{"commands", "behavior", "navigation", "movement", "contact", "combat", "resolution", "publish"};
static_assert(!tick_phase_names.back().empty(), "every layer needs its name");
using TickPhaseTimes = std::array<double, tick_phase_count>;

// Which turn clip a turn in place played as clips (MovementProfile::turn_begin_seconds) is in.
enum class TurnClip : std::uint8_t
{
    none,
    begin,
    loop,
    half,
    quarter,
    end,
};

// Which walk clip a walk played as clips (MovementProfile::walk_stop_parts) is in: unplanned for
// every other entity.
enum class WalkClip : std::uint8_t
{
    unplanned,
    standing,
    starting,
    moving,
    stopping,
    // A walker that raises a leg to walk and turn (MovementProfile::walk_raise_seconds): raising
    // it, standing with it raised, and lowering it.
    raising,
    raised,
    lowering,
};

// Everything needed to create an entity. `type_name` is the XML object type (informational for
// now); `visual` is an opaque presentation key chosen by the caller (for example a mesh index).
// What a type gives every entity of it (docs/ecs-design.md, 6.2 and decision 6): registered once
// with SimulationWorld::add_recipe, which keeps identical recipes as one, and shared by the
// entities spawned from it. Entities of one type that must differ (a company member taking its
// team's speed, a placement scaled differently) are spawned from a recipe of their own.
struct EntityRecipe
{
    bool operator==(const EntityRecipe&) const = default;
    std::string type_name;
    // World units per second; zero means the entity cannot move.
    float max_speed{};
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
    CrushProfile crush;
    // Flight (fighters): above zero the entity never stops. It flies along its heading at no less than
    // this speed, steering toward its target (or its destination, which it circles on arrival) at
    // its turn rate, so attack runs overshoot and come back.
    float min_speed{};
    // Aims with its body instead of a turret (infantry): standing, it turns to face its target, and
    // it fires only while standing and facing the target within body_aim_tolerance.
    bool aims_with_body{};
    // A team (XML Create_Team_Type containers: TEAM, SELECTABLE, UNIT_AI behaviors): an entity
    // without a body that takes its squad's orders and plans its route, its members (spawned with
    // EntitySpawn::squad) standing in this arrangement around it (docs/pathfinding-design.md, 4.3).
    std::optional<TeamArrangement> team;
};

// A recipe's place in the world's recipe store, in registration order.
using RecipeId = std::uint32_t;

// One entity to spawn: its recipe and what differs between entities of it.
struct EntitySpawn
{
    bool operator==(const EntitySpawn&) const = default;
    RecipeId recipe{};
    std::uint32_t owner{};
    Vector3 position;
    // Rotation about +Z in radians.
    float heading{};
    std::uint32_t visual{};
    bool selectable{true};
    // The company team it stands with in formations (formation.hpp).
    FormationTeam formation_team;
    // Ground occupied while the entity exists (structures, walls, obstacle props), placed.
    std::optional<GroundFootprint> footprint;
    Stance stance{Stance::aggressive};
    // A squad member: the index of its team's spawn in the same list (a spawn command's spawns, or a
    // battle's map entities). Whoever spawns the list spawns teams before their members and passes
    // the team's id to spawn().
    std::optional<std::uint32_t> squad;
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
    // Move the mobile entities as one formation: on land a frame they keep their slots in on the
    // way (docs/pathfinding-design.md, 4.3); in space each to its slot at the slowest member's speed,
    // members ahead waiting for the rest. Clears attack orders like move.
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
    // The default group move (the owner, 2026-11-15): each mobile entity from where it stands to its
    // own slot of a formation centered on `target`, on a route of its own, all at the slowest
    // member's speed; members with XML FormationGrouping Solo at their own speed to the point.
    group_move,
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
    bool operator==(const SimCommand&) const = default;
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
// How far off its victim a turret may point and still fire (5 degrees; an engine choice).
inline constexpr float turret_aim_tolerance = 0.0872665F;

struct EntitySnapshot
{
    bool operator==(const EntitySnapshot&) const = default;
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
    // A squad member's team (invalid otherwise). A team has no body and no visual; its position is
    // where its members stand.
    EntityId squad;
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
    // separation's pushes of flyers); zero for a spawn's first tick. Derived from positions, so not part of the
    // state hash.
    float speed{};
    // Flyers: roll left from a half-loop that has not unwound yet (radians; presentation adds it to
    // the bank). Zero otherwise.
    float roll{};
    // A turreted entity's turret direction relative to its facing (CombatProfile::turret; radians,
    // counter-clockwise); zero otherwise.
    float turret_yaw{};
    // Tick in which the entity last launched a shot (0: never); for animation and effects, not part
    // of the state hash.
    std::uint64_t last_shot_tick{};
    // SimCommand::serial of the last move, stop, attack, formation move, attack-move, or guard
    // applied to the entity (0: none yet); not part of the state hash.
    std::uint64_t command_serial{};
    // The standing order the entity follows; none once an attack-move arrived or a guarded object died.
    StandingOrder order{StandingOrder::none};
    // Braking to stop at its destination in the last tick (its deceleration, not its top speed or a
    // turn, held its speed back); presentation starts a walker's stop clip then. Not part of the
    // state hash.
    bool arriving{};
    // A turn in place played as clips (MovementProfile::turn_begin_seconds): its clip (none between
    // turns), whether it turns left, and how far into the clip it is (0..1).
    TurnClip turn_clip{TurnClip::none};
    bool turn_left{};
    float turn_progress{};
    // Its turns in place are played as clips at all (presentation leaves its turns to turn_clip).
    bool turns_as_clips{};
    // A walk played as clips: the clip, the MOVE_END clip a stop plays (0 for MOVE_ENDONE), and how far
    // into the clip it is (0..1; the stride phase while moving).
    WalkClip walk_clip{WalkClip::unplanned};
    std::uint32_t walk_part{};
    float walk_progress{};
};

struct ProjectileSnapshot
{
    bool operator==(const ProjectileSnapshot&) const = default;
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
    bool operator==(const DeathSnapshot&) const = default;
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
    bool operator==(const ImpactSnapshot&) const = default;
    Vector3 position;
    // Unit direction the shot was flying.
    Vector3 direction;
    // The weapon's projectile visual key (WeaponProfile::projectile_visual).
    std::uint32_t visual{};
    ImpactKind kind{ImpactKind::hull};
    // The entity the shot struck (invalid when it struck nothing).
    EntityId target;
};

// A shot launched in the last tick (each pulse of each weapon), for fire sounds and muzzle effects
// (a derived output, not part of the state hash).
struct ShotSnapshot
{
    bool operator==(const ShotSnapshot&) const = default;
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
    bool operator==(const SpinSnapshot&) const = default;
    EntityId id;
    Vector3 position;
};

// A hardpoint destroyed in the last tick, for its destruction sound and effects (a derived output,
// not part of the state hash).
struct HardpointDeathSnapshot
{
    bool operator==(const HardpointDeathSnapshot&) const = default;
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
    bool operator==(const SimSnapshot&) const = default;
    std::uint64_t tick{};
    std::vector<EntitySnapshot> entities;
    std::vector<ProjectileSnapshot> projectiles;
    // Entities killed in this tick, in dense order within each round of deaths (destroy commands are
    // not deaths).
    std::vector<DeathSnapshot> deaths;
    // Shots that ended in this tick, in the order they ended.
    std::vector<ImpactSnapshot> impacts;
    // Shots launched in this tick, in launch order.
    std::vector<ShotSnapshot> shots;
    // Hardpoints destroyed in this tick, in the order they fell.
    std::vector<HardpointDeathSnapshot> hardpoint_deaths;
    std::vector<SpinSnapshot> spins;
};

// Simulation ticks per second of real time: 60 (the owner, 2026-11-15: never below 60 frames per
// second, and the simulation steps as often; it was 30 Hz, an engine choice). The original's rate
// is not measured; its XML units per 30 Hz frame are converted apart (assets/legacy_units.hpp).
inline constexpr double default_ticks_per_second = 60.0;

// Converts real elapsed time into whole simulation ticks. At most `max_ticks_per_advance` ticks are
// returned per call (excess time is dropped), so a long stall cannot trigger an ever-growing
// backlog of ticks.
class FixedStepClock final
{
public:
    explicit FixedStepClock(double ticks_per_second = default_ticks_per_second, std::uint32_t max_ticks_per_advance = 8);

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
        // Wall-clock seconds each tick layer took, over all ticks so far and in its slowest tick
        // (measurements, not simulation state: never hashed, never read by the simulation).
        TickPhaseTimes phase_seconds{};
        TickPhaseTimes slowest_phase_seconds{};
    };

    explicit SimulationWorld(double tick_seconds = 1.0 / default_ticks_per_second);

    // Spawning is immediate and intended for setup before the first tick (map load); during a battle
    // spawns go through spawn commands, so they apply at a tick boundary like every order. Spawn
    // data comes from map and mod content: a non-finite position, heading, or speed, or full entity
    // storage, returns an error and spawns nothing.
    // Registers a recipe (validated) and returns its id; a recipe equal to one already registered
    // returns that one's id.
    [[nodiscard]] Result<RecipeId> add_recipe(EntityRecipe recipe);
    [[nodiscard]] const EntityRecipe& recipe(RecipeId id) const;
    [[nodiscard]] std::size_t recipe_count() const noexcept;
    // Builds the clearance of a movement class for footprints up to `radius`, and the chunk graph of its
    // size bucket, now (a battle does it as its map loads for every type it can spawn), so no later
    // tick builds them whole.
    void reserve_clearance(std::uint32_t passable_types, float radius);
    // `squad`: the team a squad member joins (the id spawn.squad's list index resolved to).
    [[nodiscard]] Result<EntityId> spawn(const EntitySpawn& spawn, EntityId squad = {});
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
    // The same fields as state_hash, one hash per subject (StateSubject); costs as much as state_hash.
    [[nodiscard]] StateHashes state_hashes() const noexcept;

private:
    struct CombatState
    {
        bool operator==(const CombatState&) const = default;
        // The recipe's profile (shared by the entities of the recipe; recipes never change).
        const CombatProfile* profile{};
        float health{};
        float shield{};
        EntityId target;
        // The target came from an attack command: chase it out of range instead of dropping it.
        bool ordered{};
        // Per weapon: ticks until the next burst may start, shots left in the burst, and ticks until
        // the burst's next shot.
        struct WeaponState
        {
            bool operator==(const WeaponState&) const = default;
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
        // The turret's direction relative to the hull's facing (radians, counter-clockwise).
        float turret_yaw{};
        // Scripted conditions: no damage at all, and health that damage cannot take below the floor.
        bool invulnerable{};
        bool cannot_be_killed{};
        // The stance (spawned with, then set by commands), whether it turns its body to aim
        // (EntityRecipe::aims_with_body), and the tick it last fired in (presentation reads it).
        Stance stance{Stance::aggressive};
        bool aims_with_body{};
        std::uint64_t last_shot_tick{};
        // An attack order on a team: the entity fires at any of the team's members (the owner,
        // 2026-11-15), the nearest when it picks.
        EntityId ordered_squad;
    };
    struct Projectile
    {
        bool operator==(const Projectile&) const = default;
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
        bool operator==(const SteeringIntent&) const = default;
        std::optional<Vector3> point;
        bool hold_heading{};
        // Flyers: reverse through a vertical half-loop (the locomotor finishes it once started).
        bool loop{};
    };

    // A side effect of the tick's fighting (docs/ecs-design.md, 6.9 rule 4): queued by the systems
    // that cause it and applied by the resolution system in queue order after combat, so no system
    // sees another's damage within the tick (two shots at one target both land; the second strikes
    // the body, the owner's rule). A shot's hit (on `victim`, with its impact), a crush, a blast
    // (`blast` of `owner` at `point`), or a shot that struck nothing (its impact only).
    struct DamageEvent
    {
        bool operator==(const DamageEvent&) const = default;
        enum class Kind : std::uint8_t
        {
            hit,
            crush,
            blast,
            miss,
        };
        Kind kind{Kind::hit};
        EntityId victim;
        float damage{};
        std::uint16_t damage_type{};
        bool damages_shield{};
        bool damages_health{};
        std::int32_t hardpoint{-1};
        // A shot's impact: where, which way it flew, and its visual.
        Vector3 point;
        Vector3 direction;
        std::uint32_t visual{};
        std::uint32_t owner{};
        AreaBlast blast;
    };

    template <class Data> [[nodiscard]] static std::optional<std::size_t> dense_index(Data& data, EntityId id) noexcept;
    // Behavior layer (simulation_behavior.cpp): orders from commands, then each tick what every
    // unit wants from its order, stance, and target, as a move request or a steering intent.
    void apply(const SimCommand& command);
    // An attack order's victim when it is still a valid one.
    template <class Data> [[nodiscard]] static std::optional<std::size_t> attack_victim(Data& data, std::size_t index) noexcept;
    // Hull damage after shields and armor, under the scripted conditions (invulnerable: none; cannot
    // be killed: not below the floor); returns what the hull took.
    static float wound(CombatState& combat, float damage);
    void teleport(std::size_t index, const Vector3& point, std::optional<float> heading);
    // Removes the entities at the dense indices in `dense` (any order; repeats allowed), keeping the
    // others' order: the dense order stays the order entities were spawned in.
    void remove_entities(std::vector<std::size_t> dense);
    // Requests a route to `target` (queued for the search budget when it needs a search).
    template <class Data> static void plan_move(Data& data, std::size_t index, const Vector3& target);
    struct PathRequest;
    void apply_formation_move(const SimCommand& command);
    // The ground members of a formation move on land (`movers`, their `slots` at the ordered point
    // facing `facing`) as frames, one per part of the selection by distance (simulation_frames.cpp).
    void form_frames(const SimCommand& command, std::span<const std::size_t> movers, std::span<const FormationSlot> slots, float facing);
    // Attack-move and guard commands: their standing orders, and the first move.
    void apply_standing_order(const SimCommand& command);
    // Speed this tick: the entity's own, or its group's when that is slower.
    template <class Data> [[nodiscard]] static float travel_speed(Data& data, std::size_t index) noexcept;
    // Where a turn played as clips is `time` into it: its clip, the progress through that clip, and
    // the angle turned (before the way and the scale).
    struct TurnPoint
    {
        TurnClip clip{TurnClip::none};
        float progress{};
        float turned{};
    };
    template <class Data> [[nodiscard]] static TurnPoint turn_point(Data& data, std::size_t index, float time) noexcept;
    // Stamps (or removes) an entity's footprint into the ground's passability.
    void stamp_footprint(std::size_t index, bool add);
    // Builds the clearance of each movement class that ground units of the world use, up to the
    // largest of them (at least clearance_minimum_limit), and recomputes the cells stamps changed.
    void refresh_clearance();
    // The free point nearest `at` for a ground unit of `radius` and movement class `passable_types`
    // (docs/pathfinding-design.md, 4.4: units never overlap): ground with room for its footprint,
    // off every other ground unit's (`except`, the unit itself, aside). `at` itself when it is free,
    // or when no free point lies within free_ground_rings rings around it.
    [[nodiscard]] Vector3 free_ground(const Vector3& at, float radius, std::uint32_t passable_types, std::optional<std::size_t> except);
    // Where ground units stand, for free_ground (scratch, not state): built at the tick's first
    // spawn or teleport, then each placed unit added; dropped when units move or leave.
    struct Occupancy
    {
        std::optional<std::uint64_t> tick;
        float cell{};
        float origin_x{};
        float origin_y{};
        std::uint32_t columns{};
        std::uint32_t rows{};
        float largest_radius{};
        std::vector<std::vector<std::uint32_t>> cells;
    };
    Occupancy occupancy_;
    void occupy(std::size_t index);
    // What the entity at `index` may stand on: its movement class's cells with room for its
    // footprint (docs/pathfinding-design.md, 4.1), and whether it may stand at (x, y).
    template <class Data> [[nodiscard]] static Footing footing_of(Data& data, std::size_t index) noexcept;
    template <class Data> [[nodiscard]] static bool stands_at(Data& data, std::size_t index, float x, float y) noexcept;
    // The hull share an entity's destroyable parts leave it, and whether every one is destroyed
    // (simulation_combat.cpp, apply_damage).
    template <class Data> [[nodiscard]] static float hull_share_of_parts(Data& data, std::size_t index) noexcept;
    template <class Data> [[nodiscard]] static bool parts_all_destroyed(Data& data, std::size_t index) noexcept;
    // Returns what the shot did (shields only, hull, or hull with armor reducing it).
    template <class Data> static ImpactKind apply_damage(Data& data, std::size_t victim, float damage, std::uint16_t damage_type, bool damages_shield, bool damages_health, std::int32_t hardpoint);
    // A point of an entity's own frame in world space.
    template <class Data> [[nodiscard]] static Vector3 world_point(Data& data, std::size_t index, const LocalPoint& point) noexcept;
    template <class Data> [[nodiscard]] static bool hardpoint_alive(Data& data, std::size_t index, std::size_t hardpoint) noexcept;
    // Whether any hardpoint of the kind survives (true when the entity has none of that kind).
    template <class Data> [[nodiscard]] static bool keeps(Data& data, std::size_t index, HardpointProfile::Kind kind) noexcept;
    // The victim's live targetable hardpoint nearest to `from`, or -1.
    template <class Data> [[nodiscard]] static std::int32_t aim_hardpoint(Data& data, std::size_t victim, const Vector3& from) noexcept;
    template <class Data> [[nodiscard]] static Vector3 aim_point(Data& data, std::size_t victim, std::int32_t hardpoint) noexcept;
    // The radius strays_off measures against (0: unknown size).
    template <class Data> [[nodiscard]] static float target_size(Data& data, std::size_t index) noexcept;
    // Damages the hostiles of `owner` around `point` (AreaBlast).
    template <class Data> static void area_blast(Data& data, const Vector3& point, std::uint32_t owner, const AreaBlast& blast, std::uint16_t damage_type, bool damages_shield, bool damages_health);
    // Whether one of `index`'s weapons may fire at `candidate`'s categories (or it has none).
    template <class Data> [[nodiscard]] static bool engages(Data& data, std::size_t index, std::size_t candidate) noexcept;
    // The target `index` prefers among the damageable hostiles within `reach` of `center` (in the
    // ground plane) that `accepts`: the lowest rank of its targeting priorities, then the nearest,
    // then the lower entity id. Defined in simulation_internal.hpp.
    void hash_state(simulation_detail::StateHasher& hasher) const noexcept;
    template <class Data, typename Accepts>
    [[nodiscard]] static std::optional<std::size_t> choose_target(Data& data, const SpatialGrid& grid, std::vector<std::uint32_t>& query, std::size_t index, const Vector3& center, float reach,
                                                                  const Accepts& accepts);
    // Applies one queued damage event (run_resolution).
    void resolve(const DamageEvent& event);
    // Deaths of entities not yet handled this tick, in dense order: spins begin or count down, the
    // dead are reported, queued for removal, and their death blasts queued; returns whether any
    // died.
    bool resolve_deaths(std::vector<std::uint8_t>& handled);
    template <class Data> [[nodiscard]] static bool damageable(Data& data, std::size_t index) noexcept;
    [[nodiscard]] static bool damageable(const CombatState& state) noexcept
    {
        return state.profile->health > 0.0F && state.health > 0.0F;
    }
    // The combat state of the entity at `index`: to read (no_combat() for an entity without the
    // component), to change (null without it), and its target dropped (if it has combat).
    [[nodiscard]] static const CombatState& no_combat() noexcept;
    template <class Data> [[nodiscard]] static const CombatState& combat_state(Data& data, std::size_t index) noexcept;
    template <class Data> [[nodiscard]] static auto* combat_of(Data& data, std::size_t index) noexcept;
    template <class Data> static void drop_target(Data& data, std::size_t index) noexcept;
    // Whether the entity at `index` flies (is in the flight pool).
    template <class Data> [[nodiscard]] static bool flies(Data& data, std::size_t index) noexcept;
    // Whether `crusher` crushes `victim` by walking over it: a crusher, a living hostile victim
    // squashable by its kind. Such a pair never avoids each other: the crusher walks onto the victim.
    template <class Data> [[nodiscard]] static bool can_crush(Data& data, std::size_t crusher, std::size_t victim) noexcept;
    // Whether the entity at `index` is a ground unit avoidance keeps apart on land: a footprint, on
    // the ground, not flying.
    template <class Data> [[nodiscard]] static bool ground_body(Data& data, std::size_t index) noexcept;
    // Whether the entity at `index` belongs to a formation frame still on its way (it follows a moving
    // slot: arrival and giving way among its members are the frame's business).
    template <class Data> [[nodiscard]] static bool in_moving_frame(Data& data, std::size_t index) noexcept;
    // The radius the entity at `index` takes as a whole: a squad's extent, otherwise its footprint.
    template <class Data> [[nodiscard]] static float unit_radius(Data& data, std::size_t index) noexcept;
    // Whether the entity at `index` waits this tick (for its formation group, or as a team for its
    // members), and its leaving its formation group (if it has one).
    template <class Data> [[nodiscard]] static bool waits(Data& data, std::size_t index) noexcept;
    template <class Data> static void leave_group(Data& data, std::size_t index);
    // Sends the member at `member` to `slot` (a squad's or a formation frame's) when it stands more than
    // a little away and the slot moved on from where it was last sent (`sent`), or it stopped; a member
    // blocked on the way plans around (movement) and keeps that way until the slot moves on.
    template <class Data> static void follow_slot(Data& data, std::size_t member, const Vector3& slot, std::optional<Vector3>& sent);
    template <class Data> [[nodiscard]] static std::uint32_t ticks_for(Data& data, float seconds) noexcept;

    // What the tick's systems declare access to (the schedule below): the components of the entity
    // table and the pools, and the world's resources.
    enum class SimData : std::uint8_t
    {
        identity,
        placement,
        motion,
        locomotor,
        ground_mover,
        route,
        flight,
        combat,
        orders,
        steering,
        formation,
        group,
        squads,
        squad_members,
        turn_clips,
        walk_clips,
        footprints,
        crush,
        // The ground and the passability footprints stamp on it.
        ground,
        path_queue,
        // Formation frames (docs/pathfinding-design.md, 4.3).
        frames,
        projectiles,
        // The tick's deaths, impacts, shots, hardpoint deaths, and spins.
        events,
        combat_random,
        // The tick counter and the tick's length.
        clock,
        // The slot table (generations, dense positions, free slots).
        slots,
        // Commands submitted for the next tick.
        commands,
        // The damage table and hostility (set up by the battle, read by the systems).
        rules,
        // Space avoidance's grid and largest ship radius (navigation builds them, movement reads them).
        avoidance,
        // Combat's statistics: shots fired, hits, kills, and hardpoints destroyed so far (not hashed).
        combat_counters,
        // The recipes entities are spawned from (append-only; a recipe never changes once added).
        recipes,
        // The tick's damage, queued for the resolution system.
        damage,
        count,
    };
    static constexpr std::size_t sim_data_count = static_cast<std::size_t>(SimData::count);
    // The dense entity table (docs/ecs-design.md, 6.2 and migration step 2): one column per field,
    // grouped by component, every column one entry per entity in dense order. Each group lists its
    // columns once more in for_each_column, and the table its groups in for_each_group; removal and
    // spawn's size check go through them (spawn sets the values itself). A group holds columns only,
    // so the constructor checks both lists against the sizes of what they list (check_entity_table).
    struct IdentityColumns
    {
        bool operator==(const IdentityColumns&) const = default;
        static constexpr SimData data = SimData::identity;
        std::vector<EntityId> ids;
        std::vector<RecipeId> recipes;
        std::vector<std::uint32_t> owners;
        std::vector<std::uint32_t> visuals;
        std::vector<std::uint8_t> selectable;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(ids);
            visit(recipes);
            visit(owners);
            visit(visuals);
            visit(selectable);
        }
    };
    struct PlacementColumns
    {
        bool operator==(const PlacementColumns&) const = default;
        static constexpr SimData data = SimData::placement;
        std::vector<Vector3> positions;
        std::vector<float> headings;
        // Flyers' current pitch (0 for everything else).
        std::vector<float> pitches;
        std::vector<float> radii;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(positions);
            visit(headings);
            visit(pitches);
            visit(radii);
        }
    };
    struct MotionColumns
    {
        bool operator==(const MotionColumns&) const = default;
        static constexpr SimData data = SimData::motion;
        // Speed of the last tick (EntitySnapshot::speed).
        std::vector<float> speeds;
        // Positions before the tick's movement (set by the commands system every tick).
        std::vector<Vector3> starts;
        // Horizontal velocity of the last tick, from where movement and contact left the entity: what
        // ground avoidance takes as a neighbour's velocity (hashed: the next tick reads it).
        std::vector<Vector2> velocities;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(speeds);
            visit(starts);
            visit(velocities);
        }
    };
    struct LocomotorColumns
    {
        bool operator==(const LocomotorColumns&) const = default;
        static constexpr SimData data = SimData::locomotor;
        std::vector<float> max_speeds;
        std::vector<float> turn_rates;
        // The recipe's profile (shared by the entities of the recipe).
        std::vector<const MovementProfile*> movements;
        // The locomotor's own speed, which accelerates and brakes (MotionColumns::speeds is the
        // measured one).
        std::vector<float> drive_speeds;
        // Group speed of a formation move until the entity arrives; zero when none.
        std::vector<float> speed_limits;
        // Suspend_Locomotor: the entity neither moves nor turns (hashed).
        std::vector<std::uint8_t> suspended;
        // Braking for its destination this tick (EntitySnapshot::arriving); not hashed.
        std::vector<std::uint8_t> arriving;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(max_speeds);
            visit(turn_rates);
            visit(movements);
            visit(drive_speeds);
            visit(speed_limits);
            visit(suspended);
            visit(arriving);
        }
    };
    struct GroundMoverColumns
    {
        bool operator==(const GroundMoverColumns&) const = default;
        static constexpr SimData data = SimData::ground_mover;
        std::vector<std::uint8_t> follows_ground;
        std::vector<std::uint32_t> passable_types;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(follows_ground);
            visit(passable_types);
        }
    };
    // A unit making way for a faster ally (docs/pathfinding-design.md, 4.4): the ally, the point it
    // steps aside to, where it stood when asked (a standing unit returns there), the ticks it has
    // given way, and whether it is on its way back.
    struct GiveWay
    {
        bool operator==(const GiveWay&) const = default;
        EntityId passer;
        Vector3 aside;
        Vector3 back;
        std::uint32_t ticks{};
        bool returning{};
    };
    struct RouteColumns
    {
        bool operator==(const RouteColumns&) const = default;
        static constexpr SimData data = SimData::route;
        std::vector<std::optional<Vector3>> move_targets;
        // Waypoints before the move target, last first (back() is the next one to reach).
        std::vector<std::vector<Vector3>> paths;
        // Serial of the entity's queued request (zero when none); a newer request supersedes it.
        std::vector<std::uint32_t> pending;
        // Where an idle flyer circles (set when it arrives or first idles).
        std::vector<std::optional<Vector3>> loiter_points;
        // The route left to the move target this tick (measured by navigation; 0 without a target).
        std::vector<float> remaining;
        // Space ships: the point steered at this tick to pass a unit in the way (navigation's local
        // avoidance); recomputed every tick from the state, so not hashed.
        std::vector<std::optional<Vector3>> detours;
        // Ground units: the velocity avoidance picked this tick where it differs from the one toward
        // the next waypoint (navigation's ORCA); recomputed every tick from the state, so not hashed.
        std::vector<std::optional<Vector2>> avoid_velocities;
        // Ground units making way for an ally (hashed).
        std::vector<std::optional<GiveWay>> give_ways;
        // Where a ground unit last stopped at the end of a move (hashed): units that stopped at one
        // destination are its crowd (goal_taken).
        std::vector<std::optional<Vector3>> last_goals;
        // On the last leg to the move target: the closest the unit has come to it, and the ticks since
        // it last came closer by arrival_progress (hashed; goal_taken stops a unit that cannot get
        // closer past a crowd of its allies).
        std::vector<float> closest_approaches;
        std::vector<std::uint32_t> stalled_ticks;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(move_targets);
            visit(paths);
            visit(pending);
            visit(loiter_points);
            visit(remaining);
            visit(detours);
            visit(avoid_velocities);
            visit(give_ways);
            visit(last_goals);
            visit(closest_approaches);
            visit(stalled_ticks);
        }
    };
    // Flight (pooled: flyers only, the entities whose recipe has a minimum speed above 0): the
    // speed it never drops below, the height of the plane it cruises in (its spawn height), the
    // heading change of the last tick per second, whether it is inside a half-loop, and the roll it
    // unwinds after one.
    struct FlightColumns
    {
        bool operator==(const FlightColumns&) const = default;
        static constexpr SimData data = SimData::flight;
        std::vector<float> min_speeds;
        std::vector<float> cruise_heights;
        std::vector<float> yaw_rates;
        std::vector<std::uint8_t> looping;
        std::vector<float> rolls;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(min_speeds);
            visit(cruise_heights);
            visit(yaw_rates);
            visit(looping);
            visit(rolls);
        }
    };
    // Combat (pooled: entities whose recipe has health, a shield, weapons, or hardpoints; the rest
    // read as no_combat(): no health, no weapons, no target).
    struct CombatColumns
    {
        bool operator==(const CombatColumns&) const = default;
        static constexpr SimData data = SimData::combat;
        std::vector<CombatState> states;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(states);
        }
    };
    // Standing orders (gameplay state, hashed): the kind, the destination or anchor point and object
    // (invalid: the point), whether the current target came from the order and where that chase
    // began, and the ticks to the next scan for hostiles (staggered by entity index).
    struct StandingOrderState
    {
        bool operator==(const StandingOrderState&) const = default;
        StandingOrder kind{StandingOrder::none};
        Vector3 point;
        EntityId entity;
        bool engaging{};
        Vector3 engage_origin;
        std::uint32_t scan_timer{};
    };
    struct OrderColumns
    {
        bool operator==(const OrderColumns&) const = default;
        static constexpr SimData data = SimData::orders;
        std::vector<std::uint64_t> command_serials;
        std::vector<StandingOrderState> standing;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(command_serials);
            visit(standing);
        }
    };
    // A turn in place played as clips (MovementProfile::turn_begin_seconds): under way, seconds into
    // it, the heading it started from, its angle, its way (+1 counter-clockwise, left), the scale of
    // the clips' nominal rates that ends it exactly on the way, and its cycles (loops, HALF, and
    // QUARTER); hashed for entities with such turns only.
    struct TurnPlan
    {
        bool operator==(const TurnPlan&) const = default;
        bool active{};
        float elapsed{};
        float start{};
        float angle{};
        float way{};
        float scale{};
        std::uint32_t loops{};
        std::uint32_t halves{};
        std::uint32_t quarters{};
    };
    struct TurnClipColumns
    {
        bool operator==(const TurnClipColumns&) const = default;
        static constexpr SimData data = SimData::turn_clips;
        std::vector<TurnPlan> plans;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(plans);
        }
    };
    // A walk played as clips (MovementProfile::walk_stop_parts): its clip, the stop's part, seconds
    // into the clip, the stride phase (0..1), the speed its stop brakes from, the share of the travel
    // speed a walk too short for its clips keeps, and whether its stop ends the move to `destination`
    // (a move elsewhere ordered meanwhile goes on after the stop); hashed for such entities only.
    struct WalkPlan
    {
        bool operator==(const WalkPlan&) const = default;
        WalkClip clip{WalkClip::standing};
        std::uint32_t part{};
        float elapsed{};
        float phase{};
        float from_speed{};
        float scale{1.0F};
        bool ends_move{};
        Vector3 destination;
        // A new destination came while it walks (plan_move): the first way to it decides whether it
        // turns in place first (steer).
        bool new_way{};
        // That way is more than its turn-in-place angle off: it holds its heading, stops at its
        // stride's next part, and turns in place from there.
        bool turn_back{};
    };
    struct WalkClipColumns
    {
        bool operator==(const WalkClipColumns&) const = default;
        static constexpr SimData data = SimData::walk_clips;
        std::vector<WalkPlan> plans;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(plans);
        }
    };
    struct SteeringColumns
    {
        bool operator==(const SteeringColumns&) const = default;
        static constexpr SimData data = SimData::steering;
        // Rebuilt by the behavior layer every tick before movement.
        std::vector<SteeringIntent> intents;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(intents);
        }
    };
    // A company member's team (pooled: entities spawned with a team other than the default; the
    // rest hold the default). Its type's formation order, spacing, and whether it moves alone are
    // recipe data (EntityRecipe), not columns.
    struct FormationColumns
    {
        bool operator==(const FormationColumns&) const = default;
        static constexpr SimData data = SimData::formation;
        std::vector<FormationTeam> teams;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(teams);
        }
    };
    struct GroupColumns
    {
        bool operator==(const GroupColumns&) const = default;
        static constexpr SimData data = SimData::group;
        // Group cohesion (pooled: members of a formation group, joined by a formation move of more
        // than one unit and left when the move ends, is broken off, or stalls): the group, the
        // remaining route it last made progress at, ticks since then, and whether it waits this tick.
        // In a group with a frame (FormationFrame): the member's slot as an offset in the frame
        // (forward, right), where it stood in the frame when the order came (the slot it starts from,
        // blending into its formation slot over the frame's forming distance), and the slot it was
        // last sent to.
        std::vector<std::uint32_t> ids;
        std::vector<float> best_remaining;
        std::vector<std::uint32_t> stall_ticks;
        std::vector<std::uint8_t> waiting;
        std::vector<float> ahead;
        std::vector<float> aside;
        std::vector<float> start_ahead;
        std::vector<float> start_aside;
        std::vector<std::optional<Vector3>> sent;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(ids);
            visit(best_remaining);
            visit(stall_ticks);
            visit(waiting);
            visit(ahead);
            visit(aside);
            visit(start_ahead);
            visit(start_aside);
            visit(sent);
        }
    };
    // An obstacle's footprint on the ground (pooled: obstacles only) and whether it is stamped.
    struct FootprintColumns
    {
        bool operator==(const FootprintColumns&) const = default;
        static constexpr SimData data = SimData::footprints;
        std::vector<GroundFootprint> shapes;
        std::vector<std::uint8_t> stamped;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(shapes);
            visit(stamped);
        }
    };
    // What an entity crushes and what crushes it (pooled: entities with a profile other than the
    // default; the others crush nothing and are not crushed).
    struct CrushColumns
    {
        bool operator==(const CrushColumns&) const = default;
        static constexpr SimData data = SimData::crush;
        std::vector<CrushProfile> profiles;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(profiles);
        }
    };
    IdentityColumns identity_;
    PlacementColumns placement_;
    MotionColumns motion_;
    LocomotorColumns locomotor_;
    GroundMoverColumns ground_mover_;
    RouteColumns route_;
    OrderColumns orders_;
    SteeringColumns steering_;
    // Every column of the entity table, component by component.
    // Every group of the entity table, in declaration order (identity_ first, steering_ last).
    template <class Visit> void for_each_group(Visit&& visit)
    {
        visit(identity_);
        visit(placement_);
        visit(motion_);
        visit(locomotor_);
        visit(ground_mover_);
        visit(route_);
        visit(orders_);
        visit(steering_);
    }
    template <class Visit> void for_each_column(Visit&& visit)
    {
        for_each_group(
            [&](auto& group)
            {
                group.for_each_column(visit);
            });
    }
    // That for_each_column and for_each_group list every column and group (simulation.cpp).
    void check_entity_table();

    // The tick's schedule (docs/ecs-design.md, 6.3 and migration step 4; simulation_schedule.cpp):
    // its systems in order, each with the tick layer it is timed under and the data it reads and
    // writes (a typed system's from its view's type, system_view.hpp).
    using SimAccess = std::uint64_t;
    static_assert(sim_data_count <= 64, "SimAccess holds one bit per kind of data");
    [[nodiscard]] static constexpr SimAccess access_to(std::initializer_list<SimData> data) noexcept
    {
        SimAccess access = 0;
        for (const auto item : data)
        {
            access |= SimAccess{1} << static_cast<unsigned>(item);
        }
        return access;
    }
    static constexpr SimAccess all_data = (SimAccess{1} << sim_data_count) - 1U;
    struct SystemEntry
    {
        std::string_view name;
        TickPhase phase;
        void (SimulationWorld::*run)();
        SimAccess reads;
        SimAccess writes;
    };
    static const std::array<SystemEntry, 9> schedule_;
    // Every kind of data with its objects (some kinds have several): the entity table's groups
    // (for_each_group), the pools, and the resources.
    template <class Visit> void for_each_data(Visit&& visit)
    {
        for_each_group(
            [&](auto& group)
            {
                visit(std::remove_cvref_t<decltype(group)>::data, group);
            });
        visit(SimData::turn_clips, turn_clips_);
        visit(SimData::walk_clips, walk_clips_);
        visit(SimData::flight, flight_);
        visit(SimData::footprints, footprint_);
        visit(SimData::crush, crush_);
        visit(SimData::formation, formation_);
        visit(SimData::group, group_);
        visit(SimData::combat, combat_);
        visit(SimData::squads, squads_);
        visit(SimData::squad_members, squad_members_);
        visit(SimData::ground, ground_);
        visit(SimData::path_queue, path_queue_);
        visit(SimData::frames, frames_);
        visit(SimData::projectiles, projectiles_);
        visit(SimData::events, events_);
        visit(SimData::combat_random, combat_random_);
        visit(SimData::clock, clock_);
        visit(SimData::slots, slots_);
        visit(SimData::commands, pending_);
        visit(SimData::rules, rules_);
        visit(SimData::avoidance, avoidance_);
        visit(SimData::combat_counters, combat_counters_);
        visit(SimData::recipes, recipes_);
        visit(SimData::damage, damage_queue_);
    }
    // The systems step() ran inline before: the tick's commands and its event lists, the speeds
    // movement and separation produced, and the tick counter with the snapshot.
    void run_commands();
    // The contact system's run on its view (ContactSystem).
    void run_contact();
    void run_movement();
    void run_navigation();
    void run_behavior();
    void run_combat();
    void run_publish();
    // Resolution: the tick's queued damage in order, then deaths and the blasts they set off, until no
    // more die (chain kills end in the same tick).
    void run_resolution();
    // The barrier at the end of the tick: the dead leave, the others keep their order.
    void run_removal();
    // Optional components in pools (component_pool.hpp), given at spawn: turns in place played as
    // clips to entities whose profile has them (MovementProfile::turn_begin_seconds), walks played as
    // clips to those that walk or raise a leg as clips (walk_stop_parts, walk_raise_seconds),
    // manoeuvres to flyers (a minimum speed above 0), footprints to obstacles (EntitySpawn::footprint),
    // and crush profiles to entities whose profile is not the default (crush_profile_of reads them).
    ComponentPool<TurnClipColumns> turn_clips_;
    ComponentPool<WalkClipColumns> walk_clips_;
    ComponentPool<FlightColumns> flight_;
    ComponentPool<FootprintColumns> footprint_;
    ComponentPool<CrushColumns> crush_;
    ComponentPool<FormationColumns> formation_;
    ComponentPool<GroupColumns> group_;
    ComponentPool<CombatColumns> combat_;
    // Squads (simulation_squads.cpp; pooled): a team entity's members, in id order, their ring's
    // arrangement and extent (the farthest member's distance plus the largest footprint), its
    // ordered attack target, how long it has waited for members behind, and its chase timer; and a
    // member's team, its offset (forward, right) from the team's center with its raggedness, and the
    // slot it was last sent to.
    struct SquadState
    {
        bool operator==(const SquadState&) const = default;
        std::vector<EntityId> members;
        TeamArrangement arrangement{TeamArrangement::dartboard};
        float extent{};
        EntityId target;
        bool waiting{};
        std::uint32_t wait_ticks{};
        std::uint32_t chase_timer{};
    };
    struct SquadColumns
    {
        bool operator==(const SquadColumns&) const = default;
        static constexpr SimData data = SimData::squads;
        std::vector<SquadState> states;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(states);
        }
    };
    struct SquadMemberState
    {
        bool operator==(const SquadMemberState&) const = default;
        EntityId squad;
        float ahead{};
        float aside{};
        float raggedness{};
        std::optional<Vector3> slot;
    };
    struct SquadMemberColumns
    {
        bool operator==(const SquadMemberColumns&) const = default;
        static constexpr SimData data = SimData::squad_members;
        std::vector<SquadMemberState> states;
        template <class Visit> void for_each_column(Visit&& visit)
        {
            visit(states);
        }
    };
    ComponentPool<SquadColumns> squads_;
    ComponentPool<SquadMemberColumns> squad_members_;
    // Joins the member at `member` to the team `squad`, then reshapes the team's ring.
    void join_squad(std::size_t member, EntityId squad, float raggedness);
    // The members' ring offsets and the team's extent, from its members' footprints.
    void reshape_squad(std::uint32_t squad_slot);
    // A command as squads take it: orders to a member go to its team (each team once), an attack on
    // a member is an attack on its team, and a stance set on a team is set on its members.
    [[nodiscard]] SimCommand for_squads(const SimCommand& command) const;
    // A team's attack order, given to its members (any member of a target team for each), and its
    // end (a move, stop, or standing order).
    void order_squad_attack(std::size_t squad, std::size_t victim);
    void clear_squad_attack(std::size_t squad);
    // The entities `dense` names, with each removed team's members and each team whose last member
    // goes; the teams that lose members but stay, by slot.
    void expand_squad_removals(std::vector<std::size_t>& dense, std::vector<std::uint32_t>& reshaped);
    [[nodiscard]] static const CrushProfile& crush_profile_in(const ComponentPool<CrushColumns>& pool, std::uint32_t slot) noexcept;
    // Remaining route length of every moving entity (zero for the rest), into route.remaining.
    static void measure_remaining_routes(RouteColumns& route, const PlacementColumns& placement);
    // Of `members` (dense indices), the one nearest their centroid, ties to the earlier (a group's
    // commander: where its route starts).
    [[nodiscard]] static std::size_t nearest_to_centroid(const PlacementColumns& placement, std::span<const std::size_t> members);
    // The slot of the entity at dense `index`: the key of its pooled components.
    template <class Data> [[nodiscard]] static std::uint32_t slot_of(Data& data, const std::size_t index) noexcept
    {
        return data.template get<IdentityColumns>().ids[index].index;
    }
    // A turn in place played as clips is under way.
    template <class Data> [[nodiscard]] static bool turning_as_clips(Data& data, std::size_t index) noexcept;

    // World state and per-tick scratch (not entity columns: sized per tick or per world).
    // A queued route request: one entity, or the ground members of a formation move that share a
    // movement class (planned on one shared path to `group_target`).
    struct PathRequest
    {
        bool operator==(const PathRequest&) const = default;
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
        // A formation frame's route (its group; 0 for none): only the shared path is planned, from
        // the frame, for the frame (FormationFrame::route).
        std::uint32_t frame{};
    };
    // The world's resources (docs/ecs-design.md, 6.2): what exists once, each kind of data a struct of
    // its own, so a system names what it uses by type.
    // The tick counter and the fixed length of a tick in seconds.
    struct Clock
    {
        bool operator==(const Clock&) const = default;
        static constexpr SimData data = SimData::clock;
        std::uint64_t tick{};
        double tick_seconds{};
    };
    Clock clock_;
    struct GroundState
    {
        bool operator==(const GroundState&) const = default;
        static constexpr SimData data = SimData::ground;
        GroundMap map;
        // The ground's own passability, and how many footprints cover each cell.
        std::vector<std::uint32_t> own_passability;
        std::vector<std::uint16_t> footprint_cover;
        // Clearance per movement class in use (pathfinding.hpp), refreshed at the start barrier
        // (refresh_clearance); the cells footprint stamps changed since, as one rectangle.
        std::vector<ClearanceField> clearance;
        std::optional<CellRect> changed;
    };
    struct PathQueue
    {
        bool operator==(const PathQueue&) const = default;
        static constexpr SimData data = SimData::path_queue;
        std::deque<PathRequest> requests;
        std::uint32_t next_serial{1};
    };
    // A ground formation's frame (docs/pathfinding-design.md, 4.3; simulation_frames.cpp): the point
    // and facing its members' slots move with, along the group's route at its slowest member's speed,
    // waiting for members that fall behind; at the end of the route, the slots at the ordered point.
    struct FormationFrame
    {
        bool operator==(const FormationFrame&) const = default;
        std::uint32_t group{};
        Vector3 position;
        float facing{};
        Vector3 target;
        float target_facing{};
        float speed{};
        // The route ahead, the next waypoint last (as RouteColumns::paths); planned once.
        std::vector<Vector3> route;
        bool planned{};
        bool arrived{};
        std::uint32_t wait_ticks{};
        // How far it has moved, and over how much of its way the members' slots blend from where they
        // stood into the formation (the formation forms on the move).
        float travelled{};
        float forming{};
    };
    struct FormationFrames
    {
        bool operator==(const FormationFrames&) const = default;
        static constexpr SimData data = SimData::frames;
        // In creation order (ascending group).
        std::vector<FormationFrame> list;
    };
    struct ProjectileState
    {
        bool operator==(const ProjectileState&) const = default;
        static constexpr SimData data = SimData::projectiles;
        std::vector<Projectile> flying;
        // Launch number of the next shot (ProjectileSnapshot::serial; presentation only, not hashed).
        std::uint32_t next_serial{1};
    };
    // The current tick's deaths (SimSnapshot::deaths), impacts, shots, hardpoint deaths, and spins.
    struct TickEvents
    {
        bool operator==(const TickEvents&) const = default;
        static constexpr SimData data = SimData::events;
        std::vector<DeathSnapshot> deaths;
        std::vector<ImpactSnapshot> impacts;
        std::vector<ShotSnapshot> shots;
        std::vector<HardpointDeathSnapshot> hardpoint_deaths;
        std::vector<SpinSnapshot> spins;
    };
    // The slot table: the generation per slot, the slot's position in dense storage, and the free
    // slots.
    struct SlotTable
    {
        bool operator==(const SlotTable&) const = default;
        static constexpr SimData data = SimData::slots;
        std::vector<std::uint32_t> generations;
        std::vector<std::uint32_t> dense;
        std::vector<std::uint32_t> free;
    };
    // What the battle sets up once: damage modifiers and who is hostile to whom.
    struct Rules
    {
        bool operator==(const Rules&) const = default;
        static constexpr SimData data = SimData::rules;
        DamageTable damage_table;
        Hostility hostility;
    };
    // The tick's avoidance: the grid of positions navigation planned with (ground units on land, ships
    // in space) and the largest radius among them (0 when nothing is kept apart; keep_apart then has
    // nothing to do). Movement keeps units apart against this grid, built before anything moved.
    struct Avoidance
    {
        bool operator==(const Avoidance&) const = default;
        static constexpr SimData data = SimData::avoidance;
        SpatialGrid grid;
        float largest_radius{};
        // On land, each entity's avoidance radius this tick: a ground unit's footprint, a squad's
        // extent (the squad avoids as one; its soldiers hold their places in it and are not kept
        // apart themselves), 0 for what avoidance leaves alone; and the entity whose crush profile
        // stands for it (a squad's first soldier, itself otherwise).
        std::vector<float> radii;
        std::vector<std::uint32_t> crush_proxies;
    };
    GroundState ground_;
    Avoidance avoidance_;
    PathQueue path_queue_;
    FormationFrames frames_;
    ProjectileState projectiles_;
    TickEvents events_;
    SlotTable slots_;
    Rules rules_;
    // The recipes, in registration order; a deque, so the profiles entities point at never move.
    struct RecipeStore
    {
        bool operator==(const RecipeStore&) const = default;
        static constexpr SimData data = SimData::recipes;
        std::deque<EntityRecipe> list;
    };
    RecipeStore recipes_;
    // Shots fired, hits, kills, and hardpoints destroyed so far (Stats; not hashed).
    struct CombatCounters
    {
        bool operator==(const CombatCounters&) const = default;
        static constexpr SimData data = SimData::combat_counters;
        std::uint64_t shots_fired{};
        std::uint64_t hits{};
        std::uint64_t kills{};
        std::uint64_t hardpoints_destroyed{};
        std::uint64_t crushed{};
    };
    CombatCounters combat_counters_;
    struct DamageQueue
    {
        bool operator==(const DamageQueue&) const = default;
        static constexpr SimData data = SimData::damage;
        std::vector<DamageEvent> events;
        // The dense indices of the entities that died in the tick, removed at its end.
        std::vector<std::size_t> dying;
    };
    DamageQueue damage_queue_;
    // The combat random stream: where shots stray (WeaponProfile::inaccuracy) and who spins as it
    // dies; part of the state.
    struct CombatRandom
    {
        bool operator==(const CombatRandom&) const = default;
        static constexpr SimData data = SimData::combat_random;
        Pcg32 stream{combat_random_seed};
    };
    CombatRandom combat_random_;

    // Typed system views (system_view.hpp): the kind of data a type is (a pool's is its columns'),
    // the masks a view's type declares, the world's one object of a type, and a view of them.
    template <class T> [[nodiscard]] static constexpr SimData data_of() noexcept
    {
        if constexpr (requires { typename T::ColumnsType; })
        {
            return T::ColumnsType::data;
        }
        else
        {
            return T::data;
        }
    }
    template <class View> [[nodiscard]] static constexpr SimAccess reads_of() noexcept
    {
        return []<class... Accesses>(std::tuple<Accesses...>*)
        {
            return (SimAccess{0} | ... | access_to({data_of<typename Accesses::Type>()}));
        }(static_cast<typename View::AccessList*>(nullptr));
    }
    template <class View> [[nodiscard]] static constexpr SimAccess writes_of() noexcept
    {
        return []<class... Accesses>(std::tuple<Accesses...>*)
        {
            return (SimAccess{0} | ... | (Accesses::writes ? access_to({data_of<typename Accesses::Type>()}) : SimAccess{0}));
        }(static_cast<typename View::AccessList*>(nullptr));
    }
    template <class T> [[nodiscard]] T& object_of() noexcept
    {
        T* found = nullptr;
        for_each_data(
            [&](SimData, auto& object)
            {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(object)>, T>)
                {
                    found = &object;
                }
            });
        EAW_ASSERT_MSG(found != nullptr, std::string("no world object of type ") + typeid(T).name());
        return *found;
    }
    // The world's object of a type, for code with full access (the systems still written as member
    // functions) calling helpers written for views (they ask their data for get<T>()).
    template <class T> [[nodiscard]] T& get() noexcept
    {
        return object_of<T>();
    }
    template <class T> [[nodiscard]] const T& get() const noexcept
    {
        return const_cast<SimulationWorld*>(this)->object_of<T>();
    }
    template <class View> [[nodiscard]] View view_of() noexcept
    {
        return [this]<class... Accesses>(std::tuple<Accesses...>*)
        {
            return View{&object_of<typename Accesses::Type>()...};
        }(static_cast<typename View::AccessList*>(nullptr));
    }

    // Contact (simulation_contact.cpp; docs/ecs-design.md, 6.3, phase 5): crushing, separation, and
    // the tick's measured speeds, after movement. Its scratch and counters are its own.
    class ContactSystem
    {
    public:
        using View = SystemView<Read<GroundState>, Read<IdentityColumns>, Write<PlacementColumns>, Write<MotionColumns>, Read<LocomotorColumns>, Read<GroundMoverColumns>,
                                Read<ComponentPool<FlightColumns>>, Read<ComponentPool<CombatColumns>>, Read<ComponentPool<CrushColumns>>, Read<Rules>, Write<DamageQueue>>;
        void run(const View& view, float dt);
        // The pairs of flyers separation pushed apart in the last tick.
        std::size_t separated_pairs{};

    private:
        // Crushing: after movement.
        void crush(const View& view);
        void separate(const View& view, float dt);
        // Speed and velocity of the last tick from where movement and contact left each entity.
        static void measure_speeds(const View& view, float dt);
        [[nodiscard]] static const CrushProfile& crush_profile_of(const View& view, std::size_t index) noexcept;
        std::vector<Vector3> pushes_;
        SpatialGrid grid_;
        std::vector<std::uint32_t> query_;
    };
    ContactSystem contact_;

    // Movement (simulation_locomotion.cpp, simulation_walk.cpp; docs/ecs-design.md, 6.3, phase 4):
    // ground movement, walks and turns played as clips, flight, and keeping ships apart.
    class MovementSystem
    {
    public:
        using View = SystemView<Read<IdentityColumns>, Write<PlacementColumns>, Write<LocomotorColumns>, Read<GroundMoverColumns>, Write<RouteColumns>, Read<ComponentPool<CombatColumns>>,
                                Read<SteeringColumns>, Write<ComponentPool<GroupColumns>>, Write<ComponentPool<TurnClipColumns>>, Write<ComponentPool<WalkClipColumns>>,
                                Write<ComponentPool<FlightColumns>>, Read<GroundState>, Read<Avoidance>, Write<PathQueue>, Read<ComponentPool<SquadColumns>>, Read<ComponentPool<CrushColumns>>,
                                Read<Rules>, Read<Clock>, Read<OrderColumns>, Read<FormationFrames>>;
        void run(const View& view, float dt);
        // Moves planned again so far because the unit's planned line no longer fit (avoidance took it
        // off its line, or the ground changed).
        std::uint64_t blocked_moves{};

    private:
        // Speeds up or slows down toward `wanted` at the profile's rates; returns this tick's speed.
        float drive(const View& view, std::size_t index, float wanted, float dt) noexcept;
        // Never below the minimum speed; turns and pitches toward the intent's point at the turn rate
        // (pitch limited to flight_max_pitch) and moves along the nose.
        void fly(const View& view, std::size_t index, float dt);
        // A step without the part that would take the unit deeper into a unit it touches (both radii):
        // a ground unit on land against other ground units (not what it crushes), a space ship against
        // ships; units slide along each other instead of overlapping, and a wedged unit holds still.
        [[nodiscard]] Vector3 keep_apart(const View& view, std::size_t index, Vector3 step);
        // A ground step along the velocity avoidance picked (RouteColumns::avoid_velocities), turning
        // toward it and never into a unit; false, before turning or speeding up, when that step does
        // not fit the ground (avoidance does not see walls): the unit then follows its route.
        [[nodiscard]] bool avoid_step(const View& view, std::size_t index, Vector2 velocity, float dt);
        // A unit giving way (RouteColumns::give_ways) steps toward the point aside, or back to where it
        // stood, never into a unit; back there, or held by units in its way, it is done.
        void give_way_step(const View& view, std::size_t index, float dt);
        // Records where the unit stops at the end of a move (RouteColumns::last_goals), not at a
        // formation slot on the way.
        void record_stop(const View& view, std::size_t index);
        // Whether the unit's destination is taken (or as good as reached: held for a while within its
        // radius of it): it heads for its last point and touches (within
        // taken_goal_margin) a standing agent covering that point, or, having come no closer for
        // arrival_stall_seconds, a standing agent of its owner nearer that point. It then stops where
        // it is, the point closest to the goal where it overlaps no one (the owner, 2026-11-15).
        [[nodiscard]] bool goal_taken(const View& view, std::size_t index);
        // goal_taken's scratch: the agents at the goal.
        std::vector<std::uint32_t> goal_query_;
        // One entity's ground movement this tick, along its route at move_speed (physical movement only:
        // where it goes is the behavior layer's).
        void move_over_ground(const View& view, std::size_t index, float dt);
        // This tick's speed toward a destination `distance` away after steer returned `facing`: the
        // movement profile's (wanted_speed, then drive), or a walk played as clips' (walk_speed).
        float move_speed(const View& view, std::size_t index, float facing, float distance, float dt) noexcept;
        // Movers that cannot turn while slow move along their heading, not straight at their target,
        // until within settle_distance of it.
        [[nodiscard]] bool moves_along_heading(const View& view, std::size_t index) const noexcept;
        [[nodiscard]] float settle_distance(const View& view, std::size_t index) const noexcept;
        // Turns toward `desired` (radians) at the entity's turn rate (its stopped turn rate when moving
        // slower than its profile's turn speed) and returns how much of its speed it may use: 1 when
        // facing the way, falling to 0 at 90 degrees or more off.
        [[nodiscard]] float steer(const View& view, std::size_t index, float desired, float dt);
        // A turn in place played as clips (simulation_walk.cpp): plans it when a turn of `difference`
        // (radians) begins from rest, and advances it by `dt`; false when the entity turns as usual.
        bool turn_as_clips(const View& view, std::size_t index, float difference, float dt);
        // A walk played as clips (simulation_walk.cpp): this tick's speed, starting the walk from rest,
        // advancing the stride, and beginning the stop at the stride's part where it lands closest to
        // the destination `distance` away.
        float walk_speed(const View& view, std::size_t index, float facing, float distance, float dt) noexcept;
        // Runs a walk played as clips that goes straight ahead this tick (stopping, or walking on to its
        // stop without a destination); false when the entity moves or stands as usual.
        bool walk_straight(const View& view, std::size_t index, float dt);
        // The speed a mover wants this tick, `distance` from its destination, after steer returned
        // `facing`: the travel speed shaped by its movement profile (turning in place, keeping speed in
        // a turn, braking ahead of the destination).
        // Also marks the entity arriving (LocomotorColumns::arriving) when braking for its destination
        // holds its speed back.
        [[nodiscard]] float wanted_speed(const View& view, std::size_t index, float facing, float distance) noexcept;
        // Whether a standing walk played as clips is about to turn in place toward the behavior
        // layer's point (so it keeps a raised leg up).
        [[nodiscard]] bool wants_turn_in_place(const View& view, std::size_t index) const noexcept;
        std::vector<std::uint32_t> query_;
    };
    MovementSystem movement_;

    // Navigation (simulation_navigation.cpp; docs/ecs-design.md, 6.3, phase 3): route searches under
    // their budget, the remaining routes, group cohesion, and space avoidance.
    class NavigationSystem
    {
    public:
        using View =
            SystemView<Read<IdentityColumns>, Read<PlacementColumns>, Write<LocomotorColumns>, Read<GroundMoverColumns>, Write<RouteColumns>, Read<ComponentPool<FlightColumns>>,
                       Read<ComponentPool<CombatColumns>>, Write<ComponentPool<GroupColumns>>, Read<GroundState>, Write<Avoidance>, Write<PathQueue>, Read<SlotTable>, Read<Clock>,
                       Read<ComponentPool<SquadColumns>>, Read<MotionColumns>, Read<ComponentPool<CrushColumns>>, Read<Rules>, Read<ComponentPool<SquadMemberColumns>>, Write<FormationFrames>>;
        // Route requests under the search budget, remaining routes, group cohesion, and space and
        // ground avoidance, before movement.
        void run(const View& view);
        // The ground changed (set, or a footprint stamped or lifted): the pathfinders' cached regions
        // are stale.
        void ground_changed() noexcept
        {
            pathfinder_.reset();
            local_pathfinder_.reset();
        }
        // Route searches so far, those that ended partial, and the nodes they expanded; members
        // planned on a shared route; group members waiting in the last tick, and members released from
        // their group so far.
        std::uint64_t path_searches{};
        std::uint64_t partial_paths{};
        std::uint64_t path_expansions{};
        std::uint64_t shared_routes{};
        std::size_t waiting_members{};
        std::uint64_t released_members{};

    private:
        // Works through queued path requests until the tick's search budget is spent.
        void process_path_queue(const View& view);
        // Plans now: a full search from the entity's position.
        void plan_route_now(const View& view, std::size_t index, const Vector3& target);
        // A formation frame's route (simulation_frames.cpp), for its largest member.
        void plan_frame_route(const View& view, const PathRequest& request);
        void plan_shared_member(const View& view, const PathRequest& request, std::size_t index, const Vector3& goal);
        // Plans a shared request's members until the tick's budget is spent; false when members remain.
        [[nodiscard]] bool plan_shared_route(const View& view, PathRequest& request, std::uint64_t spent_before, bool first_in_tick);
        // Local avoidance of space ships: each steering ship's detour (RouteColumns::detours) around
        // the ships in its way.
        void plan_space_avoidance(const View& view);
        // Ground avoidance on land (simulation_avoidance.cpp): each moving ground unit's velocity for
        // the tick by ORCA against its nearest ground units (RouteColumns::avoid_velocities), from the
        // state at the start of the tick, and the grid movement keeps them apart with.
        void plan_ground_avoidance(const View& view);
        // Giving way, before ground avoidance plans velocities: ends the giving way of units whose
        // passer has passed (or is gone, stopped, or took too long), then asks the allies in each moving
        // unit's way to step aside (RouteColumns::give_ways). From the state at the start of the tick.
        void plan_giving_way(const View& view);
        // Whether the unit at `yielder` stepping aside to (x, y) would stand clear of every ground unit
        // but `passer` (a unit that cannot make room is not asked to).
        [[nodiscard]] bool room_at(const View& view, std::size_t yielder, std::size_t passer, float x, float y);
        // Each entity's avoidance radius and crush proxy (Avoidance::radii, crush_proxies).
        void measure_agents(const View& view);
        // How long each unit on its last leg has made no progress (RouteColumns::stalled_ticks).
        static void measure_arrival_progress(const View& view);
        // Whether the avoidance agent at `index` moves this tick (a target, not waiting, not held, not
        // giving way): the agents avoidance plans a velocity for.
        [[nodiscard]] static bool ground_steers(const View& view, std::size_t index) noexcept;
        // The direction a moving ground unit heads in: toward its next waypoint (zero when there).
        [[nodiscard]] static Vector2 heading_toward_waypoint(const View& view, std::size_t index) noexcept;
        // Sets an entity's route from planned waypoints (the last is its destination).
        void set_route(const View& view, std::size_t index, const std::vector<PathPoint>& waypoints, float height);
        // Marks group members that are ahead of their group to wait this tick, and releases members
        // that stopped making progress.
        void update_group_cohesion(const View& view);
        // The refined layout, the owner's scheme: across the 76 stock land maps the path audit's
        // searches expand 0.12 of the plain search's nodes (0.035 before the first stretch is
        // ready), time 0.38, paths 1.5% longer, against 0.52, 0.61, and 0.5% for one level held to
        // its corridor (2026-11-15).
        GridPathfinder pathfinder_{0, ChunkLayout::refined};

    public:
        // Builds what searches with `footing` use now (a battle's map load), not in a tick.
        void prepare(const GroundMap& ground, const Footing& footing)
        {
            pathfinder_.prepare(ground, footing);
        }

    private:
        // Short legs onto and off a shared path search at most this many nodes.
        GridPathfinder local_pathfinder_{4096};
        std::vector<std::uint32_t> query_;
        // Ground avoidance's scratch: a unit's nearest neighbours (squared distance, dense index) and
        // their ORCA lines.
        std::vector<std::pair<float, std::uint32_t>> nearest_;
        std::vector<OrcaLine> lines_;
        // Giving way's scratch: the requests of a tick (who steps aside, for whom, to where).
        struct GiveWayRequest
        {
            std::uint32_t yielder{};
            std::uint32_t passer{};
            Vector3 aside;
        };
        std::vector<GiveWayRequest> requests_;
        std::vector<std::uint32_t> room_query_;
        // Space avoidance's scratch: a ship's neighbours.
        std::vector<std::uint32_t> neighbours_;
    };
    NavigationSystem navigation_;

    // Behavior (simulation_behavior.cpp; docs/ecs-design.md, 6.3, phase 2): orders, stances, and
    // targets into steering intents and route requests.
    class BehaviorSystem
    {
    public:
        using View =
            SystemView<Read<IdentityColumns>, Read<PlacementColumns>, Write<LocomotorColumns>, Read<GroundMoverColumns>, Write<RouteColumns>, Write<ComponentPool<CombatColumns>>, Write<OrderColumns>,
                       Write<SteeringColumns>, Write<ComponentPool<GroupColumns>>, Write<ComponentPool<WalkClipColumns>>, Read<ComponentPool<FlightColumns>>, Read<GroundState>, Write<PathQueue>,
                       Read<SlotTable>, Read<Rules>, Read<Clock>, Write<ComponentPool<SquadColumns>>, Write<ComponentPool<SquadMemberColumns>>, Write<FormationFrames>>;
        // Every entity's steering intent for this tick, from its order, stance, and target; route
        // requests for new destinations.
        void run(const View& view, float dt);
        // Members released from their formation group so far (stalled far behind their slot).
        std::uint64_t released_members{};

    private:
        // Attack-move and guard: when to engage (a scan for hostiles), when to break off, and where to
        // go; the chase itself is the ordered-attack pursuit.
        void run_standing_order(const View& view, std::size_t index);
        // Ground and straight-line movers: chase an ordered target until in range, then hold.
        void pursue_ordered_target(const View& view, std::size_t index);
        // Ends an engagement the standing order started (the target and its order are dropped).
        void disengage(const View& view, std::size_t index);
        // Flyers: steer at the target (attack runs, or dogfights against flyers), the destination, or
        // the point they circle; reverse through a half-loop when that point is far behind.
        void plan_flight(const View& view, std::size_t index, float dt);
        // Squads (simulation_squads.cpp): a team chases its ordered target until its members reach
        // it, and members walk to their ring slots around their team, which waits for members far
        // behind and, under a standing order, while its members fight.
        void pursue_squad_target(const View& view, std::size_t index);
        void follow_squads(const View& view);
        // Formation frames (simulation_frames.cpp): each frame advances along its route unless a
        // member is far behind its slot, narrows its slots to the ground's clearance, and sends its
        // members to their slots; a lagger that came no closer for the wait limit leaves, and an
        // arrived frame dissolves once its members stand in their slots.
        void follow_frames(const View& view, float dt);
        // An arrived frame's members take its final slots by where they stand: the front ones the
        // front rank, left to right in each rank, among members of one footprint (a column out of a
        // narrow passage fans out into the block without crossing).
        void take_final_slots(const View& view, const FormationFrame& frame, std::span<const std::pair<std::size_t, std::size_t>> members);
        // The grid of positions targets are chosen from this tick, and its query scratch.
        SpatialGrid grid_;
        std::vector<std::uint32_t> query_;
        // Formation frames' scratch: each frame's members (dense index, group row), its slots, the
        // members' order for files, which members leave this tick, and those leaving with their goal.
        std::vector<std::vector<std::pair<std::size_t, std::size_t>>> frame_members_;
        std::vector<Vector3> slots_;
        std::vector<std::size_t> order_;
        std::vector<std::uint8_t> releasing_;
        std::vector<std::pair<std::size_t, Vector3>> releases_;
    };
    BehaviorSystem behavior_;

    // Combat (simulation_combat.cpp; docs/ecs-design.md, 6.3, phase 6): shields, targeting, weapons,
    // projectiles, and damage. Removing the dead is the removal system that follows it.
    class CombatSystem
    {
    public:
        using View = SystemView<Read<IdentityColumns>, Read<PlacementColumns>, Read<RouteColumns>, Write<ComponentPool<CombatColumns>>, Read<ComponentPool<FootprintColumns>>, Write<ProjectileState>,
                                Write<TickEvents>, Read<SlotTable>, Read<Rules>, Write<CombatRandom>, Read<Clock>, Write<CombatCounters>, Write<DamageQueue>, Read<ComponentPool<SquadColumns>>>;
        // Shield refresh, then every armed entity's target, turret, and weapons, then the projectiles
        // in flight.
        void run(const View& view, float dt);

    private:
        // Turns `index`'s turret toward turret_victim (CombatProfile::turret).
        void aim_turret(const View& view, std::size_t index, float dt);
        // Whether `victim` is within `weapon`'s range, beyond its minimum, and of a category it fires at.
        [[nodiscard]] bool in_reach(const View& view, std::size_t index, const WeaponProfile& weapon, std::size_t victim) const noexcept;
        void launch(const View& view, std::size_t shooter, std::size_t victim, const WeaponProfile& weapon, const LocalPoint& mount);
        void run_projectiles(const View& view);
        void run_weapon(const View& view, std::size_t index);
        // Whether this tick refreshes the entity's shield (simulation_combat.cpp).
        [[nodiscard]] bool shield_refresh_due(const View& view, std::size_t index) const noexcept;
        // Whether this tick is the entity's retarget beat (every retarget_seconds, at a phase from
        // its slot): when a weapon looks for a victim of its own.
        [[nodiscard]] bool retarget_due(const View& view, std::size_t index) const noexcept;
        // Where a shot of `weapon` at `victim` lands relative to its aim point (WeaponProfile::inaccuracy;
        // draws from the combat random stream), and whether that misses the victim.
        [[nodiscard]] Vector3 stray(const View& view, const WeaponProfile& weapon, std::size_t victim);
        // The entity a stray shot of `owner` landing at `point` hits instead of its target: the nearest
        // live, damageable, collidable entity hostile to `owner` whose footprint holds the point.
        [[nodiscard]] std::optional<std::size_t> stray_victim(const View& view, const Vector3& point, std::uint32_t owner, std::size_t target) const noexcept;
        [[nodiscard]] bool strays_off(const View& view, std::size_t victim, const Vector3& stray) const noexcept;
        // Whether `index`'s turret points at `victim` closely enough to fire (true without a turret).
        [[nodiscard]] bool turret_aimed(const View& view, std::size_t index, std::size_t victim) const noexcept;
        // The victim `index`'s turret turns toward: its own weapon's burst victim, else the entity's
        // target within that weapon's reach, else the nearest hostile within it.
        [[nodiscard]] std::optional<std::size_t> turret_victim(const View& view, std::size_t index);
        void update_target(const View& view, std::size_t index);
        // `mount` of `weapon`, turned with the turret when the weapon is on it.
        [[nodiscard]] LocalPoint weapon_mount(const View& view, std::size_t index, const WeaponProfile& weapon, const LocalPoint& mount) const noexcept;
        // The grid of positions targets are chosen from this tick, and its query scratch.
        SpatialGrid grid_;
        std::vector<std::uint32_t> query_;
    };
    CombatSystem combat_system_;

    // Publish (simulation.cpp; docs/ecs-design.md, 6.3, phase 9): the tick counter, and the snapshot
    // presentation, scripts, and tools read.
    class PublishSystem
    {
    public:
        using View = SystemView<Read<IdentityColumns>, Read<PlacementColumns>, Read<MotionColumns>, Read<LocomotorColumns>, Read<GroundMoverColumns>, Read<RouteColumns>,
                                Read<ComponentPool<CombatColumns>>, Read<OrderColumns>, Read<ComponentPool<TurnClipColumns>>, Read<ComponentPool<WalkClipColumns>>, Read<ComponentPool<FlightColumns>>,
                                Read<ProjectileState>, Read<TickEvents>, Write<Clock>, Read<ComponentPool<SquadMemberColumns>>>;
        // Advances the tick counter and publishes the tick's snapshot.
        void run(const View& view);
        // The snapshot of the last tick (SimulationWorld::snapshot).
        SimSnapshot snapshot;

    private:
        // Fills the snapshot from the entity table, the tick's events, and the projectiles in flight.
        void publish(const View& view);
    };
    PublishSystem publish_;
    std::uint32_t next_group_id_{1};
    std::vector<SimCommand> pending_;
    std::uint64_t commands_applied_{};
    std::uint64_t stale_references_{};
    std::uint64_t rejected_spawns_{};
    TickPhaseTimes phase_seconds_{};
    TickPhaseTimes slowest_phase_seconds_{};
};

// Presentation helper: blends two consecutive snapshots by `alpha` (0 = previous, 1 = current).
// Entities missing from `previous` (just spawned) use their current state.
[[nodiscard]] std::vector<EntitySnapshot> interpolate_snapshots(const SimSnapshot& previous, const SimSnapshot& current, float alpha);

} // namespace eaw
