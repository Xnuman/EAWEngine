#pragma once

// Grid pathfinding over a GroundMap's passability cells. Deterministic: integer step costs and a
// fully ordered open list make the same request on the same ground return the same waypoints.

#include "eaw/ground_map.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace eaw {

struct PathPoint
{
    float x{};
    float y{};

    friend bool operator==(const PathPoint&, const PathPoint&) = default;
};

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
[[nodiscard]] bool ground_line_passable(const GroundMap& ground, PathPoint from, PathPoint to, std::uint32_t passable_types);

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
[[nodiscard]] std::optional<PathPoint> nearest_enterable(const GroundMap& ground, PathPoint point, std::uint32_t passable_types);

class GridPathfinder final
{
public:
    // At most `max_expansions` nodes per search; zero means every cell of the map.
    explicit GridPathfinder(std::uint32_t max_expansions = 0) noexcept;

    [[nodiscard]] PathResult find(const GroundMap& ground, PathPoint start, PathPoint goal, std::uint32_t passable_types);
    // Forgets the cached region labels.
    void reset() noexcept;

private:
    struct RegionLabels
    {
        std::uint32_t passable_types{};
        // Per cell: 0 when the class may not enter it, otherwise its connected region (from 1).
        std::vector<std::uint32_t> labels;
    };

    [[nodiscard]] const std::vector<std::uint32_t>& regions(const GroundMap& ground, std::uint32_t passable_types);

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
