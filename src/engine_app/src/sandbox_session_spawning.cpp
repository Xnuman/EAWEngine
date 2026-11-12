#include "engine_app/sandbox_session.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eaw {

void SandboxSession::spawn_units(const std::size_t type_index, const eaw::Vector3& point, const std::uint32_t count)
{
    const auto& type = spawnable[type_index];
    if (const auto submitted = battle->submit({.order = eaw::BattleOrder::spawn, .point = point, .type = type.type_name, .count = count, .owner = spawn_owner}); !submitted)
    {
        eaw::log_warning("simulation", submitted.error());
        return;
    }
    std::cout << "Spawning " << count << " " << type.type_name << (type.members.empty() ? std::string{} : " (" + std::to_string(type.members.size()) + " units each)") << " for owner " << spawn_owner
              << " at " << eaw::format_fixed(point.x, 0) << ", " << eaw::format_fixed(point.y, 0) << ", " << eaw::format_fixed(point.z, 0) << '\n';
}

void SandboxSession::spawn_named(const std::string& type, const eaw::Vector3& point, const std::uint32_t count)
{
    const auto loaded = std::ranges::find_if(spawnable,
                                             [&](const eaw::SpawnableType& candidate)
                                             {
                                                 return ascii_lower(candidate.type_name) == ascii_lower(type);
                                             });
    if (loaded != spawnable.end())
    {
        spawn_units(static_cast<std::size_t>(loaded - spawnable.begin()), point, count);
        return;
    }
    pending_spawns.push_back({type, point, count, spawn_owner});
    std::cout << "Loading " << type << " for spawning...\n";
    start_next_package();
}

void SandboxSession::start_next_package()
{
    if (pending_package.valid() || pending_spawns.empty() || pending_content.valid() || pending_check.valid())
    {
        return;
    }
    building_type = pending_spawns.front().type;
    pending_package = std::async(std::launch::async,
                                 [&files = vfs, type = building_type, land = map_land, cache = &asset_cache, fidelity = options.particle_fidelity]
                                 {
                                     eaw::MapBuildOptions build_options;
                                     build_options.particle_fidelity = fidelity;
                                     return eaw::build_spawnable_package(files, type, land, cache, build_options);
                                 });
}

void SandboxSession::poll_packages()
{
    if (pending_spawn_types.valid() && pending_spawn_types.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        auto types = pending_spawn_types.get();
        if (!types.empty() && battle)
        {
            const auto current = spawn_choice < spawn_names.size() ? spawn_names[spawn_choice] : std::string{};
            spawn_names.clear();
            for (const auto& type : types)
            {
                spawn_names.push_back(type.name);
            }
            spawn_types = std::move(types);
            const auto found = std::ranges::find(spawn_names, current);
            spawn_choice = found != spawn_names.end() ? static_cast<std::size_t>(found - spawn_names.begin()) : 0U;
            std::cout << "Spawn list: " << spawn_names.size() << " types (Tab/Shift+Tab, Ctrl for 10 at a time)\n";
        }
    }
    if (!pending_package.valid() || pending_package.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    {
        start_next_package();
        return;
    }
    auto built = pending_package.get();
    const auto type = building_type;
    const auto waiting = [&](const PendingSpawn& spawn)
    {
        return ascii_lower(spawn.type) == ascii_lower(type);
    };
    if (!built || !battle || !presentation || !animator)
    {
        eaw::log_warning("sandbox", built ? eaw::Error{"the map changed while " + type + " was being built"} : built.error());
        std::erase_if(pending_spawns, waiting);
        start_next_package();
        return;
    }
    auto package = std::move(built).value();
    for (const auto& warning : package.meshes.warnings)
    {
        std::cerr << "spawn warning: " << warning << '\n';
    }
    const std::vector<eaw::VisualMesh> meshes = package.meshes.meshes;
    auto added = renderer.add_meshes(std::move(package.meshes));
    if (!added || *added != animator->rig_count())
    {
        eaw::log_warning("sandbox", added ? eaw::Error{"mesh library and animation rigs are out of step"} : added.error());
        std::erase_if(pending_spawns, waiting);
        start_next_package();
        return;
    }
    for (auto& added_type : package.types)
    {
        added_type.mesh += *added;
    }
    for (auto& [key, visual] : package.projectile_visuals)
    {
        if (visual.mesh)
        {
            *visual.mesh += *added;
        }
    }
    presentation->add_meshes(meshes);
    presentation->add_spawnable(package.types);
    {
        const auto placed = register_particle_textures(std::move(package.particle_textures), package.rigs, {}, &package.effect_objects, &package.projectile_visuals);
        effects.add({package.rigs, &package.projectile_visuals, placed_atlas(package.projectile_atlas, placed), &package.effect_objects});
    }
    animator->add_rigs(package.rigs, package.rest_palette);
    (void)battle->add_spawnable(package.types);
    spawnable.assign(battle->spawnable().begin(), battle->spawnable().end());
    runtime_types.push_back(type);
    std::cout << "Loaded " << type << " (" << meshes.size() << " meshes)\n";
    const auto owner = spawn_owner;
    for (const auto& spawn : pending_spawns)
    {
        if (waiting(spawn))
        {
            spawn_owner = spawn.owner;
            spawn_named(spawn.type, spawn.point, spawn.count);
        }
    }
    spawn_owner = owner;
    std::erase_if(pending_spawns, waiting);
    start_next_package();
}

void SandboxSession::run_automation()
{
    if (!initial_spawns_sent && world && tactical_camera && content_shown_at && (!spawnable.empty() || !options.later_spawns.empty()))
    {
        initial_spawns_sent = true;
        initial_spawn_tick = world->stats().tick;
        // Each requested count spawns at the surface under the screen center.
        const auto stats = renderer.stats();
        const auto center = tactical_camera ? ground_target(static_cast<float>(stats.width) * 0.5F, static_cast<float>(stats.height) * 0.5F) : std::nullopt;
        // Requests spawn in command-line order, each group 180 units further along +X so
        // opposing groups start within typical weapon range of each other.
        const auto default_owner = spawn_owner;
        for (std::size_t request = 0; center && request < options.spawns.size(); ++request)
        {
            const auto& entry = options.spawns[request];
            const auto type = std::ranges::find(spawnable, entry.type, &eaw::SpawnableType::type_name);
            if (type == spawnable.end() || entry.count == 0)
            {
                continue;
            }
            spawn_owner = entry.owner.value_or(default_owner);
            auto point = *center;
            point.x += 180.0F * static_cast<float>(request);
            if (terrain_heights.valid())
            {
                point.z = terrain_heights.surface_height(point.x, point.y);
            }
            spawn_units(static_cast<std::size_t>(type - spawnable.begin()), point, entry.count);
        }
        // Types loaded at runtime spawn beside them once their packages are built.
        for (std::size_t request = 0; center && request < options.later_spawns.size(); ++request)
        {
            const auto& entry = options.later_spawns[request];
            spawn_owner = entry.owner.value_or(default_owner);
            auto point = *center;
            point.y += 180.0F * static_cast<float>(request + 1U);
            if (terrain_heights.valid())
            {
                point.z = terrain_heights.surface_height(point.x, point.y);
            }
            spawn_named(entry.type, point, std::max(entry.count, 1U));
        }
        spawn_owner = default_owner;
    }
    // Waits for the initial spawns so they are part of the order.
    if (options.auto_move && content_shown_at && world && !automated_move_sent && (spawnable.empty() || (initial_spawns_sent && world->stats().tick > initial_spawn_tick)))
    {
        automated_move_sent = true;
        selection.clear();
        for (const auto& entity : world->snapshot().entities)
        {
            if (entity.max_speed > 0.0F)
            {
                selection.insert(eaw::entity_key(entity.id));
            }
        }
        const auto& bounds = tactical_camera ? tactical_camera->focus() : Vector3{};
        std::cout << "Auto-move: " << selection.size() << " mobile units\n";
        order_move({bounds[0], bounds[1], bounds[2]});
    }
    if (options.pick && content_shown_at && !automated_pick_sent)
    {
        unbound_picks.push_back(renderer.request_pick((*options.pick)[0], (*options.pick)[1]));
        automated_pick_sent = true;
    }
}

} // namespace eaw
