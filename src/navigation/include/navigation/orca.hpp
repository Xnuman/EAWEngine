#pragma once

// Optimal reciprocal collision avoidance (ORCA; van den Berg, Guy, Lin, and Manocha, "Reciprocal
// n-body collision avoidance", 2011): each neighbour forbids a half-plane of velocities that would
// bring the two into contact within a time horizon, and a unit takes the allowed velocity closest to
// the one it prefers. Plain 2D geometry, no simulation types (docs/pathfinding-design.md, 4.4).

#include "math/vector.hpp"

#include <span>

namespace eaw {

// The allowed velocities lie on the left of the line through `point` along `direction` (unit
// length): cross(direction, velocity - point) >= 0.
struct OrcaLine
{
    Vector2 point;
    Vector2 direction;
};

// The half-plane a neighbour imposes. `offset` is the neighbour's position minus the unit's,
// `relative_velocity` the unit's velocity minus the neighbour's, `reach` the sum of their radii;
// contact within `horizon` seconds is avoided (within `step` seconds when they already overlap, so
// they part within a tick). The unit takes `share` of the avoidance (1 when the neighbour does not
// react, 0.5 between equals); `velocity` is the unit's own.
[[nodiscard]] OrcaLine orca_line(Vector2 offset, Vector2 relative_velocity, float reach, float horizon, float step, float share, Vector2 velocity) noexcept;

// The velocity no faster than `max_speed` closest to `preferred` that every line allows; when none
// does (a dense crowd), the velocity that violates the lines least (by the largest distance into a
// forbidden half-plane).
[[nodiscard]] Vector2 orca_velocity(std::span<const OrcaLine> lines, float max_speed, Vector2 preferred);

} // namespace eaw
