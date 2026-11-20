#pragma once

// Chunks (docs/pathfinding-design.md, 4.1 and 4.2): the ground cut into squares of `chunk_cells`
// cells; per square, the connected parts of the cells a footing fits (4-connected, as the region
// labels: a diagonal step needs both side cells), and which parts of neighbouring squares touch. A
// route plans over the parts first; the cell search is then held to the corridor of parts found.

#include "navigation/clearance.hpp"
#include "navigation/ground_map.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace eaw {

struct ChunkGraph
{
    std::uint32_t passable_types{};
    std::uint32_t radius{};
    std::uint32_t chunk_cells{};
    // Per cell: its part (from 1), 0 where the footing does not fit.
    std::vector<std::uint32_t> part_of;
    // Per part (index part - 1): its representative cell (the part's cell nearest its centroid, ties
    // to the lower index) and its neighbours with the step cost between their representatives, in
    // edges[edge_starts[index] .. edge_starts[index + 1]).
    std::vector<std::uint32_t> center_of_part;
    std::vector<std::uint32_t> edge_starts;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;

    [[nodiscard]] std::size_t parts() const noexcept
    {
        return center_of_part.size();
    }
};

// The graph of the cells `footing` fits (at its radius) in squares of `chunk_cells` cells.
[[nodiscard]] ChunkGraph build_chunk_graph(const GroundMap& ground, const Footing& footing, std::uint32_t chunk_cells);

// The parts of the cheapest route over `graph` from `start` to `goal` (parts, both included), only
// through parts `allowed` accepts (indexed by part - 1; empty accepts every part); empty when there
// is none. A* with octile costs between representatives, ties by estimate, then part: deterministic.
// `expanded` counts the parts it expanded.
[[nodiscard]] std::vector<std::uint32_t> chunk_route(const GroundMap& ground, const ChunkGraph& graph, std::uint32_t start, std::uint32_t goal, const std::vector<std::uint8_t>& allowed,
                                                     std::uint32_t& expanded);

} // namespace eaw
