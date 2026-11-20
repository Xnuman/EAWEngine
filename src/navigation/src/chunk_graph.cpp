#include "navigation/chunk_graph.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace eaw {
namespace {

// The cell search's step costs (pathfinding.cpp), so part costs and cell costs agree.
constexpr std::uint32_t straight_cost = 10;
constexpr std::uint32_t diagonal_cost = 14;

std::uint32_t octile(const GroundMap& ground, const std::uint32_t from, const std::uint32_t to) noexcept
{
    const auto dx = static_cast<std::uint32_t>(std::abs(static_cast<long>(from % ground.width) - static_cast<long>(to % ground.width)));
    const auto dy = static_cast<std::uint32_t>(std::abs(static_cast<long>(from / ground.width) - static_cast<long>(to / ground.width)));
    return straight_cost * (dx + dy) - (2 * straight_cost - diagonal_cost) * std::min(dx, dy);
}

} // namespace

ChunkGraph build_chunk_graph(const GroundMap& ground, const Footing& footing, const std::uint32_t chunk_cells)
{
    ChunkGraph graph;
    graph.passable_types = footing.passable_types;
    graph.radius = static_cast<std::uint32_t>(std::max(0.0F, footing.radius));
    graph.chunk_cells = std::max(1U, chunk_cells);
    if (!ground.valid() || ground.passability.size() != ground.heights.size())
    {
        return graph;
    }
    const auto cells = ground.passability.size();
    graph.part_of.assign(cells, 0);
    const auto fits = [&](const std::uint32_t cell)
    {
        return footing_fits(ground, cell, footing);
    };
    // A diagonal step's side cells need half a cell less room (diagonal_side_allowance), as in the
    // cell search, so parts and links agree with it.
    const auto side = diagonal_side_footing(ground, footing);
    const auto side_fits = [&](const std::uint32_t column, const std::uint32_t row)
    {
        return footing_fits(ground, row * ground.width + column, side);
    };
    const auto size = graph.chunk_cells;
    std::vector<std::uint32_t> pending;
    std::vector<std::uint32_t> members;
    for (std::uint32_t chunk_y = 0; chunk_y * size < ground.height; ++chunk_y)
    {
        for (std::uint32_t chunk_x = 0; chunk_x * size < ground.width; ++chunk_x)
        {
            const auto first_x = chunk_x * size;
            const auto first_y = chunk_y * size;
            const auto last_x = std::min(ground.width, first_x + size) - 1U;
            const auto last_y = std::min(ground.height, first_y + size) - 1U;
            const CellRect square{first_x, first_y, last_x, last_y};
            for (auto y = first_y; y <= last_y; ++y)
            {
                for (auto x = first_x; x <= last_x; ++x)
                {
                    const auto seed = y * ground.width + x;
                    if (graph.part_of[seed] != 0 || !fits(seed))
                    {
                        continue;
                    }
                    // A new part: flood its square, then take the cell nearest its centroid.
                    const auto part = static_cast<std::uint32_t>(graph.center_of_part.size() + 1U);
                    graph.part_of[seed] = part;
                    pending.assign(1, seed);
                    members.clear();
                    double sum_x = 0.0;
                    double sum_y = 0.0;
                    while (!pending.empty())
                    {
                        const auto cell = pending.back();
                        pending.pop_back();
                        members.push_back(cell);
                        const auto cx = cell % ground.width;
                        const auto cy = cell / ground.width;
                        sum_x += cx;
                        sum_y += cy;
                        for_each_step_neighbor(cell, ground.width, square, side_fits,
                                               [&](const std::uint32_t neighbor)
                                               {
                                                   if (graph.part_of[neighbor] == 0 && fits(neighbor))
                                                   {
                                                       graph.part_of[neighbor] = part;
                                                       pending.push_back(neighbor);
                                                   }
                                               });
                    }
                    const auto mean_x = sum_x / static_cast<double>(members.size());
                    const auto mean_y = sum_y / static_cast<double>(members.size());
                    auto best = members.front();
                    auto best_distance = std::numeric_limits<double>::max();
                    for (const auto cell : members)
                    {
                        const auto dx = static_cast<double>(cell % ground.width) - mean_x;
                        const auto dy = static_cast<double>(cell / ground.width) - mean_y;
                        const auto distance = dx * dx + dy * dy;
                        if (distance < best_distance || (distance == best_distance && cell < best))
                        {
                            best = cell;
                            best_distance = distance;
                        }
                    }
                    graph.center_of_part.push_back(best);
                }
            }
        }
    }
    // Neighbours: parts of adjacent cells across a square's border, side by side or diagonally where a
    // diagonal step may be taken (inside a square, such cells are one part).
    std::vector<std::pair<std::uint32_t, std::uint32_t>> links;
    for (std::uint32_t cell = 0; cell < cells; ++cell)
    {
        const auto part = graph.part_of[cell];
        if (part == 0)
        {
            continue;
        }
        const auto x = cell % ground.width;
        const auto y = cell / ground.width;
        const auto link = [&](const std::uint32_t neighbor)
        {
            const auto other = graph.part_of[neighbor];
            if (neighbor != cell && other != 0 && other != part)
            {
                links.emplace_back(part, other);
                links.emplace_back(other, part);
            }
        };
        link(x + 1U < ground.width ? cell + 1U : cell);
        link(y + 1U < ground.height ? cell + ground.width : cell);
        // Down-right and down-left: each diagonal pair once.
        if (y + 1U < ground.height && x + 1U < ground.width && side_fits(x + 1U, y) && side_fits(x, y + 1U))
        {
            link(cell + ground.width + 1U);
        }
        if (y + 1U < ground.height && x > 0U && side_fits(x - 1U, y) && side_fits(x, y + 1U))
        {
            link(cell + ground.width - 1U);
        }
    }
    std::ranges::sort(links);
    links.erase(std::unique(links.begin(), links.end()), links.end());
    graph.edge_starts.assign(graph.parts() + 1U, 0);
    for (const auto& [from, to] : links)
    {
        ++graph.edge_starts[from];
    }
    for (std::size_t index = 1; index < graph.edge_starts.size(); ++index)
    {
        graph.edge_starts[index] += graph.edge_starts[index - 1];
    }
    graph.edges.reserve(links.size());
    for (const auto& [from, to] : links)
    {
        graph.edges.emplace_back(to, octile(ground, graph.center_of_part[from - 1U], graph.center_of_part[to - 1U]));
    }
    return graph;
}

std::vector<std::uint32_t> chunk_route(const GroundMap& ground, const ChunkGraph& graph, const std::uint32_t start, const std::uint32_t goal, const std::vector<std::uint8_t>& allowed,
                                       std::uint32_t& expanded)
{
    std::vector<std::uint32_t> route;
    const auto parts = graph.parts();
    if (start == 0 || goal == 0 || start > parts || goal > parts)
    {
        return route;
    }
    const auto accepts = [&](const std::uint32_t part)
    {
        return allowed.empty() || allowed[part - 1U] != 0;
    };
    struct Open
    {
        std::uint32_t estimate{};
        std::uint32_t heuristic{};
        std::uint32_t part{};
    };
    const auto later = [](const Open& left, const Open& right)
    {
        if (left.estimate != right.estimate)
        {
            return left.estimate > right.estimate;
        }
        if (left.heuristic != right.heuristic)
        {
            return left.heuristic > right.heuristic;
        }
        return left.part > right.part;
    };
    constexpr auto unseen = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> cost(parts + 1U, unseen);
    std::vector<std::uint32_t> parent(parts + 1U, 0);
    std::vector<std::uint8_t> closed(parts + 1U, 0);
    std::vector<Open> open;
    const auto goal_center = graph.center_of_part[goal - 1U];
    const auto heuristic = [&](const std::uint32_t part)
    {
        return octile(ground, graph.center_of_part[part - 1U], goal_center);
    };
    cost[start] = 0;
    open.push_back({heuristic(start), heuristic(start), start});
    while (!open.empty())
    {
        std::ranges::pop_heap(open, later);
        const auto node = open.back();
        open.pop_back();
        if (closed[node.part] != 0)
        {
            continue;
        }
        closed[node.part] = 1;
        ++expanded;
        if (node.part == goal)
        {
            for (auto part = goal; part != start; part = parent[part])
            {
                route.push_back(part);
            }
            route.push_back(start);
            std::ranges::reverse(route);
            return route;
        }
        for (auto edge = graph.edge_starts[node.part - 1U]; edge < graph.edge_starts[node.part]; ++edge)
        {
            const auto [next, step] = graph.edges[edge];
            if (closed[next] != 0 || !accepts(next))
            {
                continue;
            }
            const auto next_cost = cost[node.part] + step;
            if (next_cost < cost[next])
            {
                cost[next] = next_cost;
                parent[next] = node.part;
                open.push_back({next_cost + heuristic(next), heuristic(next), next});
                std::ranges::push_heap(open, later);
            }
        }
    }
    return route;
}

} // namespace eaw
