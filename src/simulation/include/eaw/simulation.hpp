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

#include "eaw/ground_map.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace eaw {

struct SimVector3
{
    float x{};
    float y{};
    float z{};

    friend bool operator==(const SimVector3&, const SimVector3&) = default;
};

// Generation-checked entity reference. Generation 0 is never issued, so a default id is invalid and a
// destroyed entity's id never aliases a later entity in the same slot.
struct EntityId
{
    std::uint32_t index{};
    std::uint32_t generation{};

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return generation != 0;
    }
    friend constexpr bool operator==(const EntityId&, const EntityId&) = default;
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
    // Moves over the world's ground (height from the surface, stopped by blocked cells) instead of
    // in a straight 3D line.
    bool follows_ground{};
};

enum class SimCommandKind : std::uint8_t
{
    move,
    stop,
    destroy,
};

struct SimCommand
{
    SimCommandKind kind{SimCommandKind::move};
    std::vector<EntityId> entities;
    // Destination for move commands.
    SimVector3 target;
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
};

struct SimSnapshot
{
    std::uint64_t tick{};
    std::vector<EntitySnapshot> entities;
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
        // Ground moves stopped at a blocked passability cell.
        std::uint64_t blocked_moves{};
    };

    explicit SimulationWorld(double tick_seconds = 1.0 / 30.0);

    // Spawning is immediate and intended for setup between ticks (map load, reinforcements). Spawn
    // data comes from map and mod content: a non-finite position, heading, or speed, or full entity
    // storage, returns an error and spawns nothing.
    [[nodiscard]] Result<EntityId> spawn(const EntitySpawn& spawn);
    // Ground for entities that follow it. `blocked_classes` is a mask of raw passability values that
    // stop ground movement (provisionally only passability::blocked; see ground_map.hpp). An entity
    // standing on a blocked cell may still leave it.
    void set_ground(GroundMap ground, std::uint32_t blocked_classes = passability::blocked);
    [[nodiscard]] const GroundMap& ground() const noexcept;

    // Queues a command for the next step(); commands apply in submission order.
    void submit(SimCommand command);
    // Applies queued commands, runs the systems once, and publishes a new snapshot.
    void step();

    [[nodiscard]] bool alive(EntityId id) const noexcept;
    [[nodiscard]] const SimSnapshot& snapshot() const noexcept;
    [[nodiscard]] Stats stats() const noexcept;
    // FNV-1a over every component, in storage order: equal hashes mean identical simulation state.
    [[nodiscard]] std::uint64_t state_hash() const noexcept;

private:
    [[nodiscard]] std::optional<std::size_t> dense_index(EntityId id) const noexcept;
    void apply(const SimCommand& command);
    void destroy(std::size_t dense);
    void run_movement();
    void move_over_ground(std::size_t index, float dt);
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
    std::uint32_t blocked_classes_{};
    std::uint64_t blocked_moves_{};
    std::vector<SimCommand> pending_;
    SimSnapshot snapshot_;
    std::uint64_t commands_applied_{};
    std::uint64_t stale_references_{};
};

// Presentation helper: blends two consecutive snapshots by `alpha` (0 = previous, 1 = current).
// Entities missing from `previous` (just spawned) use their current state.
[[nodiscard]] std::vector<EntitySnapshot> interpolate_snapshots(const SimSnapshot& previous, const SimSnapshot& current, float alpha);

} // namespace eaw
