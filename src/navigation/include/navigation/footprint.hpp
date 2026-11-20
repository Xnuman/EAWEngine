#pragma once

// Units' footprints as oriented rectangles (docs/pathfinding-design.md, 4.7): their shape in the
// entity's frame, and the measures every unit-against-unit spacing test shares.

#include "math/vector.hpp"

#include "junk/local_point.hpp"

#include <cmath>

namespace eaw {

// A footprint as an oriented rectangle: `half_length` along the entity's heading and `half_width`
// across it, about `center` in its local frame; a zero half-width means none (the circle of the
// entity's radius). A ground unit moving along its heading sweeps its width, so it plans with
// `half_width` as its radius.
struct FootprintRect
{
    bool operator==(const FootprintRect&) const = default;
    float half_length{};
    float half_width{};
    LocalPoint center;
};

// The rectangle's center on the ground for an entity at `at` facing `heading` (a local point turned
// as SimulationWorld::world_point turns it).
[[nodiscard]] inline Vector2 footprint_center_at(const FootprintRect& body, const Vector3& at, const float heading) noexcept
{
    const auto c = std::cos(heading);
    const auto s = std::sin(heading);
    return {at.x + body.center.forward * c - body.center.left * s, at.y + body.center.forward * s + body.center.left * c};
}

// The unit vector of a heading (cosine, sine): computed once where many extents are measured.
[[nodiscard]] inline Vector2 facing_of(const float heading) noexcept
{
    return {std::cos(heading), std::sin(heading)};
}

// How far the footprint of an entity facing `facing` (facing_of) reaches from its position toward
// `direction` (a unit vector on the ground): the rectangle's support distance, or `radius` without one.
[[nodiscard]] inline float footprint_extent(const FootprintRect& body, const float radius, const Vector2 facing, const Vector2 direction) noexcept
{
    if (body.half_width <= 0.0F)
    {
        return radius;
    }
    const auto along = direction.x * facing.x + direction.y * facing.y;
    const auto across = -direction.x * facing.y + direction.y * facing.x;
    return std::fabs(along) * body.half_length + std::fabs(across) * body.half_width + along * body.center.forward + across * body.center.left;
}

// The farthest the footprint reaches from the entity's position in any direction (broad phases).
[[nodiscard]] inline float footprint_reach(const FootprintRect& body, const float radius) noexcept
{
    return body.half_width <= 0.0F ? radius : std::hypot(body.half_length, body.half_width) + std::hypot(body.center.forward, body.center.left);
}

// Whether `point` lies within the footprint of an entity at `at` facing `heading`.
[[nodiscard]] inline bool footprint_contains(const FootprintRect& body, const float radius, const Vector3& at, const float heading, const Vector2 point) noexcept
{
    if (body.half_width <= 0.0F)
    {
        return std::hypot(point.x - at.x, point.y - at.y) < radius;
    }
    const auto center = footprint_center_at(body, at, heading);
    const auto dx = point.x - center.x;
    const auto dy = point.y - center.y;
    const auto c = std::cos(heading);
    const auto s = std::sin(heading);
    return std::fabs(dx * c + dy * s) < body.half_length && std::fabs(-dx * s + dy * c) < body.half_width;
}

// One unit's footprint where it stands, for the spacing tests below (`facing`: facing_of its heading).
struct PlacedFootprint
{
    Vector3 at;
    Vector2 facing;
    float radius{};
    FootprintRect body;
};

// How much room two footprints keep between their positions: each one's extent toward the other.
// Every unit-against-unit spacing test uses it (a rectangle side by side keeps its half-width, end to
// end its half-length; at a slant it errs to the side of room), so placement and avoidance agree.
[[nodiscard]] inline float footprints_reach(const PlacedFootprint& a, const PlacedFootprint& b) noexcept
{
    // Two circles (squads, soldiers, round units: most pairs) keep their radii.
    if (a.body.half_width <= 0.0F && b.body.half_width <= 0.0F)
    {
        return a.radius + b.radius;
    }
    const auto dx = b.at.x - a.at.x;
    const auto dy = b.at.y - a.at.y;
    const auto distance = std::hypot(dx, dy);
    if (distance <= 0.0F)
    {
        return footprint_reach(a.body, a.radius) + footprint_reach(b.body, b.radius);
    }
    const Vector2 toward{dx / distance, dy / distance};
    return footprint_extent(a.body, a.radius, a.facing, toward) + footprint_extent(b.body, b.radius, b.facing, {-toward.x, -toward.y});
}

// Whether two footprints overlap: closer than the room they keep (footprints_reach).
[[nodiscard]] inline bool footprints_overlap(const PlacedFootprint& a, const PlacedFootprint& b) noexcept
{
    return std::hypot(b.at.x - a.at.x, b.at.y - a.at.y) < footprints_reach(a, b);
}

} // namespace eaw
