#include "sandbox/battle_runner.hpp"

#include "battle_stress.hpp"

#include "core/log.hpp"
#include "battle/battle.hpp"
#include "battle/battle_log.hpp"
#include "battle/battle_runtime.hpp"
#include "battle/battle_scripts.hpp"
#include "sandbox/text_format.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <iostream>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

namespace eaw {
namespace {

// 100 seconds at the default 60 Hz.
constexpr std::uint64_t default_ticks = 6000;
// A frame at 60 frames per second, which a frame with its ticks must fit in (the owner, 2026-11-15).
// Headless, a frame is the tick it runs (60 Hz), without rendering.
constexpr double frame_budget_ms = 1000.0 / 60.0;
// Frames are measured from this tick on: the ticks before apply the spawns (loading, not play).
constexpr std::uint64_t first_measured_tick = 1;
constexpr double frame_percentile = 0.99;

std::string hex(const std::uint64_t value)
{
    std::array<char, 17> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, 16);
    (void)error;
    return "0x" + std::string(buffer.data(), end);
}

struct BattleRun
{
    std::uint64_t hash{};
    SimulationWorld::Stats stats;
    BattleLog log;
    double seconds{};
    double slowest_ms{};
    std::uint64_t slowest_tick{};
    // Milliseconds of each step from first_measured_tick on.
    std::vector<double> frame_ms;
    std::size_t starting_entities{};
    // The script phases (none with --no-scripts).
    std::optional<BattleScriptStats> scripts;
    // With --hash-trace: the subject hashes after each tick.
    std::vector<StateHashes> trace;
    bool failed{};
};

// The point groups spawn around and auto-move goes to: the middle of the ground on land maps, at
// its surface, and the middle of the placed objects in space. Both come from map data alone, so
// a headless build (no textures) and a full one place battles alike.
Vector3 battle_center(const BattleMapAsset& asset)
{
    if (asset.ground.valid())
    {
        const auto& ground = asset.ground;
        Vector3 center{static_cast<float>(ground.width - 1U) * ground.spacing * 0.5F, static_cast<float>(ground.height - 1U) * ground.spacing * 0.5F, 0.0F};
        // Open ground, so every side can reach it (Utapau's middle is ground no unit may enter).
        center = open_ground(ground, center, passability::clear);
        center.z = ground.surface_height(center.x, center.y);
        return center;
    }
    // Without ground, the middle of where the placed models reach.
    const auto& extent = asset.extent;
    return {(extent.minimum.x + extent.maximum.x) * 0.5F, (extent.minimum.y + extent.maximum.y) * 0.5F, 0.0F};
}

// `scripts_reader` resolves game-object scripts; null runs none (--no-scripts).
BattleRun run_once(const BattleMapAsset& asset, const std::string& map_name, const SandboxOptions& options, const BattleLog* replay, const std::uint64_t ticks, const AssetReader* scripts_reader)
{
    BattleRun run;
    BattleRuntime runtime(battle_map(asset), map_name, scripts_reader, options.tick_rate ? static_cast<double>(*options.tick_rate) : default_ticks_per_second);
    auto& battle = runtime.battle();
    auto& world = battle.world();
    run.starting_entities = world.stats().entities;
    const auto center = battle_center(asset);
    std::size_t next_entry = 0;
    bool auto_move_sent = false;
    std::optional<StressOrders> stress;
    if (options.stress_orders)
    {
        std::vector<EntityId> placed;
        for (const auto& id : battle.map_entity_ids())
        {
            if (id)
            {
                placed.push_back(*id);
            }
        }
        stress.emplace(center, asset.ground.valid() ? &asset.ground : nullptr, std::move(placed), runtime.tick_seconds());
    }
    const auto start = std::chrono::steady_clock::now();
    if (!replay)
    {
        // Requests spawn in command-line order, each group 180 units further along +X, as in the
        // sandbox, for owner 1 unless named.
        for (std::size_t request = 0; request < options.spawns.size(); ++request)
        {
            const auto& entry = options.spawns[request];
            if (entry.count == 0)
            {
                continue;
            }
            const Vector3 point{center.x + 180.0F * static_cast<float>(request), center.y, center.z};
            if (const auto submitted = battle.submit({.order = BattleOrder::spawn, .point = point, .type = entry.type, .count = entry.count, .owner = entry.owner.value_or(1U)}); !submitted)
            {
                log_warning("battle", submitted.error());
            }
        }
    }
    while (world.stats().tick < ticks)
    {
        const auto tick = world.stats().tick;
        if (replay)
        {
            for (; next_entry < replay->entries.size() && replay->entries[next_entry].tick <= tick; ++next_entry)
            {
                if (const auto submitted = battle.submit(replay->entries[next_entry].command); !submitted)
                {
                    std::cerr << "error: replayed order at tick " << replay->entries[next_entry].tick << " was rejected: " << submitted.error().message << '\n';
                    run.failed = true;
                    return run;
                }
            }
        }
        else if (stress)
        {
            stress->issue(battle, tick);
        }
        else if (options.auto_move && !auto_move_sent && tick >= 2)
        {
            // Once the spawns have applied: every mobile unit, in id order, moves to the center.
            auto_move_sent = true;
            std::vector<EntityId> movers;
            for (const auto& entity : world.snapshot().entities)
            {
                if (entity.max_speed > 0.0F)
                {
                    movers.push_back(entity.id);
                }
            }
            std::ranges::sort(movers);
            if (!movers.empty())
            {
                if (const auto submitted = battle.submit({.order = BattleOrder::move, .entities = std::move(movers), .point = center}); !submitted)
                {
                    log_warning("battle", submitted.error());
                }
            }
        }
        runtime.step();
        if (runtime.last_step_milliseconds() > run.slowest_ms)
        {
            run.slowest_ms = runtime.last_step_milliseconds();
            run.slowest_tick = tick + 1U;
        }
        if (tick >= first_measured_tick)
        {
            run.frame_ms.push_back(runtime.last_step_milliseconds());
        }
        if (options.hash_trace)
        {
            run.trace.push_back(world.state_hashes());
        }
    }
    run.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    run.hash = world.state_hash();
    run.stats = world.stats();
    run.log = battle.log();
    if (const auto* scripts = runtime.scripts())
    {
        run.scripts = scripts->stats();
    }
    run.log.end_tick = run.stats.tick;
    run.log.end_hash = run.hash;
    return run;
}

// The first tick after which the two runs' subject hashes differ, and in which subjects.
void print_first_difference(const std::vector<StateHashes>& first, const std::vector<StateHashes>& second)
{
    const auto ticks = std::min(first.size(), second.size());
    for (std::size_t tick = 0; tick < ticks; ++tick)
    {
        if (first[tick] == second[tick])
        {
            continue;
        }
        std::cout << "Hash trace: the runs first differ after tick " << tick + 1U << " in";
        for (std::size_t subject = 0; subject < state_subject_count; ++subject)
        {
            if (first[tick][subject] != second[tick][subject])
            {
                std::cout << ' ' << state_subject_names[subject];
            }
        }
        std::cout << '\n';
        return;
    }
    std::cout << "Hash trace: every subject agrees after each of the " << ticks << " ticks (the runs differ in length or in the tick count only)\n";
}

void print_run(const BattleRun& run, const std::uint64_t ticks_run)
{
    const auto& stats = run.stats;
    std::cout << "Ran " << ticks_run << " ticks in " << format_fixed(run.seconds, 2) << " s: average " << format_fixed(ticks_run > 0 ? run.seconds * 1000.0 / static_cast<double>(ticks_run) : 0.0, 3)
              << " ms, slowest " << format_fixed(run.slowest_ms, 2) << " ms (tick " << run.slowest_tick << ")\n"
              << "State hash: " << hex(run.hash) << " at tick " << stats.tick << '\n'
              << "Entities: " << run.starting_entities << " at start, " << stats.entities << " at end | orders " << run.log.entries.size() << ", commands applied " << stats.commands_applied
              << ", stale references " << stats.stale_references << ", rejected spawns " << stats.rejected_spawns << '\n'
              << "Combat: " << stats.shots_fired << " shots, " << stats.hits << " hits, " << stats.kills << " kills, " << stats.hardpoints_destroyed << " hardpoints destroyed, " << stats.crushed
              << " crushed, " << stats.projectiles << " in flight\n"
              << "Paths: " << stats.path_searches << " planned, " << stats.partial_paths << " partial, " << stats.shared_routes << " shared, " << stats.path_expansions << " nodes expanded, "
              << stats.blocked_moves << " blocked moves\n";
    // Each tick layer: average milliseconds per tick, and the slowest tick's.
    const auto per_tick = [&](const double total)
    {
        return format_fixed(ticks_run > 0 ? total * 1000.0 / static_cast<double>(ticks_run) : 0.0, 3);
    };
    std::cout << "Layers, ms per tick (average / slowest):";
    for (std::size_t layer = 0; layer < tick_phase_count; ++layer)
    {
        std::cout << ' ' << tick_phase_names[layer] << ' ' << per_tick(stats.phase_seconds[layer]) << '/' << format_fixed(stats.slowest_phase_seconds[layer] * 1000.0, 2);
    }
    std::cout << '\n';
    if (!run.frame_ms.empty())
    {
        auto sorted = run.frame_ms;
        std::ranges::sort(sorted);
        const auto over = static_cast<std::size_t>(std::ranges::count_if(sorted,
                                                                         [](const double ms)
                                                                         {
                                                                             return ms > frame_budget_ms;
                                                                         }));
        const auto percentile = sorted[std::min(sorted.size() - 1U, static_cast<std::size_t>(frame_percentile * static_cast<double>(sorted.size())))];
        std::cout << "Frame budget " << format_fixed(frame_budget_ms, 1) << " ms: " << over << " of " << sorted.size() << " frames over | average "
                  << format_fixed(std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(sorted.size()), 3) << " ms, 99th percentile " << format_fixed(percentile, 2) << " ms, worst "
                  << format_fixed(sorted.back(), 2) << " ms\n";
    }
    if (run.scripts)
    {
        const auto& scripts = *run.scripts;
        std::cout << "Scripts: " << scripts.started << " started, " << scripts.failed << " failed | " << format_fixed(scripts.total_seconds * 1000.0, 2) << " ms in all, slowest phase "
                  << format_fixed(scripts.worst_phase_seconds * 1000.0, 3) << " ms\n";
    }
}

} // namespace

int run_battle(const SandboxOptions& options, const VirtualFileSystem& vfs)
{
    std::optional<BattleLog> replay;
    if (options.replay)
    {
        auto loaded = load_battle_log(*options.replay);
        if (!loaded)
        {
            std::cerr << "error: " << loaded.error().message << '\n';
            return 1;
        }
        replay = std::move(loaded).value();
    }
    const auto map_name = options.map ? *options.map : replay->map;
    if (map_name.empty())
    {
        std::cerr << "error: the replayed log names no map; pass --map\n";
        return 1;
    }
    // The spawnable types the orders name, in first-use order.
    std::vector<std::string> spawn_types;
    const auto add_type = [&](const std::string& type)
    {
        if (std::ranges::find(spawn_types, type) == spawn_types.end())
        {
            spawn_types.push_back(type);
        }
    };
    if (replay)
    {
        for (const auto& entry : replay->entries)
        {
            if (entry.command.order == BattleOrder::spawn)
            {
                add_type(entry.command.type);
            }
        }
    }
    for (const auto& request : options.spawns)
    {
        add_type(request.type);
    }
    const auto build_start = std::chrono::steady_clock::now();
    // Nothing is drawn: the battle side of the map only.
    auto built = build_battle_map(vfs, map_name, nullptr, spawn_types);
    if (!built)
    {
        std::cerr << "error: " << built.error().message << '\n';
        return 1;
    }
    const auto& asset = built.value();
    std::cout << "Battle: " << map_name << (asset.land ? " (land)" : " (space)") << ", built in "
              << format_fixed(std::chrono::duration<double>(std::chrono::steady_clock::now() - build_start).count(), 2) << " s | " << asset.entities.size() << " placed objects (" << asset.decorations
              << " decorations, not simulated), " << asset.spawnable.size() << " spawnable types\n";
    for (const auto& type : spawn_types)
    {
        if (std::ranges::find(asset.spawnable, type, &SpawnableType::type_name) == asset.spawnable.end())
        {
            std::cerr << "warning: '" << type << "' is not spawnable on this map\n";
        }
    }
    const auto ticks = options.ticks ? *options.ticks : replay && replay->end_tick ? *replay->end_tick : default_ticks;
    const AssetReader* scripts_reader = options.scripts ? &vfs : nullptr;
    const auto first = run_once(asset, map_name, options, replay ? &*replay : nullptr, ticks, scripts_reader);
    if (first.failed)
    {
        return 4;
    }
    print_run(first, first.stats.tick - 1U);
    int exit_code = 0;
    if (replay && replay->end_hash && replay->end_tick == first.stats.tick)
    {
        const bool same = *replay->end_hash == first.hash;
        std::cout << "Replay: state hash " << (same ? "matches the recording" : "differs from the recording (" + hex(*replay->end_hash) + ")") << '\n';
        exit_code = same ? exit_code : 4;
    }
    else if (replay && replay->end_hash)
    {
        std::cout << "Replay: ran to tick " << first.stats.tick << ", the recording ended at tick " << replay->end_tick.value_or(0) << "; state hashes not compared\n";
    }
    if (options.check_determinism)
    {
        const auto second = run_once(asset, map_name, options, replay ? &*replay : nullptr, ticks, scripts_reader);
        const bool same = !second.failed && second.hash == first.hash;
        std::cout << "Determinism: second run " << (same ? "matches" : "differs (" + hex(second.hash) + ")") << '\n';
        exit_code = same ? exit_code : 4;
        if (!same && options.hash_trace)
        {
            print_first_difference(first.trace, second.trace);
        }
    }
    if (options.record)
    {
        if (const auto saved = save_battle_log(*options.record, first.log); !saved)
        {
            std::cerr << "error: " << saved.error().message << '\n';
            return 1;
        }
        std::cout << "Recorded " << first.log.entries.size() << " orders to " << options.record->string() << '\n';
    }
    return exit_code;
}

} // namespace eaw
