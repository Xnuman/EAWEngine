#pragma once

// Clearance (docs/pathfinding-design.md, 4.1): how much room a unit's footprint has on each ground
// cell for a movement class, and what a unit may stand on.

#include "math/vector.hpp"
#include "simulation/ground_map.hpp"

#include <array>
#include <cstdint>
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

// The footprint sizes connectivity is kept for (docs/pathfinding-design.md, 4.1; the owner,
// 2026-11-15): radius up to 7, 20, 35, and 65, from the stock footprints (infantry up to 6.5, most
// vehicles up to 20, the large ones up to 35, giant vehicles up to 63). A unit uses the smallest
// that holds it; a larger one its own radius.
inline constexpr std::array<float, 4> footing_buckets{7.0F, 20.0F, 35.0F, 65.0F};
[[nodiscard]] float footing_bucket(float radius) noexcept;

// Whether `footing` fits the cell (its movement class's passability and, with a field, room for
// its radius).
[[nodiscard]] bool footing_fits(const GroundMap& ground, std::uint32_t cell, const Footing& footing) noexcept;

// Whether a unit with `footing` may stand at `point` (its cell's passability and clearance); true
// when the ground has no passability data.
[[nodiscard]] bool ground_stands(const GroundMap& ground, Vector2 point, const Footing& footing) noexcept;

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
