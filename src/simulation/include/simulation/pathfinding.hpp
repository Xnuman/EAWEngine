#pragma once

// Grid pathfinding over a GroundMap's passability cells. Deterministic: integer step costs and a
// fully ordered open list make the same request on the same ground return the same waypoints.

#include "math/vector.hpp"
#include "simulation/ground_map.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace eaw {

// A point in the ground plane (the pathfinder's name for eaw_math's Vector2).
using PathPoint = Vector2;

enum class PathStatus : std::uint8_t
{
    // The waypoints end at the goal.
    reached,
    // The goal lies on a cell the class may not enter, in a region it cannot reach, or beyond the
    // search budget: the waypoints end at the closest point found (possibly none).
    partial,
    // The ground has no passability data, or the request is not finite: no constraint applies.
    unavailable,
};

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

struct PathResult
{
    PathStatus status{PathStatus::unavailable};
    // Points after the start, ending at the goal or the closest reachable point.
    std::vector<PathPoint> waypoints;
    // Nodes expanded by the search (a cost measure).
    std::uint32_t expanded{};
};

// Whether the straight segment crosses only cells whose type is in `passable_types`. Cells are the
// nearest-sample regions of GroundMap::passability_at, traversed exactly; a segment through a cell
// corner needs both side cells. The cell containing `from` is not checked, so a unit may leave a
// cell it should not stand on; cells off the map are not passable. True when the ground has no
// passability data.
[[nodiscard]] bool ground_line_passable(const GroundMap& ground, PathPoint from, PathPoint to, const Footing& footing);
// Whether a unit with `footing` may stand at `point` (its cell's passability and clearance); true
// when the ground has no passability data.
[[nodiscard]] bool ground_stands(const GroundMap& ground, PathPoint point, const Footing& footing) noexcept;

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
// `point` when its ground cell's passability is among `passable_types`, else the center of the
// nearest such cell (Euclidean; ties to the lower index); nothing without passability data or
// when no cell qualifies.
[[nodiscard]] std::optional<PathPoint> nearest_enterable(const GroundMap& ground, PathPoint point, const Footing& footing);

class GridPathfinder final
{
public:
    // At most `max_expansions` nodes per search; zero means every cell of the map.
    explicit GridPathfinder(std::uint32_t max_expansions = 0) noexcept;

    [[nodiscard]] PathResult find(const GroundMap& ground, PathPoint start, PathPoint goal, const Footing& footing);
    // Forgets the cached region labels.
    void reset() noexcept;

private:
    // Per movement class and, with clearance, per whole-unit radius (each radius its own labels:
    // a unit fits through what its radius fits through, no more and no less).
    struct RegionLabels
    {
        std::uint32_t passable_types{};
        std::uint32_t radius{};
        // Per cell: 0 when the class may not enter it, otherwise its connected region (from 1).
        std::vector<std::uint32_t> labels;
    };

    [[nodiscard]] const std::vector<std::uint32_t>& regions(const GroundMap& ground, const Footing& footing);

    struct OpenNode
    {
        std::uint32_t estimate{};
        std::uint32_t heuristic{};
        std::uint32_t cell{};
    };

    std::uint32_t max_expansions_;
    // Per-cell search stamps: a cell's cost and parent are valid when seen_ equals search_, and it
    // is closed when closed_ does. Stamps avoid clearing the arrays for every search.
    std::uint32_t search_{};
    std::vector<std::uint32_t> seen_;
    std::vector<std::uint32_t> closed_;
    std::vector<std::uint32_t> cost_;
    std::vector<std::uint32_t> parent_;
    std::vector<OpenNode> open_;
    std::vector<RegionLabels> regions_;
};

} // namespace eaw
