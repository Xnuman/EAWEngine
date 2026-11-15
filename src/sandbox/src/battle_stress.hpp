#pragma once

// Orders for a stress battle (EAWEngine battle --stress-orders): the spawned units, in groups, get a
// new order every second, the groups staggered over the ticks, so routes are planned and replanned
// all the time. The orders cycle through move, attack-move, attack, and guard, to points drawn from
// a fixed-seed stream around the battle's center (on open ground on land) and to hostile targets.
// The spawned units cannot be killed, so the load stays that of every unit. The orders go through
// Battle::submit, so they are recorded and replay like any other order.

#include "math/vector.hpp"
#include "core/random.hpp"
#include "simulation/simulation.hpp"

#include <cstdint>
#include <vector>

namespace eaw {

class Battle;
struct GroundMap;

class StressOrders final
{
public:
    // `ground` is the land map's ground (points are moved to open ground on it), or null in space.
    // `placed` are the entities the map placed, which take no orders.
    StressOrders(const Vector3& center, const GroundMap* ground, std::vector<EntityId> placed, double tick_seconds);

    // Submits the orders due at `tick`.
    void issue(Battle& battle, std::uint64_t tick);

private:
    [[nodiscard]] Vector3 random_point();

    Vector3 center_;
    const GroundMap* ground_;
    std::vector<EntityId> placed_;
    std::uint64_t period_ticks_;
    Pcg32 random_;
    std::uint64_t orders_{};
};

} // namespace eaw
