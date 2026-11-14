// Route searches, the first simulation work planned for the job system (docs/ecs-design.md, 7.5):
// a batch of independent A* searches on one immutable ground, serially with one pathfinder (as the
// simulation's path queue does today) and in parallel with one pathfinder per chunk.

#include "core/random.hpp"
#include "jobs/job_system.hpp"
#include "jobs/parallel.hpp"
#include "simulation/ground_map.hpp"
#include "simulation/pathfinding.hpp"

#include "benchmarks/benchmarks.hpp"

#include <array>
#include <memory>
#include <vector>

namespace eaw::benchmarks {

namespace {

// A 512 by 512 cell ground (20 units a cell) with walls and blobs of obstacle, and 384 searches
// between random points, eight to a chunk. The layout is fixed by the seed.
constexpr std::uint32_t ground_cells = 512;
constexpr float cell_spacing = 20.0F;
constexpr std::uint32_t search_count = 384;
constexpr std::size_t search_grain = 8;
constexpr std::uint64_t layout_seed = 0x5eed2026;

GroundMap make_ground()
{
    GroundMap ground;
    ground.width = ground_cells;
    ground.height = ground_cells;
    ground.spacing = cell_spacing;
    ground.heights.assign(static_cast<std::size_t>(ground_cells) * ground_cells, 0.0F);
    ground.passability.assign(ground.heights.size(), passability::clear);
    Pcg32 random(layout_seed);
    const auto block = [&](const std::uint32_t x, const std::uint32_t y)
    {
        if (x < ground_cells && y < ground_cells)
        {
            ground.passability[static_cast<std::size_t>(y) * ground_cells + x] = passability::obstacle;
        }
    };
    // Walls with gaps, so routes must find the openings.
    for (std::uint32_t wall = 0; wall < 24; ++wall)
    {
        const auto x = random.next() % ground_cells;
        const auto y = random.next() % ground_cells;
        const auto length = 40 + random.next() % 160;
        const bool across = (random.next() & 1U) != 0;
        for (std::uint32_t step = 0; step < length; ++step)
        {
            if (step % 37 < 33)
            {
                block(across ? x + step : x, across ? y : y + step);
            }
        }
    }
    // Blobs of obstacle.
    for (std::uint32_t blob = 0; blob < 220; ++blob)
    {
        const auto x = random.next() % ground_cells;
        const auto y = random.next() % ground_cells;
        const auto size = 2 + random.next() % 9;
        for (std::uint32_t dy = 0; dy < size; ++dy)
        {
            for (std::uint32_t dx = 0; dx < size; ++dx)
            {
                block(x + dx, y + dy);
            }
        }
    }
    return ground;
}

struct Searches
{
    GroundMap ground;
    std::vector<std::array<PathPoint, 2>> requests;
    // One pathfinder per chunk (they keep scratch storage and are not thread-safe), warmed before
    // the timed runs; the serial run uses the first.
    std::vector<GridPathfinder> pathfinders;
    std::vector<PathResult> results;
};

Workload prepare_paths()
{
    auto searches = std::make_shared<Searches>();
    searches->ground = make_ground();
    Pcg32 random(layout_seed + 1);
    const auto extent = static_cast<float>(ground_cells - 1) * cell_spacing;
    for (std::uint32_t index = 0; index < search_count; ++index)
    {
        const auto point = [&]
        {
            return PathPoint{static_cast<float>(random.next() % 10000) / 10000.0F * extent, static_cast<float>(random.next() % 10000) / 10000.0F * extent};
        };
        searches->requests.push_back({point(), point()});
    }
    searches->pathfinders.resize(chunk_count(search_count, search_grain));
    searches->results.resize(search_count);
    for (auto& pathfinder : searches->pathfinders)
    {
        (void)pathfinder.find(searches->ground, searches->requests[0][0], searches->requests[0][1], passability::clear);
    }
    return {[searches](JobSystem* jobs)
            {
                if (jobs == nullptr)
                {
                    for (std::uint32_t index = 0; index < search_count; ++index)
                    {
                        const auto& request = searches->requests[index];
                        searches->results[index] = searches->pathfinders[0].find(searches->ground, request[0], request[1], passability::clear);
                    }
                }
                else
                {
                    parallel_for(*jobs, search_count, search_grain,
                                 [&](const std::size_t begin, const std::size_t end, const std::size_t chunk)
                                 {
                                     for (std::size_t index = begin; index < end; ++index)
                                     {
                                         const auto& request = searches->requests[index];
                                         searches->results[index] = searches->pathfinders[chunk].find(searches->ground, request[0], request[1], passability::clear);
                                     }
                                 });
                }
                std::uint64_t print = 0;
                for (const auto& result : searches->results)
                {
                    print = fingerprint(print, &result.status, sizeof(result.status));
                    print = fingerprint(print, result.waypoints.data(), result.waypoints.size() * sizeof(PathPoint));
                }
                return print;
            }};
}

constexpr std::array<Suite, 1> suites{{
    {"paths", "384 A* searches on a 512x512 ground with walls and obstacles, 8 to a chunk", &prepare_paths},
}};

} // namespace

std::span<const Suite> path_suites()
{
    return suites;
}

} // namespace eaw::benchmarks
