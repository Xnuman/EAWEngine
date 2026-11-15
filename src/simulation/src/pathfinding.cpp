#include "simulation/pathfinding.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <optional>

namespace eaw {
namespace {

constexpr std::uint32_t straight_cost = 10;
constexpr std::uint32_t diagonal_cost = 14;

bool finite(const PathPoint point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

// Keeps a point within the sampled area, where every point has a nearest cell.
PathPoint clamp_to_ground(const GroundMap& ground, const PathPoint point) noexcept
{
    return {std::clamp(point.x, 0.0F, static_cast<float>(ground.width - 1) * ground.spacing), std::clamp(point.y, 0.0F, static_cast<float>(ground.height - 1) * ground.spacing)};
}

std::uint32_t cell_of(const GroundMap& ground, const PathPoint point) noexcept
{
    const auto column = static_cast<std::uint32_t>(std::clamp(std::lround(point.x / ground.spacing), 0L, static_cast<long>(ground.width) - 1));
    const auto row = static_cast<std::uint32_t>(std::clamp(std::lround(point.y / ground.spacing), 0L, static_cast<long>(ground.height) - 1));
    return row * ground.width + column;
}

PathPoint cell_center(const GroundMap& ground, const std::uint32_t cell) noexcept
{
    return {static_cast<float>(cell % ground.width) * ground.spacing, static_cast<float>(cell / ground.width) * ground.spacing};
}

bool enterable(const GroundMap& ground, const std::uint32_t cell, const Footing& footing) noexcept
{
    return footing_fits(ground, cell, footing);
}

// The whole-unit radius regions are labeled for (0 without clearance).
std::uint32_t region_radius(const Footing& footing) noexcept
{
    return footing.clearance != nullptr && footing.radius > 0.0F ? static_cast<std::uint32_t>(std::ceil(footing.radius)) : 0U;
}

// Octile distance in step-cost units; consistent with the step costs, so A* stays optimal.
std::uint32_t heuristic(const GroundMap& ground, const std::uint32_t from, const std::uint32_t to) noexcept
{
    const auto dx = static_cast<std::uint32_t>(std::abs(static_cast<long>(from % ground.width) - static_cast<long>(to % ground.width)));
    const auto dy = static_cast<std::uint32_t>(std::abs(static_cast<long>(from / ground.width) - static_cast<long>(to / ground.width)));
    return straight_cost * (dx + dy) - (2 * straight_cost - diagonal_cost) * std::min(dx, dy);
}

// The cell accepted by `wanted` nearest to `point` (Euclidean), searched in growing square rings
// around its cell. Ties go to the cell nearer to `toward` (the other end of the path), then the
// lower index.
template <class Wanted> std::optional<std::uint32_t> nearest_cell(const GroundMap& ground, const PathPoint point, const PathPoint toward, Wanted&& wanted)
{
    const auto center = cell_of(ground, point);
    const auto center_x = static_cast<long>(center % ground.width);
    const auto center_y = static_cast<long>(center / ground.width);
    const auto max_radius = static_cast<long>(std::max(ground.width, ground.height));
    std::optional<std::uint32_t> best;
    float best_distance = std::numeric_limits<float>::infinity();
    float best_toward = std::numeric_limits<float>::infinity();
    long stop_radius = max_radius;
    for (long radius = 1; radius <= stop_radius; ++radius)
    {
        for (long y = center_y - radius; y <= center_y + radius; ++y)
        {
            if (y < 0 || y >= static_cast<long>(ground.height))
            {
                continue;
            }
            const bool edge_row = y == center_y - radius || y == center_y + radius;
            for (long x = center_x - radius; x <= center_x + radius; x += edge_row ? 1 : 2 * radius)
            {
                if (x < 0 || x >= static_cast<long>(ground.width))
                {
                    continue;
                }
                const auto cell = static_cast<std::uint32_t>(y) * ground.width + static_cast<std::uint32_t>(x);
                if (!wanted(cell))
                {
                    continue;
                }
                const auto position = cell_center(ground, cell);
                const auto distance = (position.x - point.x) * (position.x - point.x) + (position.y - point.y) * (position.y - point.y);
                const auto toward_distance = (position.x - toward.x) * (position.x - toward.x) + (position.y - toward.y) * (position.y - toward.y);
                if (distance < best_distance || (distance == best_distance && (toward_distance < best_toward || (toward_distance == best_toward && best && cell < *best))))
                {
                    best = cell;
                    best_distance = distance;
                    best_toward = toward_distance;
                }
            }
        }
        if (best && stop_radius == max_radius)
        {
            // A cell on a later ring can still be nearer (ring corners lie sqrt(2) farther out).
            stop_radius = std::min(max_radius, static_cast<long>(std::ceil(static_cast<double>(radius) * std::numbers::sqrt2)) + 1);
        }
    }
    return best;
}

} // namespace

std::optional<PathPoint> nearest_enterable(const GroundMap& ground, const PathPoint point, const Footing& footing)
{
    if (!ground.valid() || ground.passability.size() != ground.heights.size() || footing.passable_types == 0)
    {
        return std::nullopt;
    }
    if (enterable(ground, cell_of(ground, point), footing))
    {
        return point;
    }
    const auto cell = nearest_cell(ground, point, point,
                                   [&](const std::uint32_t candidate)
                                   {
                                       return enterable(ground, candidate, footing);
                                   });
    return cell ? std::optional<PathPoint>{cell_center(ground, *cell)} : std::nullopt;
}

bool ground_line_passable(const GroundMap& ground, const PathPoint from, const PathPoint to, const Footing& footing)
{
    if (!ground.has_passability() || !finite(from) || !finite(to))
    {
        return true;
    }
    // Cell (i, j) covers [i - 0.5, i + 0.5) in grid units, so shifting by half a cell makes cell
    // boundaries the integers that a standard grid traversal steps across.
    const auto u0 = from.x / ground.spacing + 0.5F;
    const auto v0 = from.y / ground.spacing + 0.5F;
    const auto u1 = to.x / ground.spacing + 0.5F;
    const auto v1 = to.y / ground.spacing + 0.5F;
    auto x = static_cast<long>(std::floor(u0));
    auto y = static_cast<long>(std::floor(v0));
    const auto end_x = static_cast<long>(std::floor(u1));
    const auto end_y = static_cast<long>(std::floor(v1));
    const auto passable = [&](const long cx, const long cy)
    {
        return cx >= 0 && cy >= 0 && cx < static_cast<long>(ground.width) && cy < static_cast<long>(ground.height) &&
               enterable(ground, static_cast<std::uint32_t>(cy) * ground.width + static_cast<std::uint32_t>(cx), footing);
    };
    const auto du = u1 - u0;
    const auto dv = v1 - v0;
    const long step_x = du > 0.0F ? 1 : du < 0.0F ? -1 : 0;
    const long step_y = dv > 0.0F ? 1 : dv < 0.0F ? -1 : 0;
    constexpr auto never = std::numeric_limits<float>::infinity();
    auto next_x = step_x > 0 ? (static_cast<float>(x + 1) - u0) / du : step_x < 0 ? (static_cast<float>(x) - u0) / du : never;
    auto next_y = step_y > 0 ? (static_cast<float>(y + 1) - v0) / dv : step_y < 0 ? (static_cast<float>(y) - v0) / dv : never;
    const auto delta_x = step_x != 0 ? 1.0F / std::fabs(du) : never;
    const auto delta_y = step_y != 0 ? 1.0F / std::fabs(dv) : never;
    // Each step moves one cell toward the end, so this bounds the walk even under rounding.
    const auto steps = std::abs(end_x - x) + std::abs(end_y - y);
    for (long step = 0; step < steps && (x != end_x || y != end_y); ++step)
    {
        if (next_x < next_y)
        {
            x += step_x;
            next_x += delta_x;
        }
        else if (next_y < next_x)
        {
            y += step_y;
            next_y += delta_y;
        }
        else
        {
            // Through a corner: the segment touches both side cells.
            if (!passable(x + step_x, y) || !passable(x, y + step_y))
            {
                return false;
            }
            x += step_x;
            y += step_y;
            next_x += delta_x;
            next_y += delta_y;
            ++step;
        }
        if (!passable(x, y))
        {
            return false;
        }
    }
    const bool start_cell = end_x == static_cast<long>(std::floor(u0)) && end_y == static_cast<long>(std::floor(v0));
    return start_cell || passable(end_x, end_y);
}

GridPathfinder::GridPathfinder(const std::uint32_t max_expansions, const ChunkLayout layout) noexcept : max_expansions_(max_expansions), layout_(layout)
{}

void GridPathfinder::reset() noexcept
{
    regions_.clear();
    chunk_graphs_.clear();
}

void GridPathfinder::prepare(const GroundMap& ground, const Footing& footing)
{
    if (layout_ == ChunkLayout::none || !ground.has_passability() || footing.passable_types == 0)
    {
        return;
    }
    const auto bucket = footing.clearance != nullptr && footing.radius > 0.0F ? footing_bucket(footing.radius) : 0.0F;
    const Footing bucket_footing(footing.passable_types, footing.clearance, bucket);
    (void)chunks(ground, bucket_footing, fine_chunk_cells);
    if (layout_ == ChunkLayout::two_level || layout_ == ChunkLayout::refined)
    {
        (void)chunks(ground, bucket_footing, coarse_chunk_cells);
    }
}

GridPathfinder::CellSearch GridPathfinder::search_cells(const GroundMap& ground, const Footing& footing, const std::uint32_t start_cell, const std::uint32_t goal_cell, const ChunkGraph* graph,
                                                        const std::vector<std::uint8_t>* parts, const std::size_t budget, std::uint32_t& expanded)
{
    const auto cells = ground.passability.size();
    if (seen_.size() != cells)
    {
        seen_.assign(cells, 0);
        closed_.assign(cells, 0);
        cost_.assign(cells, 0);
        parent_.assign(cells, 0);
        search_ = 0;
    }
    if (++search_ == 0)
    {
        // The stamp wrapped: clear the stamps once so no stale cell looks current.
        std::ranges::fill(seen_, 0U);
        std::ranges::fill(closed_, 0U);
        search_ = 1;
    }
    // A min-heap ordered by estimate, then heuristic, then cell index: no two nodes compare equal,
    // so the expansion order is fully determined.
    const auto later = [](const OpenNode& left, const OpenNode& right)
    {
        if (left.estimate != right.estimate)
        {
            return left.estimate > right.estimate;
        }
        if (left.heuristic != right.heuristic)
        {
            return left.heuristic > right.heuristic;
        }
        return left.cell > right.cell;
    };
    const auto held = [&](const std::uint32_t cell)
    {
        if (graph == nullptr)
        {
            return true;
        }
        const auto part = graph->part_of[cell];
        return part != 0 && (*parts)[part - 1U] != 0;
    };
    open_.clear();
    seen_[start_cell] = search_;
    cost_[start_cell] = 0;
    parent_[start_cell] = start_cell;
    const auto start_heuristic = heuristic(ground, start_cell, goal_cell);
    open_.push_back({start_heuristic, start_heuristic, start_cell});
    auto closest = start_cell;
    auto closest_heuristic = start_heuristic;
    std::size_t own_expanded = 0;
    static constexpr std::array<std::array<int, 2>, 8> directions{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1}}};
    while (!open_.empty())
    {
        std::ranges::pop_heap(open_, later);
        const auto node = open_.back();
        open_.pop_back();
        if (closed_[node.cell] == search_)
        {
            continue;
        }
        closed_[node.cell] = search_;
        ++expanded;
        ++own_expanded;
        if (node.heuristic < closest_heuristic || (node.heuristic == closest_heuristic && cost_[node.cell] < cost_[closest]))
        {
            closest = node.cell;
            closest_heuristic = node.heuristic;
        }
        if (node.cell == goal_cell)
        {
            return {true, goal_cell};
        }
        if (own_expanded >= budget)
        {
            break;
        }
        const auto x = static_cast<long>(node.cell % ground.width);
        const auto y = static_cast<long>(node.cell / ground.width);
        const auto open_cell = [&](const long cx, const long cy)
        {
            if (cx < 0 || cy < 0 || cx >= static_cast<long>(ground.width) || cy >= static_cast<long>(ground.height))
            {
                return false;
            }
            const auto cell = static_cast<std::uint32_t>(cy) * ground.width + static_cast<std::uint32_t>(cx);
            return enterable(ground, cell, footing) && held(cell);
        };
        for (const auto& [dx, dy] : directions)
        {
            const auto nx = x + dx;
            const auto ny = y + dy;
            if (!open_cell(nx, ny))
            {
                continue;
            }
            const bool diagonal = dx != 0 && dy != 0;
            if (diagonal && (!open_cell(x + dx, y) || !open_cell(x, y + dy)))
            {
                continue;
            }
            const auto neighbor = static_cast<std::uint32_t>(ny) * ground.width + static_cast<std::uint32_t>(nx);
            if (closed_[neighbor] == search_)
            {
                continue;
            }
            const auto cost = cost_[node.cell] + (diagonal ? diagonal_cost : straight_cost);
            if (seen_[neighbor] != search_ || cost < cost_[neighbor])
            {
                seen_[neighbor] = search_;
                cost_[neighbor] = cost;
                parent_[neighbor] = node.cell;
                const auto estimate_heuristic = heuristic(ground, neighbor, goal_cell);
                open_.push_back({cost + estimate_heuristic, estimate_heuristic, neighbor});
                std::ranges::push_heap(open_, later);
            }
        }
    }
    return {false, closest};
}

std::vector<std::uint32_t> GridPathfinder::way_back(const std::uint32_t start_cell, const std::uint32_t end) const
{
    std::vector<std::uint32_t> cells;
    for (auto cell = end; cell != start_cell; cell = parent_[cell])
    {
        cells.push_back(cell);
    }
    cells.push_back(start_cell);
    std::ranges::reverse(cells);
    return cells;
}

const ChunkGraph& GridPathfinder::chunks(const GroundMap& ground, const Footing& footing, const std::uint32_t chunk_cells)
{
    const auto radius = static_cast<std::uint32_t>(std::max(0.0F, footing.radius));
    for (const auto& graph : chunk_graphs_)
    {
        if (graph.passable_types == footing.passable_types && graph.radius == radius && graph.chunk_cells == chunk_cells && graph.part_of.size() == ground.passability.size())
        {
            return graph;
        }
    }
    return chunk_graphs_.emplace_back(build_chunk_graph(ground, footing, chunk_cells));
}

const std::vector<std::uint32_t>& GridPathfinder::regions(const GroundMap& ground, const Footing& given)
{
    const auto cells = ground.passability.size();
    const auto radius = region_radius(given);
    const auto passable_types = given.passable_types;
    // Labeled at the whole-unit radius (the unit's radius rounded up), so one set serves every unit of
    // that size.
    const Footing footing(passable_types, given.clearance, static_cast<float>(radius));
    for (const auto& cached : regions_)
    {
        if (cached.passable_types == passable_types && cached.radius == radius && cached.labels.size() == cells)
        {
            return cached.labels;
        }
    }
    // 4-connected flood fill in cell order: a diagonal step needs both side cells, so diagonal
    // neighbors that are connected at all are connected through a side cell. Labels start at 1.
    auto& entry = regions_.emplace_back();
    entry.passable_types = passable_types;
    entry.radius = radius;
    entry.labels.assign(cells, 0);
    std::vector<std::uint32_t> pending;
    std::uint32_t next_label = 0;
    for (std::uint32_t seed = 0; seed < cells; ++seed)
    {
        if (entry.labels[seed] != 0 || !enterable(ground, seed, footing))
        {
            continue;
        }
        ++next_label;
        entry.labels[seed] = next_label;
        pending.push_back(seed);
        while (!pending.empty())
        {
            const auto cell = pending.back();
            pending.pop_back();
            const auto x = cell % ground.width;
            const auto y = cell / ground.width;
            const auto visit = [&](const std::uint32_t neighbor)
            {
                if (entry.labels[neighbor] == 0 && enterable(ground, neighbor, footing))
                {
                    entry.labels[neighbor] = next_label;
                    pending.push_back(neighbor);
                }
            };
            if (x > 0)
            {
                visit(cell - 1);
            }
            if (x + 1 < ground.width)
            {
                visit(cell + 1);
            }
            if (y > 0)
            {
                visit(cell - ground.width);
            }
            if (y + 1 < ground.height)
            {
                visit(cell + ground.width);
            }
        }
    }
    return entry.labels;
}

PathResult GridPathfinder::find(const GroundMap& ground, PathPoint start, PathPoint goal, const Footing& given)
{
    // The search plans at the whole-unit radius its regions are labeled at, so the two agree.
    const Footing footing(given.passable_types, given.clearance, static_cast<float>(region_radius(given)));
    PathResult result;
    if (!ground.has_passability() || !finite(start) || !finite(goal))
    {
        return result;
    }
    result.status = PathStatus::partial;
    start = clamp_to_ground(ground, start);
    goal = clamp_to_ground(ground, goal);
    const auto cells = ground.passability.size();
    // A unit standing where its class may not be (spawned there, or on ground that changed) first
    // leaves in a straight line for the nearest enterable cell, and the search starts there.
    const auto origin = start;
    auto start_cell = cell_of(ground, start);
    const bool escaping = !enterable(ground, start_cell, footing);
    if (escaping)
    {
        const auto nearest = nearest_cell(ground, start, goal,
                                          [&](const std::uint32_t cell)
                                          {
                                              return enterable(ground, cell, footing);
                                          });
        if (!nearest)
        {
            return result;
        }
        start_cell = *nearest;
        start = cell_center(ground, start_cell);
    }
    // A goal outside the start's region (on a cell the class may not enter, or in a region it cannot
    // reach) moves to the region's cell nearest to it, so the search below always succeeds instead
    // of exhausting the region.
    const auto& labels = regions(ground, footing);
    auto goal_cell = cell_of(ground, goal);
    bool goal_moved = false;
    if (labels[goal_cell] != labels[start_cell])
    {
        const auto region = labels[start_cell];
        const auto nearest = nearest_cell(ground, goal, start,
                                          [&](const std::uint32_t cell)
                                          {
                                              return labels[cell] == region;
                                          });
        if (!nearest)
        {
            return result;
        }
        goal_cell = *nearest;
        goal = cell_center(ground, goal_cell);
        goal_moved = true;
    }

    const auto budget = max_expansions_ == 0 ? cells : std::min<std::size_t>(max_expansions_, cells);
    // The cells of the way, start to end.
    std::vector<std::uint32_t> way;
    bool found = false;
    bool chunked = false;
    if (layout_ != ChunkLayout::none && start_cell != goal_cell)
    {
        // At the footing's size bucket: every corridor cell has room for the bucket, so for the unit,
        // and a search held to the corridor always finds the way.
        const auto bucket = footing.clearance != nullptr && footing.radius > 0.0F ? footing_bucket(footing.radius) : 0.0F;
        const Footing bucket_footing(footing.passable_types, footing.clearance, bucket);
        const auto& fine = chunks(ground, bucket_footing, fine_chunk_cells);
        const auto start_part = fine.part_of[start_cell];
        const auto goal_part = fine.part_of[goal_cell];
        if (start_part != 0 && goal_part != 0 && start_part != goal_part)
        {
            std::vector<std::uint8_t> allowed;
            if (layout_ == ChunkLayout::two_level || layout_ == ChunkLayout::refined)
            {
                // The 32-cell route first; a 16-cell part lies inside one 32-cell part.
                const auto& coarse = chunks(ground, bucket_footing, coarse_chunk_cells);
                const auto coarse_route = chunk_route(ground, coarse, coarse.part_of[start_cell], coarse.part_of[goal_cell], {}, result.expanded);
                if (!coarse_route.empty())
                {
                    std::vector<std::uint8_t> in_route(coarse.parts(), 0);
                    for (const auto part : coarse_route)
                    {
                        in_route[part - 1U] = 1;
                    }
                    allowed.assign(fine.parts(), 0);
                    for (std::size_t part = 0; part < fine.parts(); ++part)
                    {
                        allowed[part] = in_route[coarse.part_of[fine.center_of_part[part]] - 1U];
                    }
                }
            }
            auto route = chunk_route(ground, fine, start_part, goal_part, allowed, result.expanded);
            if (route.empty() && !allowed.empty())
            {
                route = chunk_route(ground, fine, start_part, goal_part, {}, result.expanded);
            }
            if (!route.empty() && layout_ == ChunkLayout::refined)
            {
                // Part by part: from where the route enters the part to its best crossing into the next
                // (least cost so far plus estimate on), held to the part.
                std::vector<std::uint8_t> only(fine.parts(), 0);
                auto current = start_cell;
                way.push_back(start_cell);
                chunked = true;
                for (std::size_t step = 0; step < route.size(); ++step)
                {
                    const auto part = route[step];
                    auto target = goal_cell;
                    std::optional<std::uint32_t> across;
                    if (step + 1 < route.size())
                    {
                        const auto next = route[step + 1];
                        const auto center = fine.center_of_part[part - 1U];
                        const auto first_x = center % ground.width / fine_chunk_cells * fine_chunk_cells;
                        const auto first_y = center / ground.width / fine_chunk_cells * fine_chunk_cells;
                        auto best = std::numeric_limits<std::uint32_t>::max();
                        for (auto y = first_y; y < std::min(ground.height, first_y + fine_chunk_cells); ++y)
                        {
                            for (auto x = first_x; x < std::min(ground.width, first_x + fine_chunk_cells); ++x)
                            {
                                const auto cell = y * ground.width + x;
                                if (fine.part_of[cell] != part)
                                {
                                    continue;
                                }
                                for (const auto other :
                                     {x > 0 ? cell - 1U : cell, x + 1U < ground.width ? cell + 1U : cell, y > 0 ? cell - ground.width : cell, y + 1U < ground.height ? cell + ground.width : cell})
                                {
                                    if (other == cell || fine.part_of[other] != next)
                                    {
                                        continue;
                                    }
                                    const auto score = heuristic(ground, current, cell) + heuristic(ground, other, goal_cell);
                                    if (score < best)
                                    {
                                        best = score;
                                        target = cell;
                                        across = other;
                                    }
                                }
                            }
                        }
                        if (!across)
                        {
                            chunked = false;
                            break;
                        }
                    }
                    only[part - 1U] = 1;
                    const auto piece = search_cells(ground, footing, current, target, &fine, &only, budget, result.expanded);
                    only[part - 1U] = 0;
                    if (!piece.found)
                    {
                        chunked = false;
                        break;
                    }
                    const auto cells_of_piece = way_back(current, target);
                    way.insert(way.end(), cells_of_piece.begin() + 1, cells_of_piece.end());
                    if (step + 1 == refined_first_chunks)
                    {
                        result.first_stretch_expanded = result.expanded;
                    }
                    if (across)
                    {
                        way.push_back(*across);
                        current = *across;
                    }
                }
                if (!chunked)
                {
                    way.clear();
                }
                found = chunked;
            }
            else if (!route.empty())
            {
                std::vector<std::uint8_t> corridor(fine.parts(), 0);
                for (const auto part : route)
                {
                    corridor[part - 1U] = 1;
                }
                const auto searched = search_cells(ground, footing, start_cell, goal_cell, &fine, &corridor, budget, result.expanded);
                found = searched.found;
                way = way_back(start_cell, searched.end);
                chunked = true;
            }
        }
    }
    if (!chunked)
    {
        const auto searched = search_cells(ground, footing, start_cell, goal_cell, nullptr, nullptr, budget, result.expanded);
        found = searched.found;
        way = way_back(start_cell, searched.end);
    }
    if (result.first_stretch_expanded == 0)
    {
        result.first_stretch_expanded = result.expanded;
    }

    // Points from the start to the end (the start's own point first), then the exact goal when it
    // was reached.
    std::vector<PathPoint> points;
    if (escaping)
    {
        points.push_back(origin);
    }
    points.push_back(start);
    for (std::size_t index = 1; index < way.size(); ++index)
    {
        points.push_back(cell_center(ground, way[index]));
    }
    if (found)
    {
        if (points.size() > 1 + (escaping ? 1U : 0U))
        {
            points.back() = goal;
        }
        else
        {
            points.push_back(goal);
        }
    }
    // Keep a waypoint only when the next one cannot be reached in a straight line from the last
    // kept point. Consecutive cell centers are always connected, so this never fails to advance.
    std::size_t anchor = 0;
    while (anchor + 1 < points.size())
    {
        auto next = anchor + 1;
        while (next + 1 < points.size() && ground_line_passable(ground, points[anchor], points[next + 1], footing))
        {
            ++next;
        }
        result.waypoints.push_back(points[next]);
        anchor = next;
    }
    result.status = found && !goal_moved ? PathStatus::reached : PathStatus::partial;
    return result;
}

} // namespace eaw
