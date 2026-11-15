#include "sandbox/path_audit.hpp"

#include "core/checksum.hpp"
#include "core/file_io.hpp"
#include "core/random.hpp"
#include "core/string_utils.hpp"
#include "vfs/virtual_file_system.hpp"
#include "simulation/pathfinding.hpp"
#include "battle/battle.hpp"
#include "battle/battle_map.hpp"
#include "battle/object_types.hpp"
#include "sandbox/text_format.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace eaw {
namespace {

// Engine choices: pairs per map and class unless --pairs says otherwise, and the fixed seed (each
// map and class draws from it mixed with their names, so adding a map changes no other's pairs).
constexpr std::uint32_t default_pairs = 100;
constexpr std::uint64_t audit_seed = 0xA0D17A0D17ULL;
constexpr double percentile = 0.95;

struct PairResult
{
    PathPoint start;
    PathPoint goal;
    PathStatus status{};
    std::uint32_t expanded{};
    float length{};
    double microseconds{};
};

template <class T> T percentile_of(std::vector<T> values)
{
    if (values.empty())
    {
        return T{};
    }
    std::ranges::sort(values);
    return values[std::min(values.size() - 1U, static_cast<std::size_t>(percentile * static_cast<double>(values.size())))];
}

// The land maps to audit: --map, else every map under Data/Art/Maps whose name starts with _LAND.
std::vector<std::string> audit_maps(const SandboxOptions& options, const VirtualFileSystem& vfs)
{
    if (options.map)
    {
        return {*options.map};
    }
    std::vector<std::string> maps;
    for (const auto& asset : vfs.list("Data/Art/Maps/"))
    {
        const auto lower = ascii_lower(asset.virtual_path);
        const auto name = lower.substr(lower.find_last_of('/') + 1U);
        if (name.starts_with("_land") && name.ends_with(".ted"))
        {
            maps.push_back(asset.virtual_path);
        }
    }
    std::ranges::sort(maps);
    return maps;
}

// Each distinct mask once, named by its classes (folded names, sorted, joined with '+').
std::map<std::uint32_t, std::string> class_masks(const VirtualFileSystem& vfs)
{
    std::map<std::uint32_t, std::vector<std::string>> names;
    for (const auto& [name, mask] : load_movement_classes(vfs).classes)
    {
        if (mask != 0U)
        {
            names[mask].push_back(name);
        }
    }
    std::map<std::uint32_t, std::string> masks;
    for (auto& [mask, list] : names)
    {
        std::ranges::sort(list);
        std::string joined;
        for (const auto& name : list)
        {
            joined += (joined.empty() ? "" : "+") + name;
        }
        masks[mask] = joined;
    }
    return masks;
}

float path_length(const PathPoint start, const std::vector<PathPoint>& waypoints)
{
    float length = 0.0F;
    auto from = start;
    for (const auto& point : waypoints)
    {
        length += std::hypot(point.x - from.x, point.y - from.y);
        from = point;
    }
    return length;
}

} // namespace

int run_path_audit(const SandboxOptions& options, const VirtualFileSystem& vfs)
{
    const auto pairs = options.pairs.value_or(default_pairs);
    const auto masks = class_masks(vfs);
    std::ostringstream rows;
    rows << "map\tclass\tmask\tpair\tstart_x\tstart_y\tgoal_x\tgoal_y\tstatus\texpanded\tlength\tmicroseconds\n";
    std::size_t built = 0;
    std::vector<std::uint32_t> all_expanded;
    std::vector<double> all_microseconds;
    std::size_t all_reached = 0;
    std::cout << "map\tclass\tpairs\treached\texpanded mean/p95/max\tms mean/p95/max\tlength mean\n";
    for (const auto& map_name : audit_maps(options, vfs))
    {
        auto asset = build_battle_map(vfs, map_name);
        if (!asset || !asset->ground.valid() || asset->ground.passability.empty())
        {
            std::cerr << "skipped " << map_name << ": " << (asset ? "no ground passability" : asset.error().message) << '\n';
            continue;
        }
        ++built;
        // The battle's ground: the map's passability with the placed objects' footprints stamped.
        const Battle battle(battle_map(*asset), map_name);
        const auto& ground = battle.world().ground();
        GridPathfinder pathfinder;
        for (const auto& [mask, class_name] : masks)
        {
            std::vector<std::uint32_t> cells;
            for (std::uint32_t cell = 0; cell < ground.passability.size(); ++cell)
            {
                if ((ground.passability[cell] & mask) != 0U)
                {
                    cells.push_back(cell);
                }
            }
            if (cells.empty())
            {
                continue;
            }
            Pcg32 random(audit_seed ^ crc32(ascii_lower(map_name)) ^ (static_cast<std::uint64_t>(mask) << 32U));
            const auto point_of = [&](const std::uint32_t cell)
            {
                return PathPoint{static_cast<float>(cell % ground.width) * ground.spacing, static_cast<float>(cell / ground.width) * ground.spacing};
            };
            std::vector<PairResult> results;
            for (std::uint32_t pair = 0; pair < pairs; ++pair)
            {
                PairResult result;
                result.start = point_of(cells[random.below(static_cast<std::uint32_t>(cells.size()))]);
                result.goal = point_of(cells[random.below(static_cast<std::uint32_t>(cells.size()))]);
                const auto started = std::chrono::steady_clock::now();
                const auto found = pathfinder.find(ground, result.start, result.goal, mask);
                result.microseconds = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
                result.status = found.status;
                result.expanded = found.expanded;
                result.length = path_length(result.start, found.waypoints);
                rows << map_name << '\t' << class_name << '\t' << mask << '\t' << pair << '\t' << result.start.x << '\t' << result.start.y << '\t' << result.goal.x << '\t' << result.goal.y << '\t'
                     << (result.status == PathStatus::reached   ? "reached"
                         : result.status == PathStatus::partial ? "partial"
                                                                : "unavailable")
                     << '\t' << result.expanded << '\t' << format_fixed(result.length, 1) << '\t' << format_fixed(result.microseconds, 1) << '\n';
                results.push_back(result);
            }
            std::vector<std::uint32_t> expanded;
            std::vector<double> microseconds;
            std::size_t reached = 0;
            double length = 0.0;
            for (const auto& result : results)
            {
                expanded.push_back(result.expanded);
                microseconds.push_back(result.microseconds);
                reached += result.status == PathStatus::reached ? 1U : 0U;
                length += result.length;
            }
            all_expanded.insert(all_expanded.end(), expanded.begin(), expanded.end());
            all_microseconds.insert(all_microseconds.end(), microseconds.begin(), microseconds.end());
            all_reached += reached;
            double expanded_sum = 0.0;
            double time_sum = 0.0;
            for (std::size_t index = 0; index < results.size(); ++index)
            {
                expanded_sum += expanded[index];
                time_sum += microseconds[index];
            }
            const auto count = static_cast<double>(results.size());
            std::cout << map_name << '\t' << class_name << '\t' << results.size() << '\t' << reached << '\t' << format_fixed(expanded_sum / count, 0) << '/' << percentile_of(expanded) << '/'
                      << *std::ranges::max_element(expanded) << '\t' << format_fixed(time_sum / count / 1000.0, 3) << '/' << format_fixed(percentile_of(microseconds) / 1000.0, 3) << '/'
                      << format_fixed(*std::ranges::max_element(microseconds) / 1000.0, 3) << '\t' << format_fixed(length / count, 0) << '\n';
        }
    }
    if (built == 0)
    {
        std::cerr << "error: no land map could be built\n";
        return 1;
    }
    double expanded_sum = 0.0;
    double time_sum = 0.0;
    for (std::size_t index = 0; index < all_expanded.size(); ++index)
    {
        expanded_sum += all_expanded[index];
        time_sum += all_microseconds[index];
    }
    const auto count = static_cast<double>(std::max<std::size_t>(1U, all_expanded.size()));
    std::cout << "All: " << built << " maps, " << all_expanded.size() << " pairs, " << all_reached << " reached | expanded mean " << format_fixed(expanded_sum / count, 0) << ", p95 "
              << percentile_of(all_expanded) << " | ms mean " << format_fixed(time_sum / count / 1000.0, 3) << ", p95 " << format_fixed(percentile_of(all_microseconds) / 1000.0, 3) << '\n';
    if (options.report)
    {
        const auto file = *options.report / "path-audit.tsv";
        if (const auto written = write_text_file(file, rows.str(), WriteMode::replace); !written)
        {
            std::cerr << "error: " << written.error().message << '\n';
            return 1;
        }
        std::cout << "Wrote " << file.string() << '\n';
    }
    return 0;
}

} // namespace eaw
