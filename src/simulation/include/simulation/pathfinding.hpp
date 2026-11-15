#pragma once

// Grid pathfinding over a GroundMap's passability cells. Deterministic: integer step costs and a
// fully ordered open list make the same request on the same ground return the same waypoints.

#include "math/vector.hpp"
#include "simulation/chunk_graph.hpp"
#include "simulation/clearance.hpp"
#include "simulation/ground_map.hpp"

#include <cstdint>
#include <deque>
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
// `point` when its ground cell's passability is among `passable_types`, else the center of the
// nearest such cell (Euclidean; ties to the lower index); nothing without passability data or
// when no cell qualifies.
[[nodiscard]] std::optional<PathPoint> nearest_enterable(const GroundMap& ground, PathPoint point, const Footing& footing);

// How a search uses chunks (chunk_graph.hpp; docs/pathfinding-design.md, 4.2): not at all, one level
// of 16-cell squares, or 32-cell squares over the 16-cell ones (the two compared by the path audit).
enum class ChunkLayout : std::uint8_t
{
    none,
    one_level,
    two_level,
};
inline constexpr std::uint32_t fine_chunk_cells = 16;
inline constexpr std::uint32_t coarse_chunk_cells = 32;

class GridPathfinder final
{
public:
    // At most `max_expansions` nodes per search (cells, and with chunks the parts as well); zero means
    // every cell of the map. With chunks, a search first plans over the chunk graph of its footing's
    // size bucket and holds the cell search to the corridor of parts it found.
    explicit GridPathfinder(std::uint32_t max_expansions = 0, ChunkLayout layout = ChunkLayout::none) noexcept;

    [[nodiscard]] PathResult find(const GroundMap& ground, PathPoint start, PathPoint goal, const Footing& footing);
    // Builds now what searches with `footing` use (its size bucket's chunk graph), so the first
    // search does not.
    void prepare(const GroundMap& ground, const Footing& footing);
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

    // The chunk graph of `footing` (at its radius) in squares of `chunk_cells`, built on first use.
    [[nodiscard]] const ChunkGraph& chunks(const GroundMap& ground, const Footing& footing, std::uint32_t chunk_cells);

    std::uint32_t max_expansions_;
    ChunkLayout layout_;
    // A deque: a graph's reference stays valid while another is built.
    std::deque<ChunkGraph> chunk_graphs_;
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
