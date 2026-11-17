#pragma once

// Clearance (docs/pathfinding-design.md, 4.1): how much room a unit's footprint has on each ground
// cell for a movement class, and what a unit may stand on.

#include "math/vector.hpp"
#include "simulation/ground_map.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace eaw {

// How far each cell's center lies from ground a movement class may not stand on (cells whose
// passability is not in `passable_types`, and the area off the map), in world units, up to `limit`
// (docs/pathfinding-design.md, 4.1): a unit of radius r fits on a cell whose clearance is at least
// r. Cells are the nearest-sample squares of GroundMap::passability_at.
struct ClearanceField
{
    bool operator==(const ClearanceField&) const = default;
    std::uint32_t passable_types{};
    float limit{};
    std::vector<float> cells;
};

// A rectangle of cells, both ends included.
struct CellRect
{
    bool operator==(const CellRect&) const = default;
    std::uint32_t first_column{};
    std::uint32_t first_row{};
    std::uint32_t last_column{};
    std::uint32_t last_row{};
};

// Every cell of the ground (call only for a ground with cells).
[[nodiscard]] inline CellRect whole_ground(const GroundMap& ground)
{
    return {0, 0, ground.width - 1U, ground.height - 1U};
}

// Calls visit(neighbour) for each side neighbour of `cell` inside `bounds`, in the order left, right,
// up, down (callers that keep the first of equal candidates depend on it); `width` is the grid's row
// length.
template <typename Visit> void for_each_side_neighbor(const std::uint32_t cell, const std::uint32_t width, const CellRect& bounds, Visit&& visit)
{
    const auto x = cell % width;
    const auto y = cell / width;
    if (x > bounds.first_column)
    {
        visit(cell - 1U);
    }
    if (x < bounds.last_column)
    {
        visit(cell + 1U);
    }
    if (y > bounds.first_row)
    {
        visit(cell - width);
    }
    if (y < bounds.last_row)
    {
        visit(cell + width);
    }
}

// Calls visit(neighbour) for each side neighbour of `cell` inside `bounds` (as for_each_side_neighbor),
// then for each diagonal neighbour inside them whose two side cells pass side_fits(column, row), in
// the order up-left, up-right, down-left, down-right.
template <typename SideFits, typename Visit> void for_each_step_neighbor(const std::uint32_t cell, const std::uint32_t width, const CellRect& bounds, SideFits&& side_fits, Visit&& visit)
{
    for_each_side_neighbor(cell, width, bounds, visit);
    const auto x = cell % width;
    const auto y = cell / width;
    for (const auto [dx, dy] : {std::pair{-1, -1}, std::pair{1, -1}, std::pair{-1, 1}, std::pair{1, 1}})
    {
        const auto nx = static_cast<long>(x) + dx;
        const auto ny = static_cast<long>(y) + dy;
        if (nx < static_cast<long>(bounds.first_column) || nx > static_cast<long>(bounds.last_column) || ny < static_cast<long>(bounds.first_row) || ny > static_cast<long>(bounds.last_row))
        {
            continue;
        }
        if (side_fits(static_cast<std::uint32_t>(nx), y) && side_fits(x, static_cast<std::uint32_t>(ny)))
        {
            visit(static_cast<std::uint32_t>(ny) * width + static_cast<std::uint32_t>(nx));
        }
    }
}

// The field of `passable_types` up to `limit` over the whole ground.
[[nodiscard]] ClearanceField clearance_field(const GroundMap& ground, std::uint32_t passable_types, float limit);
// Recomputes the cells whose clearance a change of passability within `changed` can affect.
void update_clearance(ClearanceField& field, const GroundMap& ground, CellRect changed);

// What a unit may stand on: its movement class's cells and, with a clearance field, only those with
// room for its radius. A plain mask converts to a footing without clearance (any radius fits).
struct Footing
{
    Footing(std::uint32_t mask) noexcept : passable_types(mask)
    {}
    Footing(std::uint32_t mask, const ClearanceField* field, float unit_radius) noexcept : passable_types(mask), clearance(field), radius(unit_radius)
    {}
    std::uint32_t passable_types{};
    const ClearanceField* clearance{};
    float radius{};
};

// A diagonal step's side cells need half a cell less clearance than the cells it joins: clearance is
// known only at cell centres, the step passes 14 units from each side cell's centre, and demanding
// the full radius there closed diagonal gaps whose centre line has room (the owner's report,
// 2026-11-17: Utapau's gate, a 42-clearance centre line between side cells of 32, kept the
// Gargantuan, footing 37.3, out). An engine choice; a hull may graze a wall corner by up to half a
// cell on such a step. Passability itself is not relaxed: a side cell the class may not enter at all
// still closes the step.
inline constexpr float diagonal_side_allowance = 0.5F; // of a cell's spacing
// The footing a diagonal step's side cells must fit: `footing` with its radius less the allowance
// (no less than zero); a footing without a clearance field is unchanged.
[[nodiscard]] Footing diagonal_side_footing(const GroundMap& ground, const Footing& footing) noexcept;

// The footprint sizes connectivity is kept for (docs/pathfinding-design.md, 4.1; the owner,
// 2026-11-15, after 7, 20, 35, and 65 kept units out of gaps they fit): radius every 2 units, 2 to 68
// (stock footprints: infantry up to 6.5, most vehicles up to 20, the large ones up to 35, giant
// vehicles up to 63). A unit uses the smallest that holds it, so it avoids only gaps less than 4
// units narrower than it needs; a larger one its own radius. Graphs exist only for the sizes a map
// uses.
inline constexpr float footing_bucket_step = 2.0F;
inline constexpr auto footing_buckets = []
{
    std::array<float, 34> buckets{};
    for (std::size_t bucket = 0; bucket < buckets.size(); ++bucket)
    {
        buckets[bucket] = footing_bucket_step * static_cast<float>(bucket + 1U);
    }
    return buckets;
}();
[[nodiscard]] float footing_bucket(float radius) noexcept;

// Whether `footing` fits the cell (its movement class's passability and, with a field, room for
// its radius).
[[nodiscard]] bool footing_fits(const GroundMap& ground, std::uint32_t cell, const Footing& footing) noexcept;

// Whether a unit with `footing` may stand at `point` (its cell's passability and clearance, or half a
// cell less room beside a cell with the full room, where a diagonal step passes:
// diagonal_side_allowance); true when the ground has no passability data.
[[nodiscard]] bool ground_stands(const GroundMap& ground, Vector2 point, const Footing& footing) noexcept;
// Calls visit(cell) for each cell whose sample point (column x spacing, row x spacing) lies in the
// rectangle of `half_length` along `heading` and `half_width` across it about `center`, and returns
// the window of cells looked at (nothing when it is empty). Placed obstacles' footprints are stamped
// with it, and units' rectangles tested (pathfinding-design.md 4.7).
template <typename Visit>
std::optional<CellRect> for_each_cell_in_rectangle(const GroundMap& ground, const Vector2 center, const float heading, const float half_length, const float half_width, Visit&& visit)
{
    const auto cos_heading = std::cos(heading);
    const auto sin_heading = std::sin(heading);
    const auto reach = std::hypot(half_length, half_width);
    const auto spacing = ground.spacing;
    const auto first_column = std::max(0L, static_cast<long>(std::floor((center.x - reach) / spacing)));
    const auto last_column = std::min(static_cast<long>(ground.width) - 1, static_cast<long>(std::ceil((center.x + reach) / spacing)));
    const auto first_row = std::max(0L, static_cast<long>(std::floor((center.y - reach) / spacing)));
    const auto last_row = std::min(static_cast<long>(ground.height) - 1, static_cast<long>(std::ceil((center.y + reach) / spacing)));
    for (auto row = first_row; row <= last_row; ++row)
    {
        for (auto column = first_column; column <= last_column; ++column)
        {
            const auto dx = static_cast<float>(column) * spacing - center.x;
            const auto dy = static_cast<float>(row) * spacing - center.y;
            const auto along = dx * cos_heading + dy * sin_heading;
            const auto across = -dx * sin_heading + dy * cos_heading;
            if (std::fabs(along) > half_length || std::fabs(across) > half_width)
            {
                continue;
            }
            visit(static_cast<std::size_t>(row) * ground.width + static_cast<std::size_t>(column));
        }
    }
    if (first_column > last_column || first_row > last_row)
    {
        return std::nullopt;
    }
    return CellRect{static_cast<std::uint32_t>(first_column), static_cast<std::uint32_t>(first_row), static_cast<std::uint32_t>(last_column), static_cast<std::uint32_t>(last_row)};
}

// Whether a unit's rectangle (pathfinding-design.md 4.7) may stand there: every cell under it of a
// passability in `passable_types`, its center's cell too (a rectangle narrower than a cell may hold
// no sample point); true when the ground has no passability data.
[[nodiscard]] bool ground_rectangle_fits(const GroundMap& ground, Vector2 center, float heading, float half_length, float half_width, std::uint32_t passable_types) noexcept;

// A* over the passability cells (one node per elevation sample): 8-connected, steps cost 10
// straight and 14 diagonal, a diagonal step needs both side cells, and ties break by lower
// heuristic, then lower cell index. A start on a cell the class may not enter (a unit spawned or
// stranded there) first leaves in a straight line for the nearest enterable cell; a goal on such a
// cell, or in a region the start cannot reach, moves to the nearest cell of the start's region
// (regions are labeled once per class mask and cached). Equally near cells prefer the side of the
// other end of the path. The cell path is then shortened: a waypoint is
// dropped when a straight line from the previous kept point to the one after it stays passable.
//
// Scratch storage and region labels are reused between searches, so a pathfinder is cheap to call
// repeatedly but not thread-safe, and reset() must be called when the ground's passability changes.

} // namespace eaw
