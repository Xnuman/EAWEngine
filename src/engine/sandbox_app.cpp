#include "sandbox_app.hpp"

#include "asset_cache.hpp"
#include "camera_controller.hpp"
#include "camera_math.hpp"
#include "combat_content.hpp"
#include "rts_control.hpp"
#include "sandbox_content.hpp"
#include "sandbox_debug_lines.hpp"
#include "sandbox_overlay.hpp"
#include "scene_presentation.hpp"
#include "text_format.hpp"
#include "visual_scene_builder.hpp"

#include "eaw/d3d12_renderer.hpp"
#include "eaw/log.hpp"
#include "eaw/platform_window.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eaw {
namespace {

// The sandbox's state between frames and the steps of one frame. Members keep the declaration
// order the work depends on: the asset cache and archive watch outlive the futures that use them.
struct SandboxSession
{
    const eaw::SandboxOptions& options;
    eaw::VirtualFileSystem& vfs;
    eaw::PlatformWindow& window;
    eaw::D3d12Renderer& renderer;

    std::optional<eaw::VisualModelAsset> visual_model;
    std::optional<eaw::AlaAnimation> visual_animation;
    // Model inspection orbits the model; a map uses the tactical camera. The validation
    // triangle, shown while content builds, gets an orbit over its small extent.
    std::optional<eaw::OrbitCamera> orbit_camera{std::in_place, eaw::CameraBounds{{-0.8F, -0.7F, -0.1F}, {0.8F, 0.8F, 0.1F}}};
    std::optional<eaw::TacticalCamera> tactical_camera;
    // Kept only for --reload-every; otherwise the CPU copy moves into the renderer.
    std::optional<eaw::VisualScene> reload_scene;
    std::vector<std::array<float, 16>> bind_palette;
    // Runtime-spawnable types of the current map; N spawns `spawn_choice` at the cursor.
    std::vector<eaw::SpawnableType> spawnable;
    // The map's ground surface for CPU order targets (invalid on space maps and models).
    eaw::GroundMap terrain_heights;
    std::size_t spawn_choice = 0;
    // Owner slot of debug spawns (O cycles through the non-neutral slots 0..7).
    std::uint32_t spawn_owner = 1;
    // Pick requests: the instance-to-entity list of the frame that drew each one (instance order
    // changes whenever an entity dies), requests still waiting for that frame, and which
    // requests are right-click attack orders rather than selections.
    std::unordered_map<std::uint32_t, std::vector<eaw::EntityId>> pick_entities;
    std::vector<std::uint32_t> unbound_picks;
    std::unordered_set<std::uint32_t> attack_picks;
    bool initial_spawns_sent = false;
    // Every shown object is a simulation entity; backdrops stay static. Presentation re-poses the
    // scene's instances from interpolated snapshots each frame.
    std::optional<eaw::SimulationWorld> world;
    std::optional<eaw::ScenePresentation> presentation;
    eaw::FixedStepClock simulation_clock{30.0};
    eaw::SimSnapshot previous_snapshot;
    std::vector<eaw::VisualInstance> frame_instances;
    std::vector<eaw::EntityId> frame_entities;
    std::unordered_set<std::uint64_t> selection;
    std::chrono::steady_clock::time_point simulation_tick = std::chrono::steady_clock::now();
    float axis_length = 1.0F;
    bool show_all_bounds = options.show_bounds;
    // G: passability classes drawn around the camera focus (land maps).
    bool show_passability = options.show_passability;
    // Text shown on the statistics overlay; updated where the console reports the same events.
    std::string scene_status = options.validation_triangle ? "validation triangle" : "building in the background...";
    std::string animation_status;
    std::string reload_status = options.hot_reload ? "waiting for the scene" : "off (--hot-reload)";
    // Model mode: Left/Right step through [bind pose, animations...].
    std::vector<std::string> animation_paths;
    std::size_t animation_choice = 0;

    // The window stays responsive while the map or model builds on a worker thread. The future
    // is declared after the VFS, so leaving early waits for the worker before the VFS goes away.
    // Decoded assets shared by every build; a hot reload re-decodes only what changed.
    eaw::AssetCache asset_cache;
    eaw::ArchiveWatch archive_watch{vfs};
    std::optional<eaw::DependencySet> scene_dependencies;
    std::future<eaw::SandboxContent> pending_content;
    std::future<eaw::HotReloadCheck> pending_check;
    std::chrono::steady_clock::time_point next_reload_check = std::chrono::steady_clock::now();
    // Frame count at which the content was shown; --frame-limit counts from here.
    std::optional<std::uint64_t> content_shown_at;
    std::chrono::steady_clock::time_point animation_tick = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point scene_start = animation_tick;
    std::chrono::steady_clock::time_point camera_tick = animation_tick;
    float animation_time = 0.0F;
    bool animation_playing = true;
    std::uint64_t scene_reloads = 0;
    bool textures_streamed = false;
    bool reload_check_reported = false;
    std::int64_t click_travel = 0;
    bool overlay_visible = options.overlay;
    std::uint64_t overlay_frames = 0;
    std::chrono::steady_clock::time_point overlay_refreshed = std::chrono::steady_clock::now();
    bool automated_pick_sent = false;
    bool automated_move_sent = false;
    // Maps use the legacy RTS camera (no rotation; left-drag box-selects, right-click moves);
    // C switches to the free debug camera (drag rotates, right-drag pans).
    bool free_camera = false;
    std::optional<std::array<float, 2>> drag_start;
    bool box_active = false;
    std::uint64_t orders_issued = 0;
    // Simulation step cost (move orders are planned inside the step that applies them).
    double last_tick_ms = 0.0;
    double slowest_tick_ms = 0.0;
    std::uint64_t streaming_since = 0;

    SandboxSession(const eaw::SandboxOptions& session_options, eaw::VirtualFileSystem& session_vfs, eaw::PlatformWindow& session_window, eaw::D3d12Renderer& session_renderer)
        : options(session_options), vfs(session_vfs), window(session_window), renderer(session_renderer)
    {
        if (!options.validation_triangle)
        {
            pending_content = std::async(std::launch::async, eaw::build_sandbox_content, std::cref(vfs), std::cref(options), &asset_cache, false);
            std::cout << "Building " << (options.map ? "map " + *options.map : "model " + options.model) << " in the background...\n";
        }
        if (options.validation_triangle)
        {
            content_shown_at = 0;
        }
        renderer.set_camera(orbit_camera->camera());
        std::cout << "D3D12 adapter: " << renderer.adapter_name() << '\n'
                  << "Present mode: " << (options.vsync ? "vsync" : "immediate when tearing is available") << '\n'
                  << "Frame latency: " << options.frame_latency << '\n'
                  << "Texture upload budget: " << (options.upload_budget_mib == 0 ? std::string("unlimited") : std::to_string(options.upload_budget_mib) + " MiB per frame") << '\n'
                  << "Pipeline cache: " << renderer.stats().pipeline_cache_status << " (" << renderer.stats().pipeline_cache_hits << " hits, " << renderer.stats().pipeline_cache_misses
                  << " misses)\n";
    }

    std::string describe_instance(const std::size_t index) const
    {
        const auto& instance = frame_instances[index];
        const auto type = frame_entities[index].valid() ? presentation->type_name(frame_entities[index]) : std::string{};
        const auto& mesh = presentation->mesh_name(instance.mesh);
        return type.empty() || type == mesh ? mesh : type + " (" + mesh + ")";
    }

    void start_simulation(const eaw::VisualScene& scene, const std::span<const eaw::MapEntitySpawn> entities)
    {
        world.emplace(simulation_clock.tick_seconds());
        presentation.emplace(scene, entities, *world);
        world->step();
        previous_snapshot = world->snapshot();
        selection.clear();
        float extent = 0.0F;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            extent = std::max(extent, scene.bounds_maximum[axis] - scene.bounds_minimum[axis]);
        }
        axis_length = std::max(extent * 0.1F, 1.0F);
        simulation_tick = std::chrono::steady_clock::now();
    }

    bool load_content_scene(eaw::VisualScene scene, const bool reload)
    {
        const auto loaded = renderer.load_scene(std::move(scene));
        if (!loaded)
        {
            eaw::log_warning("scene", loaded.error().message + (reload ? "; keeping the current view" : "; using the validation triangle"));
        }
        return loaded.ok();
    }

    static eaw::VisualScene presentation_layout(const eaw::VisualScene& scene)
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

    void apply_content(eaw::SandboxContent content)
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
                      << " | companies: " << visual_map.expanded_companies << " (" << visual_map.company_members << " members) | obstacle footprints: " << visual_map.obstacle_footprints << "\n"
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
            start_simulation(layout, visual_map.entities);
            spawnable = visual_map.spawnable;
            terrain_heights = visual_map.ground;
            world->set_ground(terrain_heights);
            world->set_damage_table(visual_map.damage_table);
            world->set_hostility(eaw::provisional_map_hostility());
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
                orbit_camera.reset();
                renderer.set_camera(tactical_camera->camera());
                std::cout << "Tactical camera: WASD/arrows or right-drag pan (Shift is faster), left-drag rotates, wheel zooms, Space resets.\n";
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
            std::vector<eaw::MapEntitySpawn> model_entity;
            if (!scene.instances.empty())
            {
                const auto& matrix = scene.instances.front().transform;
                eaw::EntitySpawn spawn;
                spawn.type_name = scene.name;
                spawn.position = {matrix[12], matrix[13], matrix[14]};
                spawn.heading = std::atan2(matrix[1], matrix[0]);
                spawn.visual = scene.instances.front().mesh;
                model_entity.push_back({std::move(spawn), 0});
            }
            const auto layout = presentation_layout(scene);
            if (!load_content_scene(std::exchange(scene, {}), content.reload))
            {
                return;
            }
            visual_model = std::move(content.model);
            scene_status = model_status;
            animation_status = "bind pose";
            reload_scene = std::move(reload_copy);
            start_simulation(layout, model_entity);
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
                std::cout << "Animation: " << content.animation_path << " | " << visual_animation->playable_frame_count() << " frames at " << visual_animation->frames_per_second << " fps | "
                          << visual_animation->duration_seconds() << " seconds\n";
                for (const auto& warning : visual_animation->warnings)
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

    std::optional<eaw::SimVector3> ground_target(const float x, const float y) const
    {
        const auto stats = renderer.stats();
        const auto camera = tactical_camera->camera();
        const auto width = static_cast<float>(stats.width);
        const auto height = static_cast<float>(stats.height);
        auto point = eaw::intersect_terrain(camera, width, height, x, y, terrain_heights);
        if (!point)
        {
            point = eaw::intersect_ground_plane(camera, width, height, x, y, terrain_heights.liquid_level.value_or(0.0F));
        }
        return point ? std::optional(eaw::SimVector3{(*point)[0], (*point)[1], (*point)[2]}) : std::nullopt;
    }

    void order_move(const eaw::SimVector3& target)
    {
        // Selected mobile units, in id order, move as one formation centered on the target.
        std::vector<eaw::EntityId> movers;
        for (const auto& entity : world->snapshot().entities)
        {
            if (selection.contains(eaw::entity_key(entity.id)) && entity.max_speed > 0.0F)
            {
                movers.push_back(entity.id);
            }
        }
        std::ranges::sort(movers, {}, eaw::entity_key);
        const auto count = movers.size();
        world->submit({.kind = eaw::SimCommandKind::formation_move, .entities = std::move(movers), .target = target});
        ++orders_issued;
        std::cout << "Move " << count << " units to " << eaw::format_fixed(target.x, 0) << ", " << eaw::format_fixed(target.y, 0) << ", " << eaw::format_fixed(target.z, 0) << '\n';
    }

    void spawn_units(const std::size_t type_index, const eaw::SimVector3& point, const std::uint32_t count)
    {
        if (!spawnable[type_index].members.empty())
        {
            spawn_companies(type_index, point, count);
            return;
        }
        const auto& type = spawnable[type_index];
        const auto spots = eaw::grid_targets(point, count, 40.0F);
        for (auto spot : spots)
        {
            if (terrain_heights.valid())
            {
                spot.z = terrain_heights.surface_height(spot.x, spot.y) + (type.follows_ground ? 0.0F : type.hover_offset);
            }
            if (const auto spawned = presentation->spawn(*world, type, spot, 0.0F, spawn_owner, type.follows_ground && terrain_heights.valid()); !spawned)
            {
                eaw::log_warning("simulation", spawned.error());
            }
        }
        std::cout << "Spawned " << spots.size() << " " << type.type_name << " for owner " << spawn_owner << " at " << eaw::format_fixed(point.x, 0) << ", " << eaw::format_fixed(point.y, 0) << ", "
                  << eaw::format_fixed(point.z, 0) << '\n';
    }

    // Spawns `count` companies of the type side by side, each as its members in formation.
    void spawn_companies(const std::size_t type_index, const eaw::SimVector3& point, const std::uint32_t count)
    {
        const auto& company = spawnable[type_index];
        const auto centers = eaw::grid_targets(point, count, 160.0F);
        for (const auto& center : centers)
        {
            std::vector<eaw::FormationMember> members;
            for (std::size_t member = 0; member < company.members.size(); ++member)
            {
                const auto& type = spawnable[company.members[member]];
                members.push_back({eaw::EntityId{static_cast<std::uint32_t>(member + 1U), 1}, center, type.radius, type.formation_spacing, type.formation_order});
            }
            const auto slots = eaw::formation_slots(members, center, 0.0F);
            for (std::size_t member = 0; member < company.members.size(); ++member)
            {
                const auto& type = spawnable[company.members[member]];
                auto spot = slots[member].position;
                if (terrain_heights.valid())
                {
                    spot.z = terrain_heights.surface_height(spot.x, spot.y) + (type.follows_ground ? 0.0F : type.hover_offset);
                }
                if (const auto spawned = presentation->spawn(*world, type, spot, 0.0F, spawn_owner, type.follows_ground && terrain_heights.valid()); !spawned)
                {
                    eaw::log_warning("simulation", spawned.error());
                }
            }
        }
        std::cout << "Spawned " << centers.size() << " " << company.type_name << " (" << company.members.size() << " units each) for owner " << spawn_owner << '\n';
    }

    void order_attack(const eaw::EntityId victim)
    {
        const auto& entities = world->snapshot().entities;
        const auto found = std::ranges::find(entities, victim, &eaw::EntitySnapshot::id);
        if (found == entities.end() || found->max_health <= 0.0F)
        {
            return;
        }
        // Selected units hostile to the victim, in id order; unarmed ones are ignored by the
        // simulation and keep their move.
        std::vector<eaw::EntityId> attackers;
        for (const auto& entity : entities)
        {
            if (selection.contains(eaw::entity_key(entity.id)) && world->hostility().hostile(entity.owner, found->owner))
            {
                attackers.push_back(entity.id);
            }
        }
        if (attackers.empty())
        {
            return;
        }
        std::ranges::sort(attackers, {}, eaw::entity_key);
        world->submit({.kind = eaw::SimCommandKind::attack, .entities = std::move(attackers), .target_entity = victim});
        ++orders_issued;
        std::cout << "Attack " << presentation->type_name(victim) << " (owner " << found->owner << ")\n";
    }

    void update_debug_lines()
    {
        eaw::SandboxDebugView view;
        view.presentation = presentation ? &*presentation : nullptr;
        view.world = world ? &*world : nullptr;
        view.instances = frame_instances;
        view.entities = frame_entities;
        view.selection = &selection;
        // The simulation's ground shows structure footprints stamped into its passability.
        view.ground = world ? &world->ground() : &terrain_heights;
        if (tactical_camera)
        {
            view.focus = tactical_camera->focus();
        }
        view.show_bounds = show_all_bounds;
        view.show_passability = show_passability;
        view.axis_length = axis_length;
        view.friendly_owner = spawn_owner;
        renderer.set_debug_lines(eaw::build_sandbox_debug_lines(view));
    }

    // Refreshes the statistics panel (four times a second, so the numbers stay readable).
    void update_overlay(const std::chrono::steady_clock::time_point now)
    {
        eaw::SandboxOverlayView view;
        view.adapter_name = renderer.adapter_name();
        view.stats = renderer.stats();
        view.vsync = options.vsync;
        view.frame_latency = options.frame_latency;
        view.raw_input = window.input().raw_input;
        view.raw_events = window.input().raw_events;
        view.frames = overlay_frames;
        view.seconds = std::chrono::duration<double>(now - overlay_refreshed).count();
        view.scene_status = scene_status;
        view.eye = tactical_camera ? tactical_camera->camera().eye : orbit_camera->camera().eye;
        if (visual_model)
        {
            view.animation_status = animation_status;
        }
        view.animation_playing = animation_playing;
        std::string selected_text = "none";
        if (selection.size() == 1)
        {
            for (std::size_t index = 0; index < frame_entities.size(); ++index)
            {
                if (frame_entities[index].valid() && selection.contains(eaw::entity_key(frame_entities[index])))
                {
                    selected_text = describe_instance(index);
                    break;
                }
            }
        }
        else if (!selection.empty())
        {
            selected_text = std::to_string(selection.size()) + " objects";
        }
        view.selected_text = selected_text;
        view.world = world ? &*world : nullptr;
        view.selection = &selection;
        view.dropped_ticks = simulation_clock.dropped_ticks();
        view.last_tick_ms = last_tick_ms;
        view.slowest_tick_ms = slowest_tick_ms;
        view.orders_issued = orders_issued;
        if (!spawnable.empty())
        {
            view.spawn_type = spawnable[spawn_choice].type_name;
        }
        view.spawn_types = spawnable.size();
        view.spawn_owner = spawn_owner;
        view.reload_status = reload_status;
        view.camera = !tactical_camera ? eaw::SandboxCameraMode::model : free_camera ? eaw::SandboxCameraMode::free : eaw::SandboxCameraMode::rts;
        renderer.set_overlay_text(eaw::sandbox_overlay_lines(view));
        overlay_frames = 0;
        overlay_refreshed = now;
    }
    // Camera: keyboard panning on maps, mouse orbit/rotation/pan where the camera is free, the wheel.
    void update_camera(const eaw::InputState& input, const bool scene_mouse, const bool rts_mode)
    {
        const auto key_down = [&](const eaw::Key key)
        {
            return input.keys[static_cast<std::size_t>(key)].down;
        };
        const auto camera_now = std::chrono::steady_clock::now();
        const auto camera_seconds = std::min(std::chrono::duration<float>(camera_now - camera_tick).count(), 0.25F);
        camera_tick = camera_now;
        eaw::CameraInput camera_input;
        camera_input.reset = input.keys[static_cast<std::size_t>(eaw::Key::space)].pressed;
        camera_input.zoom_steps = scene_mouse ? input.wheel_delta : 0.0F;
        if (scene_mouse && !rts_mode && input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::left)].down)
        {
            // Rotation follows the device (raw, unaccelerated counts); dragging that should track
            // the cursor, such as panning, uses cursor pixels.
            camera_input.yaw = static_cast<float>(input.raw_mouse_delta_x) * 0.01F;
            camera_input.pitch = static_cast<float>(-input.raw_mouse_delta_y) * 0.01F;
        }
        if (tactical_camera)
        {
            // Pan speed scales with the view distance; panning units are view distances.
            const auto speed = (key_down(eaw::Key::shift) ? 2.4F : 0.8F) * camera_seconds;
            camera_input.pan_right = static_cast<float>((key_down(eaw::Key::d) || key_down(eaw::Key::right) ? 1 : 0) - (key_down(eaw::Key::a) || key_down(eaw::Key::left) ? 1 : 0)) * speed;
            camera_input.pan_forward = static_cast<float>((key_down(eaw::Key::w) || key_down(eaw::Key::up) ? 1 : 0) - (key_down(eaw::Key::s) || key_down(eaw::Key::down) ? 1 : 0)) * speed;
            if (scene_mouse && !rts_mode && input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::right)].down)
            {
                camera_input.pan_right -= static_cast<float>(input.mouse_delta_x) * 0.0015F;
                camera_input.pan_forward += static_cast<float>(input.mouse_delta_y) * 0.0015F;
            }
            tactical_camera->apply(camera_input);
            renderer.set_camera(tactical_camera->camera());
        }
        else
        {
            orbit_camera->apply(camera_input);
            renderer.set_camera(orbit_camera->camera());
        }
    }

    // Left button selection (click picks, RTS drag box-selects) and right-click orders.
    void handle_pointer(const eaw::InputState& input, const bool scene_mouse, const bool rts_mode, const std::array<float, 2>& mouse)
    {
        // A left click that does not drag selects what is under the cursor; dragging orbits or
        // rotates as before.
        const auto& left_button = input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::left)];
        const auto& right_button = input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::right)];
        if (left_button.pressed)
        {
            // A press over the overlay never becomes a scene click.
            click_travel = scene_mouse ? 0 : std::numeric_limits<std::int32_t>::max();
            drag_start = scene_mouse ? std::optional(mouse) : std::nullopt;
            box_active = false;
        }
        if (left_button.down)
        {
            click_travel += std::abs(input.mouse_delta_x) + std::abs(input.mouse_delta_y);
            if (rts_mode && drag_start && click_travel > 4)
            {
                box_active = true;
                renderer.set_selection_rectangle(std::array<float, 4>{(*drag_start)[0], (*drag_start)[1], mouse[0], mouse[1]});
            }
        }
        if (left_button.released)
        {
            if (box_active && drag_start && world)
            {
                // Drag-selection picks mobile units only, like the original's box selection.
                const auto stats = renderer.stats();
                const auto picked = eaw::entities_in_rectangle(world->snapshot().entities, tactical_camera->camera(), static_cast<float>(stats.width), static_cast<float>(stats.height),
                                                               {(*drag_start)[0], (*drag_start)[1], mouse[0], mouse[1]}, true);
                selection.clear();
                for (const auto id : picked)
                {
                    selection.insert(eaw::entity_key(id));
                }
                std::cout << "Box-selected " << selection.size() << " units\n";
            }
            else if (click_travel <= 3 && input.mouse_x >= 0 && input.mouse_y >= 0)
            {
                unbound_picks.push_back(renderer.request_pick(static_cast<std::uint32_t>(input.mouse_x), static_cast<std::uint32_t>(input.mouse_y)));
            }
            renderer.set_selection_rectangle(std::nullopt);
            drag_start.reset();
            box_active = false;
        }
        // Orders and spawns target the ground under a client pixel, found on the CPU in this frame
        // (terrain or liquid surface on land, the z = 0 plane in space), so they never wait for a
        // GPU readback. A right-click also requests a pick: if it lands on a hostile entity, an
        // attack order follows the move a frame or two later.
        // RTS mode: the right button has no drag action, so an order is issued on the press with no
        // movement threshold (a threshold dropped quick clicks made while the mouse was moving).
        if (right_button.pressed && rts_mode && scene_mouse && !selection.empty() && world)
        {
            if (const auto target = ground_target(mouse[0], mouse[1]))
            {
                order_move(*target);
            }
            if (input.mouse_x >= 0 && input.mouse_y >= 0)
            {
                const auto request = renderer.request_pick(static_cast<std::uint32_t>(input.mouse_x), static_cast<std::uint32_t>(input.mouse_y));
                unbound_picks.push_back(request);
                attack_picks.insert(request);
            }
        }
    }

    // Spawn keys (O owner, Tab type, N spawn at the cursor) and C (RTS or free camera).
    void handle_command_keys(const eaw::InputState& input, const bool scene_mouse, const std::array<float, 2>& mouse)
    {
        if (input.keys[static_cast<std::size_t>(eaw::Key::o)].pressed && !spawnable.empty())
        {
            do
            {
                spawn_owner = (spawn_owner + 1U) % 8U;
            } while (spawn_owner == eaw::neutral_map_owner);
            std::cout << "Spawn owner: " << spawn_owner << '\n';
        }
        if (input.keys[static_cast<std::size_t>(eaw::Key::tab)].pressed && !spawnable.empty())
        {
            spawn_choice = (spawn_choice + 1U) % spawnable.size();
            std::cout << "Spawn type: " << spawnable[spawn_choice].type_name << '\n';
        }
        if (input.keys[static_cast<std::size_t>(eaw::Key::n)].pressed && !spawnable.empty() && world && presentation && tactical_camera && scene_mouse)
        {
            if (const auto target = ground_target(mouse[0], mouse[1]))
            {
                spawn_units(spawn_choice, *target, 1);
            }
        }
        if (input.keys[static_cast<std::size_t>(eaw::Key::c)].pressed && tactical_camera)
        {
            free_camera = !free_camera;
            std::cout << (free_camera ? "Free debug camera (drag rotates, right-drag pans)\n" : "RTS camera (drag selects, right-click moves)\n");
        }
    }

    // Completed GPU picks: selections, or attack targets for right-clicks.
    void process_picks()
    {
        while (const auto pick = renderer.take_pick())
        {
            // Pick ids are positions in the instance list of the frame that drew the pick.
            const auto recorded = pick_entities.find(pick->request);
            const auto& entities_then = recorded != pick_entities.end() ? recorded->second : frame_entities;
            const auto entity = pick->instance && *pick->instance < entities_then.size() ? entities_then[*pick->instance] : eaw::EntityId{};
            const bool attack = attack_picks.erase(pick->request) != 0;
            std::erase(unbound_picks, pick->request);
            if (recorded != pick_entities.end())
            {
                pick_entities.erase(recorded);
            }
            if (attack)
            {
                if (entity.valid() && world && world->alive(entity))
                {
                    order_attack(entity);
                }
                continue;
            }
            selection.clear();
            if (entity.valid())
            {
                selection.insert(eaw::entity_key(entity));
                std::cout << "Selected " << (presentation ? presentation->type_name(entity) : std::string{}) << '\n';
            }
            else
            {
                std::cout << "Selected: " << (pick->terrain ? "terrain" : "nothing") << '\n';
            }
        }
    }

    // P pauses the animation, B and G toggle bounds and passability, Left/Right step animations (models).
    void handle_view_keys(const eaw::InputState& input)
    {
        if (window.input().keys[static_cast<std::size_t>(eaw::Key::p)].pressed && visual_animation)
        {
            animation_playing = !animation_playing;
        }
        if (input.keys[static_cast<std::size_t>(eaw::Key::b)].pressed)
        {
            show_all_bounds = !show_all_bounds;
            std::cout << "Bounds overlay " << (show_all_bounds ? "on" : "off") << '\n';
        }
        if (input.keys[static_cast<std::size_t>(eaw::Key::g)].pressed)
        {
            show_passability = !show_passability;
            std::cout << "Passability overlay " << (show_passability ? "on" : "off") << '\n';
        }
        const int animation_step = (input.keys[static_cast<std::size_t>(eaw::Key::right)].pressed ? 1 : 0) - (input.keys[static_cast<std::size_t>(eaw::Key::left)].pressed ? 1 : 0);
        if (visual_model && !tactical_camera && animation_step != 0 && !animation_paths.empty())
        {
            // Choice 0 is the bind pose; reading here is safe because builds only read too, and
            // remounts happen on this thread between builds.
            const auto choices = animation_paths.size() + 1U;
            animation_choice = (animation_choice + (animation_step > 0 ? 1U : choices - 1U)) % choices;
            animation_time = 0.0F;
            visual_animation.reset();
            if (animation_choice == 0)
            {
                std::cout << "Animation: bind pose\n";
                animation_status = "bind pose";
                renderer.set_bone_transforms(bind_palette);
            }
            else
            {
                const auto& path = animation_paths[animation_choice - 1U];
                auto animation = read_animation(vfs, path);
                if (animation)
                {
                    visual_animation = std::move(animation).value();
                    animation_status = path;
                    std::cout << "Animation " << animation_choice << "/" << animation_paths.size() << ": " << path << " | " << visual_animation->playable_frame_count() << " frames, "
                              << visual_animation->duration_seconds() << " seconds\n";
                }
                else
                {
                    std::cerr << "animation warning: " << path << ": " << animation.error().message << "; showing the bind pose\n";
                    renderer.set_bone_transforms(bind_palette);
                }
            }
        }
    }

    // Shows a finished background build.
    void poll_content()
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
            animation_tick = std::chrono::steady_clock::now();
            next_reload_check = animation_tick + std::chrono::seconds(1);
        }
    }

    // Hot reload checks and rebuilds.
    void poll_hot_reload()
    {
        // Hot reload: once a second (and never during a build) a worker compares the scene's
        // dependencies and the MEG archives with their recorded state. Rebuilt archives are
        // remounted here, while no other thread reads the file system, then the scene rebuilds
        // in the background from the cache.
        if (options.hot_reload && scene_dependencies && !pending_content.valid())
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
                    auto replacement = eaw::MegMount::open(std::string(mount->name()), archive);
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
                    pending_content = std::async(std::launch::async, eaw::build_sandbox_content, std::cref(vfs), std::cref(options), &asset_cache, true);
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

    // Model animation and scene time.
    void advance_animation(const std::chrono::steady_clock::time_point now)
    {
        const auto animation_delta = std::chrono::duration<float>(now - animation_tick).count();
        animation_tick = now;
        if (animation_playing)
        {
            animation_time += animation_delta;
        }
        if (visual_model && visual_animation)
        {
            const auto skin_palette = eaw::sample_skin_palette(*visual_model, &*visual_animation, animation_time);
            renderer.set_bone_transforms(skin_palette);
        }
        renderer.set_scene_time(std::chrono::duration<float>(now - scene_start).count());
    }

    // Fixed simulation ticks, then this frame's interpolated instances and debug lines.
    void step_simulation(const std::chrono::steady_clock::time_point now)
    {
        if (world && presentation)
        {
            const auto ticks = simulation_clock.advance(std::chrono::duration<double>(now - simulation_tick).count());
            simulation_tick = now;
            for (std::uint32_t tick = 0; tick < ticks; ++tick)
            {
                previous_snapshot = world->snapshot();
                const auto tick_start = std::chrono::steady_clock::now();
                world->step();
                last_tick_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tick_start).count();
                slowest_tick_ms = std::max(slowest_tick_ms, last_tick_ms);
            }
            const auto entities = eaw::interpolate_snapshots(previous_snapshot, world->snapshot(), simulation_clock.interpolation());
            presentation->build(entities, selection, frame_instances, frame_entities);
            for (const auto request : unbound_picks)
            {
                pick_entities[request] = frame_entities;
            }
            unbound_picks.clear();
            renderer.set_instances(frame_instances);
            update_debug_lines();
        }
    }

    // Command-line automation: initial spawns, --auto-move, and --pick, once the content is shown.
    void run_automation()
    {
        if (!initial_spawns_sent && world && tactical_camera && content_shown_at && !spawnable.empty())
        {
            initial_spawns_sent = true;
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
            spawn_owner = default_owner;
        }
        // Waits for the initial spawns so they are part of the order.
        if (options.auto_move && content_shown_at && world && !automated_move_sent && (spawnable.empty() || (initial_spawns_sent && world->snapshot().entities.size() == world->stats().entities)))
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
            const auto& bounds = tactical_camera ? tactical_camera->focus() : std::array<float, 3>{0.0F, 0.0F, 0.0F};
            std::cout << "Auto-move: " << selection.size() << " mobile units\n";
            order_move({bounds[0], bounds[1], bounds[2]});
        }
        if (options.pick && content_shown_at && !automated_pick_sent)
        {
            unbound_picks.push_back(renderer.request_pick((*options.pick)[0], (*options.pick)[1]));
            automated_pick_sent = true;
        }
    }

    // F1 and the statistics panel refresh.
    void update_panel(const eaw::InputState& input, const std::chrono::steady_clock::time_point now)
    {
        if (input.keys[static_cast<std::size_t>(eaw::Key::f1)].pressed)
        {
            overlay_visible = !overlay_visible;
            if (!overlay_visible)
            {
                renderer.set_overlay_text({});
            }
        }
        // The panel is refreshed four times a second so the numbers stay readable.
        ++overlay_frames;
        if (overlay_visible && now - overlay_refreshed >= std::chrono::milliseconds(250))
        {
            update_overlay(now);
        }
    }

    // Renders, runs the --reload-every test, reports streaming, and applies --frame-limit.
    void present()
    {
        const bool rendered = renderer.render();
        if (rendered && reload_scene && renderer.stats().frame_number % *options.reload_every == 0)
        {
            // Load reset the bone palette; restore the bind pose until animation samples again.
            // The copy was accepted before, so a failure here leaves the identical scene shown.
            if (const auto loaded = renderer.load_scene(*reload_scene); loaded)
            {
                if (!bind_palette.empty())
                {
                    renderer.set_bone_transforms(bind_palette);
                }
                ++scene_reloads;
            }
            else
            {
                eaw::log_warning("scene", eaw::with_context("reloading the scene copy", loaded.error()));
            }
        }
        if (content_shown_at && !textures_streamed && rendered && renderer.stats().pending_texture_uploads == 0)
        {
            textures_streamed = true;
            if (renderer.stats().live_textures != 0)
            {
                std::cout << "Textures resident after " << renderer.stats().frame_number - streaming_since << " frames\n";
            }
        }
        if (options.frame_limit && content_shown_at && renderer.stats().frame_number - *content_shown_at >= *options.frame_limit)
        {
            window.request_close();
        }
        if (!rendered)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    }

    // One iteration of the window loop; false once the window is closing. The steps run in this
    // order every frame.
    bool frame()
    {
        window.poll_events();
        if (window.input().keys[static_cast<std::size_t>(eaw::Key::escape)].pressed)
        {
            window.request_close();
        }
        const auto& input = window.input();
        // The overlay gets the mouse first; while it wants it, the scene ignores the mouse.
        renderer.set_overlay_input({static_cast<float>(input.mouse_x), static_cast<float>(input.mouse_y), input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::left)].down,
                                    input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::right)].down, input.wheel_delta});
        const bool scene_mouse = !renderer.overlay_wants_mouse();
        const bool rts_mode = tactical_camera && !free_camera;
        const std::array<float, 2> mouse{static_cast<float>(input.mouse_x), static_cast<float>(input.mouse_y)};
        update_camera(input, scene_mouse, rts_mode);
        handle_pointer(input, scene_mouse, rts_mode, mouse);
        handle_command_keys(input, scene_mouse, mouse);
        process_picks();
        handle_view_keys(input);
        if (window.close_requested())
        {
            return false;
        }
        if (window.minimized())
        {
            animation_tick = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            return true;
        }

        renderer.resize(window.client_width(), window.client_height());
        poll_content();
        poll_hot_reload();
        const auto now = std::chrono::steady_clock::now();
        advance_animation(now);
        step_simulation(now);
        run_automation();
        update_panel(input, now);
        present();
        return true;
    }

    // Waits for the GPU and prints the run summary.
    int finish()
    {
        renderer.wait_idle();
        while (const auto pick = renderer.take_pick())
        {
            std::cout << "Selected: "
                      << (pick->instance && *pick->instance < frame_entities.size() && frame_entities[*pick->instance].valid() ? describe_instance(*pick->instance)
                          : pick->terrain                                                                                      ? std::string("terrain")
                                                                                                                               : std::string("nothing"))
                      << '\n';
        }
        const auto stats = renderer.stats();
        std::cout << "Rendered frames: " << stats.frame_number << '\n';
        if (options.reload_every)
        {
            std::cout << "Scene reloads: " << scene_reloads << '\n';
        }
        if (world)
        {
            const auto simulation = world->stats();
            std::cout << "Simulation: " << simulation.tick << " ticks, slowest " << eaw::format_fixed(slowest_tick_ms, 2) << " ms | paths " << simulation.path_searches << " planned, "
                      << simulation.partial_paths << " partial, " << simulation.path_expansions << " nodes expanded | blocked moves " << simulation.blocked_moves << '\n';
            std::cout << "Combat: " << simulation.shots_fired << " shots, " << simulation.hits << " hits, " << simulation.kills << " kills, " << simulation.projectiles << " in flight\n";
        }
        std::cout << "Resources after idle: " << stats.live_textures << " textures, " << stats.free_texture_slots << " free and " << stats.pending_texture_slots << " pending texture slots, "
                  << stats.pending_retirements << " pending retirements, " << stats.pending_texture_uploads << " textures still streaming\n";
        return 0;
    }
};

} // namespace

int run_sandbox(const SandboxOptions& options, VirtualFileSystem& vfs, const GameProfile& profile)
{
    eaw::WindowDescription window_description;
    window_description.title = "EAWEngine Visual Sandbox — " + std::string(eaw::to_string(profile.variant));
    window_description.width = options.width;
    window_description.height = options.height;
    window_description.raw_input = options.raw_input;
    auto created_window = eaw::PlatformWindow::create(window_description);
    if (!created_window)
    {
        std::cerr << "error: " << created_window.error().message << '\n';
        return 1;
    }
    auto window = std::move(created_window).value();

    eaw::D3d12RendererOptions renderer_options;
    renderer_options.vsync = options.vsync;
    renderer_options.use_warp = options.use_warp;
    renderer_options.gpu_validation = options.gpu_validation;
    renderer_options.maximum_frame_latency = options.frame_latency;
    renderer_options.pipeline_cache_directory = options.pipeline_cache;
    renderer_options.texture_upload_budget_bytes = options.upload_budget_mib * 1024ULL * 1024ULL;
    auto created_renderer = eaw::D3d12Renderer::create(window.native_handle(), window.client_width(), window.client_height(), renderer_options);
    if (!created_renderer)
    {
        std::cerr << "error: " << created_renderer.error().message << '\n';
        return 1;
    }
    auto& renderer = **created_renderer;
    SandboxSession session(options, vfs, window, renderer);
    while (!window.close_requested())
    {
        if (!session.frame())
        {
            break;
        }
    }
    return session.finish();
}

} // namespace eaw
