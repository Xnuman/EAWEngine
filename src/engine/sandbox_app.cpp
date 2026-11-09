#include "sandbox_app.hpp"

#include "asset_cache.hpp"
#include "attached_effects.hpp"
#include "battle.hpp"
#include "battle_log.hpp"
#include "camera_controller.hpp"
#include "camera_math.hpp"
#include "combat_content.hpp"
#include "particle_effects.hpp"
#include "rts_control.hpp"
#include "sandbox_content.hpp"
#include "sandbox_debug_lines.hpp"
#include "sandbox_overlay.hpp"
#include "sandbox_panels.hpp"
#include "scene_presentation.hpp"
#include "text_format.hpp"
#include "unit_animation.hpp"
#include "visual_scene_builder.hpp"

#include "eaw/d3d12_renderer.hpp"
#include "eaw/log.hpp"
#include "eaw/platform_window.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cctype>
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
// XML type names compare case-insensitively.
std::string fold_name(std::string text)
{
    std::ranges::transform(text, text.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return text;
}

// Reports a frame step that runs longer than its threshold (a stall or an endless loop), naming the
// step and the simulation tick, from a background thread and every five seconds while it lasts, so
// a frozen window still leaves a diagnosis on stderr and in the log file. The threshold is 5 s on a
// hardware device and 60 s on WARP or with GPU validation, where whole frames take seconds.
class FrameWatchdog final
{
public:
    FrameWatchdog()
        : thread_(
              [this]
              {
                  run();
              })
    {}
    ~FrameWatchdog()
    {
        {
            const std::scoped_lock lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }
    FrameWatchdog(const FrameWatchdog&) = delete;
    FrameWatchdog& operator=(const FrameWatchdog&) = delete;

    void set_threshold(const std::chrono::seconds threshold) noexcept
    {
        threshold_seconds_.store(threshold.count(), std::memory_order_relaxed);
    }

    // Marks the start of a frame step; `step` must be a string literal.
    void enter(const char* step, const std::uint64_t tick) noexcept
    {
        tick_.store(tick, std::memory_order_relaxed);
        since_.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
        step_.store(step, std::memory_order_release);
    }

private:
    void run()
    {
        using namespace std::chrono_literals;
        std::unique_lock lock(mutex_);
        std::int64_t reported_since = 0;
        auto last_report = std::chrono::steady_clock::time_point{};
        while (!wake_.wait_for(lock, 500ms,
                               [&]
                               {
                                   return stop_;
                               }))
        {
            const auto* step = step_.load(std::memory_order_acquire);
            const auto since = since_.load(std::memory_order_relaxed);
            if (step == nullptr)
            {
                continue;
            }
            const auto now = std::chrono::steady_clock::now();
            const auto stuck = now - std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(since));
            if (stuck < std::chrono::seconds(threshold_seconds_.load(std::memory_order_relaxed)) || (since == reported_since && now - last_report < 5s))
            {
                continue;
            }
            reported_since = since;
            last_report = now;
            eaw::log_warning("sandbox", eaw::Error{"frame step '" + std::string(step) + "' has run for " + eaw::format_fixed(std::chrono::duration<double>(stuck).count(), 1) + " s (simulation tick " +
                                                   std::to_string(tick_.load(std::memory_order_relaxed)) + ")"});
        }
    }

    std::atomic<const char*> step_{nullptr};
    std::atomic<std::int64_t> since_{0};
    std::atomic<std::uint64_t> tick_{0};
    std::atomic<std::int64_t> threshold_seconds_{5};
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_{false};
    std::thread thread_;
};

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
    // Runtime-spawnable types of the current map (the battle's); N spawns the chosen name at the
    // cursor, building its package first when it is not loaded yet.
    std::vector<eaw::SpawnableType> spawnable;
    // The map's ground surface for CPU order targets (invalid on space maps and models).
    eaw::GroundMap terrain_heights;
    bool map_land = false;
    // Names Tab cycles through: every unit type with a model for the map (listed in the
    // background once the map is shown), or the loaded spawnable types until then.
    std::vector<std::string> spawn_names;
    std::future<std::vector<std::string>> pending_spawn_names;
    std::size_t spawn_choice = 0;
    // Types added at runtime (kept across hot-reload rebuilds), the package being built, and spawns
    // waiting for their type.
    std::vector<std::string> runtime_types;
    struct PendingSpawn
    {
        std::string type;
        eaw::SimVector3 point;
        std::uint32_t count{};
        std::uint32_t owner{};
    };
    std::vector<PendingSpawn> pending_spawns;
    std::string building_type;
    std::future<eaw::Result<eaw::SpawnablePackage>> pending_package;
    // Owner slot of debug spawns (O cycles through the non-neutral slots 0..7).
    std::uint32_t spawn_owner = 1;
    // Pick requests: the instance-to-entity list of the frame that drew each one (instance order
    // changes whenever an entity dies), requests still waiting for that frame, and which
    // requests are right-click attack orders rather than selections.
    std::unordered_map<std::uint32_t, std::vector<eaw::EntityId>> pick_entities;
    std::vector<std::uint32_t> unbound_picks;
    std::unordered_set<std::uint32_t> attack_picks;
    bool initial_spawns_sent = false;
    // Tick at which the initial spawn orders were given (they apply in the next step).
    std::uint64_t initial_spawn_tick = 0;
    // Every shown object is a simulation entity of the battle; backdrops stay static. Presentation
    // re-poses the scene's instances from interpolated snapshots each frame. `world` is the
    // battle's world (null before the first scene).
    // Declared first among the frame state so it outlives nothing it reports on.
    FrameWatchdog watchdog;
    std::optional<eaw::Battle> battle;
    eaw::SimulationWorld* world = nullptr;
    std::optional<eaw::ScenePresentation> presentation;
    // Map units' animation (presentation only) and the palette it fills each frame.
    std::optional<eaw::UnitAnimator> animator;
    std::vector<std::array<float, 16>> frame_palette;
    // Particle effects (presentation only): the proxies of drawn units and the --effect effects,
    // which play at the map center and restart when they end.
    eaw::ParticleWorld particles;
    eaw::AttachedEffects attached_effects;
    std::vector<eaw::VisualParticle> frame_particles;
    std::vector<std::shared_ptr<const eaw::ParticleEffect>> shown_effects;
    std::vector<eaw::ParticleInstanceId> shown_effect_ids;
    std::array<float, 3> effect_center{};
    std::uint64_t effect_seed = 1;
    std::chrono::steady_clock::time_point particle_tick = std::chrono::steady_clock::now();
    // How shots look (by projectile visual key) and the bolt atlas's renderer texture.
    eaw::ProjectileVisualTable projectile_visuals;
    std::uint32_t projectile_atlas = eaw::invalid_visual_texture;
    // The XML Particle objects played one-shot where shots end and units die.
    eaw::ParticleEffectObjectTable effect_objects;
    // Kept for --reload-every, which reloads the scene and so drops added textures.
    std::vector<eaw::VisualTexture> particle_texture_copy;
    std::chrono::steady_clock::time_point map_animation_start = std::chrono::steady_clock::now();
    // P on maps freezes the world (simulation ticks, unit animation, particles, texture scrolling)
    // while the cameras keep working; orders given meanwhile apply after it resumes.
    bool world_paused = false;
    std::chrono::steady_clock::time_point paused_since;
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
    // Dev panels (F1 opens their menu); statistics starts as --overlay says.
    eaw::SandboxPanels panels{.menu = options.panels.menu, .statistics = options.overlay || options.panels.statistics, .spawn = options.panels.spawn};
    bool statistics_shown = false;
    // How many units N and the spawn panel spawn, and the panel's last button presses.
    std::uint32_t spawn_count = 1;
    eaw::SpawnPanelActions panel_actions;
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
        watchdog.set_threshold(options.use_warp || options.gpu_validation ? std::chrono::seconds(60) : std::chrono::seconds(5));
        particles.set_draw_order(eaw::particle_interpretation(options.particle_fidelity).draw_order);
        renderer.set_overlay_ui(
            [this]
            {
                draw_panels();
            });
        if (!options.validation_triangle)
        {
            pending_content = std::async(std::launch::async, eaw::build_sandbox_content, std::cref(vfs), std::cref(options), &asset_cache, false, std::vector<std::string>{});
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
        const auto type = frame_entities[index].valid() && world != nullptr ? std::string(world->type_name(frame_entities[index])) : std::string{};
        const auto& mesh = presentation->mesh_name(instance.mesh);
        return type.empty() || type == mesh ? mesh : type + " (" + mesh + ")";
    }

    void start_simulation(const eaw::VisualScene& scene, const eaw::BattleMap& map, std::string name)
    {
        battle.emplace(map, std::move(name), simulation_clock.tick_seconds());
        world = &battle->world();
        presentation.emplace(scene, map.entities, battle->map_entity_ids(), battle->spawnable());
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

    // Registers particle textures with the renderer and points the effects at them (white when
    // registration fails); returns where each texture landed (empty on failure).
    std::vector<std::uint32_t> register_particle_textures(std::vector<eaw::VisualTexture> textures, std::span<eaw::VisualRig> rigs, std::span<std::shared_ptr<const eaw::ParticleEffect>> effects,
                                                          eaw::ParticleEffectObjectTable* objects = nullptr)
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
        eaw::renumber_effect_textures(rigs, effects, placed, objects);
        return placed;
    }

    // Takes over a build's projectile visuals; `atlas` indexes its particle textures (`placed`).
    void add_projectile_visuals(const eaw::ProjectileVisualTable& visuals, const std::uint32_t atlas, std::span<const std::uint32_t> placed)
    {
        projectile_visuals.insert(visuals.begin(), visuals.end());
        if (projectile_atlas == eaw::invalid_visual_texture && atlas < placed.size())
        {
            projectile_atlas = placed[atlas];
        }
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
            if (options.reload_every)
            {
                particle_texture_copy = visual_map.particle_textures;
            }
            {
                const auto placed = register_particle_textures(std::move(visual_map.particle_textures), visual_map.rigs, visual_map.effects, &visual_map.effect_objects);
                projectile_visuals.clear();
                projectile_atlas = eaw::invalid_visual_texture;
                add_projectile_visuals(visual_map.projectile_visuals, visual_map.projectile_atlas, placed);
                effect_objects = std::move(visual_map.effect_objects);
            }
            start_simulation(layout, eaw::battle_map(visual_map), *options.map);
            animator.emplace(visual_map.rigs, visual_map.rest_palette);
            attached_effects.clear(particles);
            particles.clear();
            attached_effects.set_rigs(visual_map.rigs);
            shown_effects = visual_map.effects;
            shown_effect_ids.assign(shown_effects.size(), {});
            effect_center = {(bounds.minimum[0] + bounds.maximum[0]) * 0.5F, (bounds.minimum[1] + bounds.maximum[1]) * 0.5F, 0.0F};
            if (visual_map.land && visual_map.ground.valid())
            {
                effect_center[2] = visual_map.ground.surface_height(effect_center[0], effect_center[1]);
                particles.set_ground(
                    [this](const float x, const float y) -> std::optional<float>
                    {
                        return terrain_heights.valid() ? std::optional<float>(terrain_heights.surface_height(x, y)) : std::nullopt;
                    });
            }
            else
            {
                particles.set_ground({});
            }
            for (std::size_t index = 0; index < shown_effects.size(); ++index)
            {
                std::cout << "Effect: " << options.effects[index] << (shown_effects[index] ? " (" + std::to_string(shown_effects[index]->emitters.size()) + " emitters)" : " not loaded") << '\n';
            }
            map_animation_start = std::chrono::steady_clock::now();
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
                const auto playable = std::ranges::count_if(effect_objects,
                                                            [](const auto& entry)
                                                            {
                                                                return entry.second.effect != nullptr;
                                                            });
                std::cout << "Impact and death effects: " << playable << " of " << effect_objects.size() << " particle objects playable\n";
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
            if (!pending_spawn_names.valid())
            {
                pending_spawn_names = std::async(std::launch::async,
                                                 [&files = vfs, land = map_land, cache = &asset_cache]
                                                 {
                                                     return eaw::spawnable_type_names(files, land, cache);
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
                orbit_camera.reset();
                renderer.set_camera(tactical_camera->camera());
                std::cout << "Tactical camera: WASD/arrows or right-drag pan (Shift is faster), left-drag rotates, wheel zooms, Space resets, C toggles the free camera, P pauses the world.\n";
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
                spawn.heading = eaw::model_forward_heading(matrix);
                spawn.visual = scene.instances.front().mesh;
                model_entity.push_back({std::move(spawn), 0});
            }
            const auto layout = presentation_layout(scene);
            if (!load_content_scene(std::exchange(scene, {}), content.reload))
            {
                return;
            }
            visual_model = std::move(content.model);
            animator.reset();
            attached_effects.clear(particles);
            particles.clear();
            attached_effects.set_rigs({});
            shown_effects.clear();
            shown_effect_ids.clear();
            spawn_names.clear();
            pending_spawns.clear();
            scene_status = model_status;
            animation_status = "bind pose";
            reload_scene = std::move(reload_copy);
            eaw::BattleMap model_map;
            model_map.entities = model_entity;
            start_simulation(layout, model_map, options.model);
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
        if (movers.empty())
        {
            return;
        }
        std::ranges::sort(movers, {}, eaw::entity_key);
        const auto count = movers.size();
        if (const auto submitted = battle->submit({.order = eaw::BattleOrder::move, .entities = std::move(movers), .point = target}); !submitted)
        {
            eaw::log_warning("simulation", submitted.error());
            return;
        }
        ++orders_issued;
        std::cout << "Move " << count << " units to " << eaw::format_fixed(target.x, 0) << ", " << eaw::format_fixed(target.y, 0) << ", " << eaw::format_fixed(target.z, 0) << '\n';
    }

    // Spawns `count` of a spawnable type (companies and squadrons as their members in formation) for
    // the current spawn owner, by order: they appear at the next tick.
    void spawn_units(const std::size_t type_index, const eaw::SimVector3& point, const std::uint32_t count)
    {
        const auto& type = spawnable[type_index];
        if (const auto submitted = battle->submit({.order = eaw::BattleOrder::spawn, .point = point, .type = type.type_name, .count = count, .owner = spawn_owner}); !submitted)
        {
            eaw::log_warning("simulation", submitted.error());
            return;
        }
        std::cout << "Spawning " << count << " " << type.type_name << (type.members.empty() ? std::string{} : " (" + std::to_string(type.members.size()) + " units each)") << " for owner "
                  << spawn_owner << " at " << eaw::format_fixed(point.x, 0) << ", " << eaw::format_fixed(point.y, 0) << ", " << eaw::format_fixed(point.z, 0) << '\n';
    }

    ~SandboxSession()
    {
        renderer.set_overlay_ui({});
    }
    SandboxSession(const SandboxSession&) = delete;
    SandboxSession& operator=(const SandboxSession&) = delete;

    // The dev panels, drawn by the renderer inside its overlay frame; button presses are applied in
    // the next frame (apply_panel_actions).
    void draw_panels()
    {
        eaw::draw_sandbox_menu(panels, show_all_bounds, show_passability);
        eaw::SpawnPanelView view;
        view.names = spawn_names;
        view.loaded = [this](const std::string& name)
        {
            return std::ranges::any_of(spawnable,
                                       [&](const eaw::SpawnableType& type)
                                       {
                                           return fold_name(type.type_name) == fold_name(name);
                                       });
        };
        view.choice = &spawn_choice;
        view.count = &spawn_count;
        view.owner = &spawn_owner;
        view.neutral_owner = eaw::neutral_map_owner;
        view.can_spawn = world != nullptr && tactical_camera.has_value() && !spawn_names.empty();
        if (pending_package.valid())
        {
            view.status = "Loading " + building_type + "...";
        }
        else if (!pending_spawns.empty())
        {
            view.status = std::to_string(pending_spawns.size()) + " spawns waiting";
        }
        const auto actions = eaw::draw_spawn_panel(panels, view);
        panel_actions.spawn_at_view_center = panel_actions.spawn_at_view_center || actions.spawn_at_view_center;
    }

    void apply_panel_actions()
    {
        if (std::exchange(panel_actions.spawn_at_view_center, false) && spawn_choice < spawn_names.size() && world && tactical_camera)
        {
            const auto stats = renderer.stats();
            if (const auto center = ground_target(static_cast<float>(stats.width) * 0.5F, static_cast<float>(stats.height) * 0.5F))
            {
                spawn_named(spawn_names[spawn_choice], *center, spawn_count);
            }
        }
    }

    // Spawns `count` of a type by name: at once when it is loaded, otherwise after its package is
    // built (on a worker) and registered with the renderer, presentation, animation, and battle.
    void spawn_named(const std::string& type, const eaw::SimVector3& point, const std::uint32_t count)
    {
        const auto loaded = std::ranges::find_if(spawnable,
                                                 [&](const eaw::SpawnableType& candidate)
                                                 {
                                                     return fold_name(candidate.type_name) == fold_name(type);
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

    void start_next_package()
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

    // A finished package: its meshes join the renderer's library, its types the battle, and the
    // spawns waiting for it are given.
    void poll_packages()
    {
        if (pending_spawn_names.valid() && pending_spawn_names.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            auto names = pending_spawn_names.get();
            if (!names.empty() && battle)
            {
                const auto current = spawn_choice < spawn_names.size() ? spawn_names[spawn_choice] : std::string{};
                spawn_names = std::move(names);
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
            return fold_name(spawn.type) == fold_name(type);
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
        presentation->add_meshes(meshes);
        presentation->add_spawnable(package.types);
        {
            const auto placed = register_particle_textures(std::move(package.particle_textures), package.rigs, {}, &package.effect_objects);
            add_projectile_visuals(package.projectile_visuals, package.projectile_atlas, placed);
            effect_objects.insert(package.effect_objects.begin(), package.effect_objects.end());
        }
        animator->add_rigs(package.rigs, package.rest_palette);
        attached_effects.add_rigs(package.rigs);
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
        if (const auto submitted = battle->submit({.order = eaw::BattleOrder::attack, .entities = std::move(attackers), .target = victim}); !submitted)
        {
            eaw::log_warning("simulation", submitted.error());
            return;
        }
        ++orders_issued;
        std::cout << "Attack " << world->type_name(victim) << " (owner " << found->owner << ")\n";
    }

    void update_debug_lines()
    {
        eaw::SandboxDebugView view;
        view.presentation = presentation ? &*presentation : nullptr;
        view.world = world;
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
        view.projectile_visuals = projectile_atlas != eaw::invalid_visual_texture ? &projectile_visuals : nullptr;
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
        const auto particle_stats = particles.stats();
        view.paused = world_paused;
        view.particle_effects = particle_stats.instances;
        view.particles = particle_stats.particles;
        view.attached_effects = attached_effects.playing();
        view.dropped_particles = particle_stats.dropped_particles;
        view.skipped_emitters = particle_stats.skipped_emitters;
        view.frames = overlay_frames;
        view.seconds = std::chrono::duration<double>(now - overlay_refreshed).count();
        view.scene_status = scene_status;
        view.eye = tactical_camera ? tactical_camera->camera().eye : orbit_camera->camera().eye;
        if (visual_model)
        {
            view.animation_status = animation_status;
        }
        else if (animator)
        {
            const auto& animation = animator->stats();
            view.animation_status = std::to_string(animation.animated) + " units animated (" + std::to_string(animation.blending) + " blending), " + std::to_string(animation.corpses) + " corpses, " +
                                    std::to_string(animation.poses) + " poses (" + std::to_string(animation.sampled) + " sampled this frame), palette " + std::to_string(animation.palette_entries) +
                                    (animation.over_budget > 0 ? ", " + std::to_string(animation.over_budget) + " in rest pose (palette full)" : std::string{});
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
        view.world = world;
        view.selection = &selection;
        view.dropped_ticks = simulation_clock.dropped_ticks();
        view.last_tick_ms = last_tick_ms;
        view.slowest_tick_ms = slowest_tick_ms;
        view.orders_issued = orders_issued;
        if (spawn_choice < spawn_names.size())
        {
            view.spawn_type = spawn_names[spawn_choice] + (building_type.empty() || !pending_package.valid() ? std::string{} : " (loading " + building_type + ")");
        }
        view.spawn_types = spawn_names.size();
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
        if (input.keys[static_cast<std::size_t>(eaw::Key::o)].pressed && !spawn_names.empty())
        {
            do
            {
                spawn_owner = (spawn_owner + 1U) % 8U;
            } while (spawn_owner == eaw::neutral_map_owner);
            std::cout << "Spawn owner: " << spawn_owner << '\n';
        }
        if (input.keys[static_cast<std::size_t>(eaw::Key::tab)].pressed && !spawn_names.empty())
        {
            // Tab next, Shift+Tab previous, with Ctrl ten at a time.
            const auto size = spawn_names.size();
            const auto step = (input.keys[static_cast<std::size_t>(eaw::Key::control)].down ? 10U : 1U) % size;
            spawn_choice = input.keys[static_cast<std::size_t>(eaw::Key::shift)].down ? (spawn_choice + size - step) % size : (spawn_choice + step) % size;
            const bool loaded = std::ranges::any_of(spawnable,
                                                    [&](const eaw::SpawnableType& type)
                                                    {
                                                        return fold_name(type.type_name) == fold_name(spawn_names[spawn_choice]);
                                                    });
            std::cout << "Spawn type: " << spawn_names[spawn_choice] << (loaded ? "" : " (loads on first spawn)") << " [" << spawn_choice + 1U << "/" << size << "]\n";
        }
        if (input.keys[static_cast<std::size_t>(eaw::Key::n)].pressed && spawn_choice < spawn_names.size() && world && presentation && tactical_camera && scene_mouse)
        {
            if (const auto target = ground_target(mouse[0], mouse[1]))
            {
                spawn_named(spawn_names[spawn_choice], *target, spawn_count);
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
                std::cout << "Selected " << (world != nullptr ? world->type_name(entity) : std::string_view{}) << '\n';
            }
            else
            {
                std::cout << "Selected: " << (pick->terrain ? "terrain" : "nothing") << '\n';
            }
        }
    }

    // Presentation time of the map world: frozen while paused (resuming shifts the clocks' origins by
    // the paused span, so nothing jumps).
    [[nodiscard]] std::chrono::steady_clock::time_point world_now(const std::chrono::steady_clock::time_point now) const
    {
        return world_paused ? paused_since : now;
    }

    void toggle_world_pause()
    {
        const auto now = std::chrono::steady_clock::now();
        if (!world_paused)
        {
            world_paused = true;
            paused_since = now;
        }
        else
        {
            const auto span = now - paused_since;
            map_animation_start += span;
            scene_start += span;
            world_paused = false;
        }
        std::cout << (world_paused ? "World paused" : "World resumed") << '\n';
    }

    // P pauses the animation (models) or the world (maps), B and G toggle bounds and passability,
    // Left/Right step animations (models).
    void handle_view_keys(const eaw::InputState& input)
    {
        if (input.keys[static_cast<std::size_t>(eaw::Key::p)].pressed)
        {
            if (tactical_camera && world)
            {
                toggle_world_pause();
            }
            else if (visual_animation)
            {
                animation_playing = !animation_playing;
            }
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
        // Archives are remounted only while no worker reads through the file system.
        if (options.hot_reload && scene_dependencies && !pending_content.valid() && !pending_package.valid() && !pending_spawn_names.valid())
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
        renderer.set_scene_time(std::chrono::duration<float>(world_now(now) - scene_start).count());
    }

    // Fixed simulation ticks, then this frame's interpolated instances and debug lines.
    void step_simulation(const std::chrono::steady_clock::time_point now)
    {
        if (world && presentation)
        {
            const auto ticks = world_paused ? 0U : simulation_clock.advance(std::chrono::duration<double>(now - simulation_tick).count());
            simulation_tick = now;
            const auto world_seconds = std::chrono::duration<double>(world_now(now) - map_animation_start).count();
            for (std::uint32_t tick = 0; tick < ticks; ++tick)
            {
                previous_snapshot = world->snapshot();
                watchdog.enter("simulation tick", world->stats().tick);
                const auto tick_start = std::chrono::steady_clock::now();
                world->step();
                last_tick_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tick_start).count();
                slowest_tick_ms = std::max(slowest_tick_ms, last_tick_ms);
                // Every tick's deaths, so none is lost when a frame runs several ticks.
                for (const auto& death : world->snapshot().deaths)
                {
                    if (const auto corpse = presentation->corpse_instance(death); corpse && animator)
                    {
                        animator->add_corpse(corpse->instance, death.id, world_seconds, corpse->style);
                    }
                    play_effect_object(death.death_effect, death.position);
                }
                for (const auto& impact : world->snapshot().impacts)
                {
                    if (const auto found = projectile_visuals.find(impact.visual); found != projectile_visuals.end())
                    {
                        play_effect_object(eaw::impact_effect(found->second, impact.kind), impact.position);
                    }
                }
            }
            const auto entities = eaw::interpolate_snapshots(previous_snapshot, world->snapshot(), simulation_clock.interpolation());
            watchdog.enter("presentation", world->stats().tick);
            presentation->build(entities, selection, frame_instances, frame_entities);
            if (animator)
            {
                watchdog.enter("unit animation", world->stats().tick);
                // The palette goes first: set_instances checks bone ranges against it.
                animator->animate(entities, frame_entities, frame_instances, world_seconds, eaw::D3d12Renderer::max_bone_transforms(), frame_palette);
                renderer.set_bone_transforms(frame_palette);
                attached_effects.update(particles, frame_instances, frame_entities, frame_palette, entities, world_seconds);
            }
            for (const auto request : unbound_picks)
            {
                pick_entities[request] = frame_entities;
            }
            unbound_picks.clear();
            renderer.set_instances(frame_instances);
            update_debug_lines();
        }
    }

    // Plays an XML Particle object once at `position`, upright (the effect's own axes; an engine
    // choice until the original's orientation of impacts and explosions is known).
    void play_effect_object(const std::uint32_t key, const eaw::SimVector3& position)
    {
        const auto found = effect_objects.find(key);
        if (key == 0 || found == effect_objects.end() || !found->second.effect)
        {
            return;
        }
        auto transform = eaw::visual_identity_transform;
        transform[12] = position.x;
        transform[13] = position.y;
        transform[14] = position.z;
        (void)particles.play(found->second.effect, transform, effect_seed++, {}, found->second.scale);
    }

    // Particle effects advance on the frame clock and are drawn back to front from the camera.
    void step_particles(const std::chrono::steady_clock::time_point now)
    {
        const auto seconds = world_paused ? 0.0F : std::chrono::duration<float>(now - particle_tick).count();
        particle_tick = now;
        for (std::size_t index = 0; index < shown_effects.size(); ++index)
        {
            if (shown_effects[index] && !particles.playing(shown_effect_ids[index]))
            {
                auto transform = eaw::visual_identity_transform;
                transform[12] = effect_center[0];
                transform[13] = effect_center[1];
                transform[14] = effect_center[2];
                shown_effect_ids[index] = particles.play(shown_effects[index], transform, effect_seed++);
            }
        }
        particles.update(seconds);
        frame_particles.clear();
        const auto eye = tactical_camera ? tactical_camera->camera().eye : orbit_camera ? orbit_camera->camera().eye : std::array<float, 3>{};
        particles.build_sprites(eye, frame_particles);
        // Shots in flight as bolts (additive, so their order among the particles does not matter).
        if (world != nullptr && projectile_atlas != eaw::invalid_visual_texture)
        {
            eaw::append_projectile_sprites(world->snapshot().projectiles, projectile_visuals, projectile_atlas, static_cast<float>(simulation_clock.interpolation()),
                                           static_cast<float>(simulation_clock.tick_seconds()), frame_particles);
        }
        renderer.set_particles(frame_particles);
    }

    // Command-line automation: initial spawns, --auto-move, and --pick, once the content is shown.
    void run_automation()
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
            panels.menu = !panels.menu;
        }
        if (!panels.statistics && statistics_shown)
        {
            renderer.set_overlay_text({});
        }
        // Shown now: refresh at once instead of waiting for the next quarter second.
        if (panels.statistics && !statistics_shown)
        {
            overlay_refreshed = now - std::chrono::milliseconds(250);
        }
        statistics_shown = panels.statistics;
        // The panel is refreshed four times a second so the numbers stay readable.
        ++overlay_frames;
        if (panels.statistics && now - overlay_refreshed >= std::chrono::milliseconds(250))
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
                // The same textures in the same order land at the same indices again.
                if (!particle_texture_copy.empty())
                {
                    if (const auto added = renderer.add_textures(particle_texture_copy); !added)
                    {
                        eaw::log_warning("particles", added.error());
                    }
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
        if (window.input().keys[static_cast<std::size_t>(eaw::Key::escape)].pressed && !renderer.overlay_wants_keyboard())
        {
            window.request_close();
        }
        const auto& window_input = window.input();
        const auto down = [&](const eaw::Key key)
        {
            return window_input.keys[static_cast<std::size_t>(key)].down;
        };
        // The overlay gets the mouse and keyboard first; while it wants the mouse the scene ignores
        // the mouse, and while one of its text fields is active the scene gets no keys.
        eaw::VisualOverlayInput overlay_input{static_cast<float>(window_input.mouse_x), static_cast<float>(window_input.mouse_y),
                                              window_input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::left)].down,
                                              window_input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::right)].down, window_input.wheel_delta};
        overlay_input.text = window_input.text;
        overlay_input.backspace = down(eaw::Key::backspace);
        overlay_input.del = down(eaw::Key::del);
        overlay_input.enter = down(eaw::Key::enter);
        overlay_input.escape = down(eaw::Key::escape);
        overlay_input.tab = down(eaw::Key::tab);
        overlay_input.left = down(eaw::Key::left);
        overlay_input.right = down(eaw::Key::right);
        overlay_input.up = down(eaw::Key::up);
        overlay_input.down = down(eaw::Key::down);
        overlay_input.home = down(eaw::Key::home);
        overlay_input.end = down(eaw::Key::end);
        overlay_input.control = down(eaw::Key::control);
        overlay_input.shift = down(eaw::Key::shift);
        renderer.set_overlay_input(overlay_input);
        auto scene_input = window_input;
        if (renderer.overlay_wants_keyboard())
        {
            for (auto& key : scene_input.keys)
            {
                key = {};
            }
        }
        const auto& input = scene_input;
        const bool scene_mouse = !renderer.overlay_wants_mouse();
        const bool rts_mode = tactical_camera && !free_camera;
        const std::array<float, 2> mouse{static_cast<float>(input.mouse_x), static_cast<float>(input.mouse_y)};
        const auto tick = world != nullptr ? world->stats().tick : 0U;
        watchdog.enter("camera", tick);
        update_camera(input, scene_mouse, rts_mode);
        watchdog.enter("pointer and orders", tick);
        handle_pointer(input, scene_mouse, rts_mode, mouse);
        handle_command_keys(input, scene_mouse, mouse);
        apply_panel_actions();
        watchdog.enter("picks", tick);
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

        watchdog.enter("content", tick);
        renderer.resize(window.client_width(), window.client_height());
        poll_content();
        poll_hot_reload();
        poll_packages();
        const auto now = std::chrono::steady_clock::now();
        advance_animation(now);
        watchdog.enter("simulation and presentation", tick);
        step_simulation(now);
        watchdog.enter("particles", tick);
        step_particles(now);
        watchdog.enter("automation and panel", tick);
        run_automation();
        update_panel(input, now);
        watchdog.enter("render", tick);
        present();
        watchdog.enter("events", tick);
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
        const auto particle_stats = particles.stats();
        std::cout << "Particles at exit: " << particle_stats.particles << " in " << particle_stats.instances << " effects (" << attached_effects.playing() << " attached), " << stats.particles
                  << " drawn in the last frame, " << particle_stats.dropped_particles << " dropped, " << particle_stats.skipped_emitters << " emitters without a pass\n";
        if (options.reload_every)
        {
            std::cout << "Scene reloads: " << scene_reloads << '\n';
        }
        if (world)
        {
            const auto simulation = world->stats();
            std::cout << "Simulation: " << simulation.tick << " ticks, slowest " << eaw::format_fixed(slowest_tick_ms, 2) << " ms | paths " << simulation.path_searches << " planned, "
                      << simulation.partial_paths << " partial, " << simulation.shared_routes << " shared, " << simulation.path_expansions << " nodes expanded | blocked moves "
                      << simulation.blocked_moves << '\n';
            std::cout << "Combat: " << simulation.shots_fired << " shots, " << simulation.hits << " hits, " << simulation.kills << " kills, " << simulation.hardpoints_destroyed
                      << " hardpoints destroyed, " << simulation.projectiles << " in flight\n";
        }
        if (animator)
        {
            const auto& animation = animator->stats();
            std::cout << "Animation (last frame): " << animation.animated << " units animated, " << animation.blending << " blending, " << animation.corpses << " corpses, " << animation.poses
                      << " poses, palette " << animation.palette_entries << " entries, " << animation.over_budget << " in rest pose (palette full)\n";
        }
        if (options.record && battle)
        {
            // The current battle's orders (a hot reload or scene replacement starts a new battle).
            auto log = battle->log();
            log.end_tick = world->stats().tick;
            log.end_hash = world->state_hash();
            if (const auto saved = eaw::save_battle_log(*options.record, log); !saved)
            {
                eaw::log_warning("battle", saved.error());
            }
            else
            {
                std::cout << "Recorded " << log.entries.size() << " orders to " << options.record->string() << " (replay with: EAWEngine battle --replay <file>)\n";
            }
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
    renderer_options.bloom = options.bloom_strength > 0.0F;
    renderer_options.bloom_strength = options.bloom_strength;
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
