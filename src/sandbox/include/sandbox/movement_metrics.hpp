#pragma once

// Movement measurements taken from a simulation's snapshots, tick by tick
// (docs/group-movement-research.md): how often ground units reverse the direction they turn in
// (turning left and right without settling), and how long they stand still while ordered to move.
// Measured outside the simulation, never part of its state; route changes are counted by the
// simulation itself (SimulationWorld::Stats::route_changes).

#include "simulation/simulation.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eaw {

// A reversal is a visible swing undone: a unit turns at least reversal_swing_radians (10 degrees) one
// way and then turns the other way within reversal_window_ticks (a second at 60 ticks a second) of
// the swing's start. Heading changes below turn_noise_radians a tick are steering noise and neither
// extend nor end a swing. A stall is a tick a unit ordered to move spends below stall_speed_fraction
// of its top speed. A freeze is a unit ordered to move whose position has not changed (by more than
// freeze_tolerance world units) for freeze_ticks (5 seconds at 60 ticks a second); counted once per
// episode. Engine choices for the measure.
inline constexpr float turn_noise_radians = 0.0005F;
inline constexpr std::uint64_t freeze_ticks = 300;
inline constexpr float freeze_tolerance = 0.01F;
inline constexpr float reversal_swing_radians = 0.17F;
inline constexpr std::uint64_t reversal_window_ticks = 60;
inline constexpr float stall_speed_fraction = 0.05F;

struct MovementMetrics
{
    std::uint64_t reversals{};
    std::uint64_t stalled_ticks{};
    std::uint64_t moving_ticks{};
    std::uint64_t freezes{};
};

// Observes ground units that move on their own (squad soldiers follow their team and are left out).
class MovementMeter
{
public:
    void observe(const SimSnapshot& snapshot);
    [[nodiscard]] const MovementMetrics& metrics() const noexcept
    {
        return metrics_;
    }

private:
    struct Track
    {
        std::uint32_t generation{};
        float heading{};
        std::int8_t turn{};
        // The current swing: radians turned in `turn`'s direction since it started.
        float swing{};
        std::uint64_t swing_start{};
        // Where it stood when it last moved, and the ticks since while ordered to move.
        Vector3 still_at;
        std::uint64_t still_ticks{};
    };
    // By entity index; a different generation starts a new track.
    std::vector<Track> tracks_;
    std::uint64_t tick_{};
    MovementMetrics metrics_;
};

// One line: reversals, stalled and moving unit-seconds, freezes, and the route changes by kind.
[[nodiscard]] std::string format_movement(const MovementMetrics& metrics, const SimulationWorld::Stats& stats, double tick_seconds);

} // namespace eaw
