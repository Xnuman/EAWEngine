#include "formats/ala_animation_helpers.hpp"
#include "engine_app/sandbox_session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace eaw {

void SandboxSession::start_simulation(const eaw::VisualScene& scene, const eaw::BattleMap& map, const std::span<const eaw::MapEntityLook> looks, std::string name)
{
    battle_scripts.reset();
    battle.emplace(map, std::move(name), simulation_clock.tick_seconds());
    world = &battle->world();
    if (options.scripts)
    {
        battle_scripts.emplace(vfs);
    }
    presentation.emplace(scene, map.entities, looks, battle->map_entity_ids(), battle->spawnable());
    previous_snapshot = world->snapshot();
    selection.clear();
    float extent = 0.0F;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        extent = std::max(extent, scene.bounds_maximum[axis] - scene.bounds_minimum[axis]);
    }
    axis_length = std::max(extent * 0.1F, 1.0F);
    simulation_tick = presentation_now();
}

std::vector<std::uint32_t> SandboxSession::register_particle_textures(std::vector<eaw::VisualTexture> textures, std::span<eaw::VisualRig> rigs,
                                                                      std::span<std::shared_ptr<const eaw::ParticleEffect>> named_effects, eaw::ParticleEffectObjectTable* objects,
                                                                      eaw::ProjectileVisualTable* projectiles)
{
    std::vector<std::uint32_t> placed;
    if (!textures.empty())
    {
        if (auto added = renderer.add_textures(std::move(textures)))
        {
            placed = std::move(added).value();
        }
        else
        {
            eaw::log_warning("particles", added.error());
        }
    }
    eaw::renumber_effect_textures(rigs, named_effects, placed, objects, projectiles);
    return placed;
}

std::uint32_t SandboxSession::placed_atlas(const std::uint32_t atlas, const std::span<const std::uint32_t> placed)
{
    return atlas < placed.size() ? placed[atlas] : eaw::invalid_visual_texture;
}

bool SandboxSession::load_content_scene(eaw::VisualScene scene, const bool reload)
{
    const auto loaded = renderer.load_scene(std::move(scene));
    if (!loaded)
    {
        eaw::log_warning("scene", loaded.error().message + (reload ? "; keeping the current view" : "; using the validation triangle"));
    }
    return loaded.ok();
}

eaw::VisualScene SandboxSession::presentation_layout(const eaw::VisualScene& scene)
{
    eaw::VisualScene layout;
    layout.name = scene.name;
    layout.meshes = scene.meshes;
    layout.instances = scene.instances;
    layout.bone_count = scene.bone_count;
    layout.bounds_minimum = scene.bounds_minimum;
    layout.bounds_maximum = scene.bounds_maximum;
    return layout;
}

void SandboxSession::apply_content(eaw::SandboxContent content)
{
    scene_dependencies = std::move(content.dependencies);
    if (content.reload)
    {
        const auto stats = asset_cache.stats();
        std::cout << "Hot reload: rebuilt in " << content.build_seconds << " s; " << content.invalidated.size() << " cached assets re-decoded, " << content.evicted << " unused evicted, "
                  << stats.entries << " cached\n";
        reload_status = "rebuilt in " + eaw::format_fixed(content.build_seconds, 2) + " s, " + std::to_string(content.invalidated.size()) + " assets re-decoded";
    }
    if (!content.error.empty())
    {
        eaw::log_warning(options.map ? "map" : "model", content.error + (content.reload ? "; keeping the current view" : "; using the validation triangle"));
        return;
    }
    if (content.map)
    {
        auto& visual_map = *content.map;
        const auto& scene = visual_map.scene;
        const auto map_status =
            scene.name + (visual_map.land ? " (land)" : " (space)") + ", " + std::to_string(scene.instances.size()) + " objects, built in " + eaw::format_fixed(content.build_seconds, 2) + " s";
        std::cout << "Visual map: " << scene.name << (visual_map.land ? " (land)" : " (space)") << '\n'
                  << "Terrain: "
                  << (scene.terrain.vertices.empty() ? std::string("none")
                                                     : std::to_string(scene.terrain.vertices.size() / 3U) + " triangles, " + std::to_string(scene.terrain.layer_count) + " layers" +
                                                           (visual_map.water == eaw::TedWaterType::water  ? ", water"
                                                            : visual_map.water == eaw::TedWaterType::lava ? ", lava"
                                                            : visual_map.water == eaw::TedWaterType::ice  ? ", ice"
                                                                                                          : "") +
                                                           ", " + std::to_string(visual_map.tracks) + " tracks")
                  << '\n'
                  << "Objects: " << scene.instances.size() << " instances of " << scene.meshes.size() << " models | squadron members expanded: " << visual_map.expanded_members
                  << " | companies: " << visual_map.expanded_companies << " (" << visual_map.company_members << " members) | obstacle footprints: " << visual_map.obstacle_footprints
                  << " | space obstacles: " << visual_map.space_obstacles << "\n"
                  << "Not drawn: " << visual_map.untyped << " untyped, " << visual_map.without_model << " without model, " << visual_map.missing_model_files << " missing model files, "
                  << visual_map.unexpanded_containers << " containers without members, " << visual_map.unrenderable_placements << " unrenderable models\n"
                  << "Geometry: " << scene.vertices.size() << " vertices, " << scene.indices.size() / 3U << " triangles, " << scene.draws.size() << " draws | Textures: " << scene.textures.size()
                  << " | Built in " << content.build_seconds << " s\n"
                  << "Lighting: " << (visual_map.lighting ? "environment '" + visual_map.environment_name + "'" : std::string("engine default")) << " | team colors: " << visual_map.faction_colored
                  << " faction, " << visual_map.player_colored << " player-slot fallback\n"
                  << "Backdrop: ";
        if (visual_map.backdrops.empty())
        {
            std::cout << "none";
        }
        for (std::size_t index = 0; index < visual_map.backdrops.size(); ++index)
        {
            std::cout << (index == 0 ? "" : ", ") << visual_map.backdrops[index];
        }
        std::cout << '\n';
        for (const auto& warning : scene.warnings)
        {
            std::cerr << "map warning: " << warning << '\n';
        }
        const eaw::CameraBounds bounds{scene.bounds_minimum, scene.bounds_maximum};
        std::optional<eaw::VisualScene> reload_copy;
        if (options.reload_every)
        {
            reload_copy = scene;
        }
        const auto layout = presentation_layout(scene);
        if (!load_content_scene(std::move(visual_map.scene), content.reload))
        {
            return;
        }
        scene_status = map_status;
        reload_scene = std::move(reload_copy);
        if (options.reload_every)
        {
            particle_texture_copy = visual_map.particle_textures;
        }
        const auto placed = register_particle_textures(std::move(visual_map.particle_textures), visual_map.rigs, visual_map.effects, &visual_map.effect_objects, &visual_map.projectile_visuals);
        start_simulation(layout, eaw::battle_map(visual_map), visual_map.entity_looks, *options.map);
        audio.reset();
        audio.emplace(vfs, &asset_cache, *audio_device, options.language, visual_map.land);
        animator.emplace(visual_map.rigs, visual_map.rest_palette);
        effects.reset({visual_map.rigs, &visual_map.projectile_visuals, placed_atlas(visual_map.projectile_atlas, placed), &visual_map.effect_objects});
        shown_effects = visual_map.effects;
        shown_effect_ids.assign(shown_effects.size(), {});
        effect_center = {(bounds.minimum[0] + bounds.maximum[0]) * 0.5F, (bounds.minimum[1] + bounds.maximum[1]) * 0.5F, 0.0F};
        if (visual_map.land && visual_map.ground.valid())
        {
            effect_center[2] = visual_map.ground.surface_height(effect_center[0], effect_center[1]);
            effects.set_ground(
                [this](const float x, const float y) -> std::optional<float>
                {
                    return terrain_heights.valid() ? std::optional<float>(terrain_heights.surface_height(x, y)) : std::nullopt;
                });
        }
        else
        {
            effects.set_ground({});
        }
        for (std::size_t index = 0; index < shown_effects.size(); ++index)
        {
            std::cout << "Effect: " << options.effects[index] << (shown_effects[index] ? " (" + std::to_string(shown_effects[index]->emitters.size()) + " emitters)" : " not loaded") << '\n';
        }
        map_animation_start = presentation_now();
        world_paused = false;
        {
            std::size_t animated_meshes = 0;
            std::size_t clips = 0;
            for (const auto& rig : visual_map.rigs)
            {
                animated_meshes += rig.clips.empty() ? 0U : 1U;
                clips += rig.clips.size();
            }
            std::cout << "Animation: " << animated_meshes << " of " << visual_map.rigs.size() << " meshes have clips (" << clips << " clips), rest palette " << visual_map.rest_palette.size()
                      << " entries\n";
            std::cout << "Impact and death effects: " << effects.playable_effect_objects() << " of " << effects.effect_objects() << " particle objects playable\n";
            std::size_t bolts = 0;
            std::size_t models = 0;
            std::size_t particle_models = 0;
            for (const auto& [key, visual] : effects.projectile_visuals())
            {
                bolts += visual.bolt ? 1U : 0U;
                models += !visual.bolt && visual.mesh ? 1U : 0U;
                particle_models += !visual.bolt && visual.effect ? 1U : 0U;
            }
            std::cout << "Projectile looks: " << bolts << " bolts, " << models << " models, " << particle_models << " particle systems, "
                      << effects.projectile_visuals().size() - bolts - models - particle_models << " not drawn\n";
        }
        spawnable = visual_map.spawnable;
        terrain_heights = visual_map.ground;
        map_land = visual_map.land;
        spawn_names.clear();
        for (const auto& type : spawnable)
        {
            spawn_names.push_back(type.type_name);
        }
        // The full list comes from the cached XML catalog on a worker (no remount runs meanwhile).
        if (!pending_spawn_types.valid())
        {
            spawn_types.clear();
            pending_spawn_types = std::async(std::launch::async,
                                             [&files = vfs, land = map_land, cache = &asset_cache]
                                             {
                                                 return eaw::spawnable_types(files, land, cache);
                                             });
        }
        spawn_choice = 0;
        initial_spawns_sent = false;
        for (const auto& type : spawnable)
        {
            std::cout << "Spawnable: " << type.type_name << " (speed " << eaw::format_fixed(type.max_speed, 1) << ")\n";
        }
        if (visual_map.lighting)
        {
            renderer.set_lighting(*visual_map.lighting);
        }
        // A hot reload keeps the viewer's camera.
        if (!content.reload || !tactical_camera)
        {
            tactical_camera.emplace(bounds);
            if (options.camera_view)
            {
                tactical_camera->set_view({(*options.camera_view)[0], (*options.camera_view)[1]}, (*options.camera_view)[2]);
            }
            orbit_camera.reset();
            renderer.set_camera(tactical_camera->camera());
            std::cout << "Tactical camera: WASD/arrows pan (Shift is faster), wheel zooms, Space resets, C cycles the camera modes (fixed, debug, free), P pauses the "
                         "world.\n";
        }
    }
    else if (content.model)
    {
        auto& scene = content.model->scene;
        const auto model_status = scene.name + ", " + std::to_string(scene.bone_count) + " palette entries, built in " + eaw::format_fixed(content.build_seconds, 2) + " s";
        std::cout << "Visual model: " << scene.name << '\n'
                  << "Geometry: " << scene.vertices.size() << " vertices, " << scene.indices.size() / 3U << " triangles, " << scene.draws.size() << " draws\n"
                  << "Textures: " << scene.textures.size() << " | Bones: " << scene.bone_count << " | Built in " << content.build_seconds << " s\n";
        for (const auto& warning : scene.warnings)
        {
            std::cerr << "model warning: " << warning << '\n';
        }
        const eaw::CameraBounds bounds{scene.bounds_minimum, scene.bounds_maximum};
        std::optional<eaw::VisualScene> reload_copy;
        if (options.reload_every)
        {
            reload_copy = scene;
        }
        // The renderer takes the scene; keep only the skeleton data needed for animation.
        // The model is one immobile entity.
        std::vector<eaw::MapEntity> model_entity;
        std::vector<eaw::MapEntityLook> model_look;
        if (!scene.instances.empty())
        {
            const auto& matrix = scene.instances.front().transform;
            eaw::EntitySpawn spawn;
            spawn.type_name = scene.name;
            spawn.position = eaw::translation(matrix);
            spawn.heading = eaw::model_forward_heading(matrix);
            spawn.visual = scene.instances.front().mesh;
            model_entity.push_back({std::move(spawn)});
            model_look.push_back({0});
        }
        const auto layout = presentation_layout(scene);
        if (!load_content_scene(std::exchange(scene, {}), content.reload))
        {
            return;
        }
        visual_model = std::move(content.model);
        animator.reset();
        audio.reset();
        effects.reset({});
        shown_effects.clear();
        shown_effect_ids.clear();
        spawn_names.clear();
        pending_spawns.clear();
        scene_status = model_status;
        animation_status = "bind pose";
        reload_scene = std::move(reload_copy);
        eaw::BattleMap model_map;
        model_map.entities = model_entity;
        start_simulation(layout, model_map, model_look, options.model);
        bind_palette = eaw::sample_skin_palette(*visual_model, nullptr, 0.0F);
        renderer.set_bone_transforms(bind_palette);
        if (!content.reload || !orbit_camera)
        {
            orbit_camera.emplace(bounds);
            renderer.set_camera(orbit_camera->camera());
        }
        if (!content.animation_error.empty())
        {
            std::cerr << "animation warning: " << content.animation_error << '\n';
        }
        visual_animation.reset();
        animation_paths = std::move(content.animations);
        animation_choice = 0;
        if (content.animation)
        {
            if (const auto found = std::ranges::find(animation_paths, content.animation_path); found != animation_paths.end())
            {
                animation_choice = static_cast<std::size_t>(found - animation_paths.begin()) + 1U;
            }
            visual_animation = std::move(content.animation);
            animation_status = content.animation_path;
            std::cout << "Animation: " << content.animation_path << " | " << visual_animation->loop_frame_count << " frames at " << visual_animation->frames_per_second << " fps | "
                      << eaw::animation_duration(*visual_animation) << " seconds\n";
            for (const auto& warning : content.animation_warnings)
            {
                std::cerr << "animation warning: " << warning << '\n';
            }
        }
        if (!content.reload)
        {
            std::cout << "Left-drag orbits, wheel zooms, Space resets, P pauses animation, Left/Right select among " << animation_paths.size() << " animations.\n";
        }
    }
    if (content.reload)
    {
        const auto stats = renderer.stats();
        std::cout << "Hot reload: " << stats.reused_textures << " textures kept on the GPU, " << stats.pending_texture_uploads << " uploading\n";
    }
}

void SandboxSession::poll_content()
{
    if (pending_content.valid() && pending_content.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        // A failed build or a scene the renderer rejects keeps the current view.
        apply_content(pending_content.get());
        if (!content_shown_at)
        {
            content_shown_at = renderer.stats().frame_number;
        }
        textures_streamed = false;
        streaming_since = renderer.stats().frame_number;
        animation_tick = presentation_now();
        next_reload_check = animation_tick + std::chrono::seconds(1);
    }
}

void SandboxSession::poll_hot_reload()
{
    // Hot reload: once a second (and never during a build) a worker compares the scene's
    // dependencies and the MEG archives with their recorded state. Rebuilt archives are
    // remounted here, while no other thread reads the file system, then the scene rebuilds
    // in the background from the cache.
    // Archives are remounted only while no worker reads through the file system.
    if (options.hot_reload && scene_dependencies && !pending_content.valid() && !pending_package.valid() && !pending_spawn_types.valid())
    {
        if (pending_check.valid() && pending_check.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            auto check = pending_check.get();
            if (!reload_check_reported)
            {
                reload_check_reported = true;
                std::cout << "Hot reload: watching " << scene_dependencies->file_count() << " files and " << scene_dependencies->listing_count() << " listings; a check takes " << check.seconds
                          << " s\n";
                reload_status = "watching " + std::to_string(scene_dependencies->file_count()) + " files";
            }
            next_reload_check = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            bool remounted = true;
            for (const auto index : check.archives)
            {
                const auto& mount = vfs.mounts()[index];
                const auto archive = mount->source_path();
                // Only archive mounts are watched for rebuilds (kind meg_archive).
                auto replacement = static_cast<const eaw::MegMount&>(*mount).reopen();
                if (!replacement)
                {
                    // Typically the archive is still being written; the next check retries.
                    std::cerr << "hot reload warning: " << archive.string() << " could not be remounted (" << replacement.error().message << "); retrying\n";
                    remounted = false;
                    continue;
                }
                vfs.replace_mount(index, std::move(replacement).value());
                archive_watch.accept(vfs, index);
                std::cout << "Hot reload: remounted " << archive.string() << '\n';
            }
            if (remounted && (!check.archives.empty() || !check.files.empty()))
            {
                std::cout << "Hot reload: " << check.files.size() << " changed dependencies";
                for (std::size_t index = 0; index < check.files.size() && index < 3; ++index)
                {
                    std::cout << (index == 0 ? " (" : ", ") << check.files[index];
                }
                std::cout << (check.files.size() > 3 ? ", ...)" : check.files.empty() ? "" : ")") << "; rebuilding\n";
                pending_content = std::async(std::launch::async, eaw::build_sandbox_content, std::cref(vfs), std::cref(options), &asset_cache, true, runtime_types);
                reload_status = std::to_string(check.files.size()) + " changes, rebuilding...";
            }
        }
        else if (!pending_check.valid() && std::chrono::steady_clock::now() >= next_reload_check)
        {
            pending_check = std::async(std::launch::async,
                                       [&files = vfs, &watch = archive_watch, dependencies = *scene_dependencies]
                                       {
                                           const auto start = std::chrono::steady_clock::now();
                                           HotReloadCheck result{watch.changed(files), dependencies.changes(files), 0.0};
                                           result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                                           return result;
                                       });
        }
    }
}

} // namespace eaw
