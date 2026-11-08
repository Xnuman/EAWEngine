#include "battle_runner.hpp"

#include "battle.hpp"
#include "battle_log.hpp"
#include "text_format.hpp"
#include "visual_scene_builder.hpp"

#include "eaw/log.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace eaw {
namespace {

constexpr std::uint64_t default_ticks = 3000;

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
    std::size_t starting_entities{};
    bool failed{};
};

// The point groups spawn around and auto-move goes to: the middle of the ground on land maps, at
// its surface, and the middle of the placed objects in space. Both come from map data alone, so
// a headless build (no textures) and a full one place battles alike.
SimVector3 battle_center(const VisualMapAsset& asset)
{
    if (asset.ground.valid())
    {
        const auto& ground = asset.ground;
        SimVector3 center{static_cast<float>(ground.width - 1U) * ground.spacing * 0.5F, static_cast<float>(ground.height - 1U) * ground.spacing * 0.5F, 0.0F};
        center.z = ground.surface_height(center.x, center.y);
        return center;
    }
    const auto& minimum = asset.scene.bounds_minimum;
    const auto& maximum = asset.scene.bounds_maximum;
    return {(minimum[0] + maximum[0]) * 0.5F, (minimum[1] + maximum[1]) * 0.5F, 0.0F};
}

BattleRun run_once(const VisualMapAsset& asset, const std::string& map_name, const SandboxOptions& options, const BattleLog* replay, const std::uint64_t ticks)
{
    BattleRun run;
    Battle battle(battle_map(asset), map_name);
    auto& world = battle.world();
    run.starting_entities = world.stats().entities;
    const auto center = battle_center(asset);
    std::size_t next_entry = 0;
    bool auto_move_sent = false;
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
            const SimVector3 point{center.x + 180.0F * static_cast<float>(request), center.y, center.z};
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
            std::ranges::sort(movers,
                              [](const EntityId left, const EntityId right)
                              {
                                  return left.index != right.index ? left.index < right.index : left.generation < right.generation;
                              });
            if (!movers.empty())
            {
                if (const auto submitted = battle.submit({.order = BattleOrder::move, .entities = std::move(movers), .point = center}); !submitted)
                {
                    log_warning("battle", submitted.error());
                }
            }
        }
        const auto tick_start = std::chrono::steady_clock::now();
        battle.step();
        const auto milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tick_start).count();
        if (milliseconds > run.slowest_ms)
        {
            run.slowest_ms = milliseconds;
            run.slowest_tick = tick + 1U;
        }
    }
    run.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    run.hash = world.state_hash();
    run.stats = world.stats();
    run.log = battle.log();
    run.log.end_tick = run.stats.tick;
    run.log.end_hash = run.hash;
    return run;
}

void print_run(const BattleRun& run, const std::uint64_t ticks_run)
{
    const auto& stats = run.stats;
    std::cout << "Ran " << ticks_run << " ticks in " << format_fixed(run.seconds, 2) << " s: average " << format_fixed(ticks_run > 0 ? run.seconds * 1000.0 / static_cast<double>(ticks_run) : 0.0, 3)
              << " ms, slowest " << format_fixed(run.slowest_ms, 2) << " ms (tick " << run.slowest_tick << ")\n"
              << "State hash: " << hex(run.hash) << " at tick " << stats.tick << '\n'
              << "Entities: " << run.starting_entities << " at start, " << stats.entities << " at end | orders " << run.log.entries.size() << ", commands applied " << stats.commands_applied
              << ", stale references " << stats.stale_references << ", rejected spawns " << stats.rejected_spawns << '\n'
              << "Combat: " << stats.shots_fired << " shots, " << stats.hits << " hits, " << stats.kills << " kills, " << stats.hardpoints_destroyed << " hardpoints destroyed, " << stats.projectiles
              << " in flight\n"
              << "Paths: " << stats.path_searches << " planned, " << stats.partial_paths << " partial, " << stats.shared_routes << " shared, " << stats.path_expansions << " nodes expanded, "
              << stats.blocked_moves << " blocked moves\n";
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
    // Nothing is drawn: skip textures and animation clips.
    auto built = build_visual_map(vfs, map_name, nullptr, spawn_types, MapBuildOptions{.textures = false, .animation_clips = false});
    if (!built)
    {
        std::cerr << "error: " << built.error().message << '\n';
        return 1;
    }
    const auto& asset = built.value();
    std::cout << "Battle: " << map_name << (asset.land ? " (land)" : " (space)") << ", built in "
              << format_fixed(std::chrono::duration<double>(std::chrono::steady_clock::now() - build_start).count(), 2) << " s | " << asset.entities.size() << " placed objects, "
              << asset.spawnable.size() << " spawnable types\n";
    for (const auto& type : spawn_types)
    {
        if (std::ranges::find(asset.spawnable, type, &SpawnableType::type_name) == asset.spawnable.end())
        {
            std::cerr << "warning: '" << type << "' is not spawnable on this map\n";
        }
    }
    const auto ticks = options.ticks ? *options.ticks : replay && replay->end_tick ? *replay->end_tick : default_ticks;
    const auto first = run_once(asset, map_name, options, replay ? &*replay : nullptr, ticks);
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
        const auto second = run_once(asset, map_name, options, replay ? &*replay : nullptr, ticks);
        const bool same = !second.failed && second.hash == first.hash;
        std::cout << "Determinism: second run " << (same ? "matches" : "differs (" + hex(second.hash) + ")") << '\n';
        exit_code = same ? exit_code : 4;
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
