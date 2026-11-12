#pragma once

// The sandbox session: its state between frames and the steps of one frame. Private to the EAWEngine
// executable; the member functions are defined by subject in sandbox_app.cpp and the
// sandbox_session_*.cpp files named below.

#include "frame_watchdog.hpp"

#include "assets/asset_cache.hpp"
#include "audio/audio_device.hpp"
#include "battle_audio.hpp"
#include "battle_effects.hpp"
#include "battle.hpp"
#include "battle_scripts.hpp"
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
#include "core/log.hpp"
#include "eaw/platform_window.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eaw {

// Members keep the declaration order the work depends on: the asset cache and archive watch
// outlive the futures that use them.
// The free camera's first speed (world units per second; an engine choice, about a squad's width
// a second at close range) and how its speed changes per wheel step.
inline constexpr float default_fly_speed = 400.0F;
inline constexpr float fly_speed_step = 1.25F;
inline constexpr float fly_fast_factor = 4.0F;
// Free-camera look: radians per raw mouse count.
inline constexpr float fly_look_rate = 0.004F;

struct SandboxSession
{
    const eaw::SandboxOptions& options;
    eaw::VirtualFileSystem& vfs;
    eaw::PlatformWindow& window;
    eaw::D3d12Renderer& renderer;

    std::optional<eaw::VisualModelAsset> visual_model;
    std::optional<eaw::Animation> visual_animation;
    // Model inspection orbits the model; a map uses the tactical camera. The validation
    // triangle, shown while content builds, gets an orbit over its small extent.
    std::optional<eaw::OrbitCamera> orbit_camera{std::in_place, eaw::CameraBounds{{-0.8F, -0.7F, -0.1F}, {0.8F, 0.8F, 0.1F}}};
    std::optional<eaw::TacticalCamera> tactical_camera;
    // Kept only for --reload-every; otherwise the CPU copy moves into the renderer.
    std::optional<eaw::VisualScene> reload_scene;
    std::vector<Matrix4> bind_palette;
    // Runtime-spawnable types of the current map (the battle's); N spawns the chosen name at the
    // cursor, building its package first when it is not loaded yet.
    std::vector<eaw::SpawnableType> spawnable;
    // The map's ground surface for CPU order targets (invalid on space maps and models).
    eaw::GroundMap terrain_heights;
    bool map_land = false;
    // Names Tab cycles through: every unit type with a model for the map (listed in the
    // background once the map is shown), or the loaded spawnable types until then.
    std::vector<std::string> spawn_names;
    std::future<std::vector<eaw::SpawnTypeInfo>> pending_spawn_types;
    // Each spawn name's kind and factions once the full list arrived (parallel to spawn_names;
    // empty before), for the spawn panel's filters.
    std::vector<eaw::SpawnTypeInfo> spawn_types;
    std::size_t spawn_choice = 0;
    // Types added at runtime (kept across hot-reload rebuilds), the package being built, and spawns
    // waiting for their type.
    std::vector<std::string> runtime_types;
    struct PendingSpawn
    {
        std::string type;
        eaw::Vector3 point;
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
    // Picks of Shift + right-clicks: an object under the cursor turns the guard of the point into a
    // guard of the object.
    std::unordered_set<std::uint32_t> guard_picks;
    bool initial_spawns_sent = false;
    // Tick at which the initial spawn orders were given (they apply in the next step).
    std::uint64_t initial_spawn_tick = 0;
    // Every shown object is a simulation entity of the battle; backdrops stay static. Presentation
    // re-poses the scene's instances from interpolated snapshots each frame. `world` is the
    // battle's world (null before the first scene).
    // Declared first among the frame state so it outlives nothing it reports on.
    FrameWatchdog watchdog;
    std::optional<eaw::Battle> battle;
    // Game-object scripts of the battle (none with --no-scripts).
    std::optional<eaw::BattleScripts> battle_scripts;
    eaw::SimulationWorld* world = nullptr;
    std::optional<eaw::ScenePresentation> presentation;
    // Map units' animation (presentation only) and the palette it fills each frame.
    std::optional<eaw::UnitAnimator> animator;
    std::vector<Matrix4> frame_palette;
    // The battle's effects (presentation only: drawn units' proxies, impacts and deaths, bolts), and
    // the --effect effects, which play at the map center and restart when they end.
    eaw::BattleEffects effects{eaw::particle_interpretation(options.particle_fidelity).draw_order};
    std::vector<eaw::VisualParticle> frame_particles;
    std::vector<std::shared_ptr<const eaw::ParticleEffect>> shown_effects;
    std::vector<eaw::ParticleInstanceId> shown_effect_ids;
    Vector3 effect_center{};
    std::uint64_t shown_effect_seed = 1;
    std::chrono::steady_clock::time_point particle_tick = std::chrono::steady_clock::now();
    // Presentation time (presentation_now): the wall clock, or with --fixed-frame-time (and for
    // --capture) a clock advanced by a fixed step each frame.
    std::optional<std::chrono::steady_clock::duration> fixed_frame_step;
    std::chrono::steady_clock::time_point virtual_now = std::chrono::steady_clock::now();
    // --capture: presented frames counted once textures are resident, and whether the capture was
    // requested and written.
    std::uint64_t capture_frames{};
    bool capture_requested{};
    bool capture_written{};
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
    // Shots drawn as models this frame (after the entities in frame_instances) and their serials.
    std::vector<std::uint32_t> frame_projectile_serials;
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
    // The audio device (--audio; the null device when the backend cannot start) and the map's
    // battle sounds, which read the asset cache and so are declared after it.
    std::unique_ptr<eaw::AudioDevice> audio_device;
    std::optional<eaw::BattleAudio> audio;
    // Volumes the sound panel edits (applied to the device in the next frame).
    float master_volume = options.volume;
    std::array<float, eaw::audio_category_count> category_volumes{1.0F, 1.0F, 1.0F, 1.0F};
    bool volumes_changed = false;
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
    // Dev panels under the bar (F1 hides it); statistics starts as --overlay says.
    eaw::SandboxPanels panels{.statistics = options.overlay || options.panels.statistics, .spawn = options.panels.spawn, .sound = options.panels.sound, .camera = options.panels.camera};
    bool statistics_shown = false;
    std::vector<std::string> statistics_lines;
    // How many units N and the spawn panel spawn, and the panel's last button presses.
    std::uint32_t spawn_count = 1;
    eaw::SpawnPanelActions panel_actions;
    std::uint64_t overlay_frames = 0;
    std::chrono::steady_clock::time_point overlay_refreshed = std::chrono::steady_clock::now();
    bool automated_pick_sent = false;
    bool automated_move_sent = false;
    // Maps start in the legacy RTS camera (fixed: no rotation; left-drag box-selects, right-click
    // moves); C or the camera panel switch to the debug camera (the same camera, drag rotates,
    // right-drag pans) and the free flying camera (fly_camera, at fly_speed world units per second).
    eaw::SandboxCameraMode camera_mode = eaw::SandboxCameraMode::fixed;
    std::optional<eaw::FlyCamera> fly_camera;
    float fly_speed = default_fly_speed;
    bool start_camera_applied = false;
    std::optional<Vector2> drag_start;
    bool box_active = false;
    std::uint64_t orders_issued = 0;
    // Simulation step cost (move orders are planned inside the step that applies them).
    double last_tick_ms = 0.0;
    double slowest_tick_ms = 0.0;
    std::uint64_t streaming_since = 0;

    SandboxSession(const SandboxSession&) = delete;
    SandboxSession& operator=(const SandboxSession&) = delete;

    // Session lifetime and the frame loop (sandbox_app.cpp).
    SandboxSession(const eaw::SandboxOptions& session_options, eaw::VirtualFileSystem& session_vfs, eaw::PlatformWindow& session_window, eaw::D3d12Renderer& session_renderer);
    std::string describe_instance(const std::size_t index) const;
    ~SandboxSession();
    // Renders, runs the --reload-every test, reports streaming, and applies --frame-limit.
    void present();
    // One iteration of the window loop; false once the window is closing. The steps run in this
    // order every frame.
    bool frame();
    // Waits for the GPU and prints the run summary.
    int finish();
    // Presentation time now (see virtual_now), and its once-per-frame advance.
    [[nodiscard]] std::chrono::steady_clock::time_point presentation_now() const;
    void advance_presentation_clock();
    // --capture: requests the frame when due, and writes it once the renderer has it.
    void request_due_capture();
    void write_finished_capture();

    // Showing built content, hot reload (sandbox_session_content.cpp).
    void start_simulation(const eaw::VisualScene& scene, const eaw::BattleMap& map, std::string name);
    // Registers particle textures with the renderer and points the effects at them (white when
    // registration fails); returns where each texture landed (empty on failure).
    std::vector<std::uint32_t> register_particle_textures(std::vector<eaw::VisualTexture> textures, std::span<eaw::VisualRig> rigs, std::span<std::shared_ptr<const eaw::ParticleEffect>> named_effects,
                                                          eaw::ParticleEffectObjectTable* objects = nullptr, eaw::ProjectileVisualTable* projectiles = nullptr);
    // The renderer texture of a build's bolt atlas (`atlas` indexes its particle textures, `placed`
    // says where they landed), or invalid_visual_texture.
    static std::uint32_t placed_atlas(std::uint32_t atlas, std::span<const std::uint32_t> placed);
    bool load_content_scene(eaw::VisualScene scene, const bool reload);
    static eaw::VisualScene presentation_layout(const eaw::VisualScene& scene);
    void apply_content(eaw::SandboxContent content);
    // Shows a finished background build.
    void poll_content();
    // Hot reload checks and rebuilds.
    void poll_hot_reload();

    // Cameras, selection, picks, and orders (sandbox_session_input.cpp).
    std::optional<eaw::Vector3> ground_target(const float x, const float y) const;
    // The unit's type answers an order or a selection (its XML SFXEvent_Select/Move/Attack).
    // `unit`'s response to an order; with a group of more than one, a member with a group line
    // (heroes: SFXEvent_Group_Move, _Group_Attack) speaks it instead (an engine choice of who speaks).
    void respond(eaw::EntityId unit, eaw::UnitResponse response, std::span<const eaw::EntityId> group = {});
    void order_move(const eaw::Vector3& target);
    void order_attack(const eaw::EntityId victim);
    // Attack-move (Ctrl + right-click) or guard (Shift + right-click) for the selected mobile units:
    // to a point, or to `target` while it lives.
    void order_standing(eaw::BattleOrder order, const eaw::Vector3& point, eaw::EntityId target = {});
    // Camera: keyboard panning on maps, mouse orbit/rotation/pan where the camera is free, the wheel.
    void update_camera(const eaw::InputState& input, const bool scene_mouse, const bool rts_mode);
    // Left button selection (click picks, RTS drag box-selects) and right-click orders.
    void handle_pointer(const eaw::InputState& input, const bool scene_mouse, const bool rts_mode, const Vector2& mouse);
    // Spawn keys (O owner, Tab type, N spawn at the cursor) and C (RTS or free camera).
    void handle_command_keys(const eaw::InputState& input, const bool scene_mouse, const Vector2& mouse);
    // Completed GPU picks: selections, or attack targets for right-clicks.
    void process_picks();
    // P pauses the animation (models) or the world (maps), B and G toggle bounds and passability,
    // Left/Right step animations (models).
    void handle_view_keys(const eaw::InputState& input);

    // Runtime spawning and command-line automation (sandbox_session_spawning.cpp).
    // Spawns `count` of a spawnable type (companies and squadrons as their members in formation) for
    // the current spawn owner, by order: they appear at the next tick.
    void spawn_units(const std::size_t type_index, const eaw::Vector3& point, const std::uint32_t count);
    // Spawns `count` of a type by name: at once when it is loaded, otherwise after its package is
    // built (on a worker) and registered with the renderer, presentation, animation, and battle.
    void spawn_named(const std::string& type, const eaw::Vector3& point, const std::uint32_t count);
    void start_next_package();
    // A finished package: its meshes join the renderer's library, its types the battle, and the
    // spawns waiting for it are given.
    void poll_packages();
    // Command-line automation: initial spawns, --auto-move, and --pick, once the content is shown.
    void run_automation();

    // The simulation, its presentation, and effects over time (sandbox_session_world.cpp).
    // Presentation time of the map world: frozen while paused (resuming shifts the clocks' origins by
    // the paused span, so nothing jumps).
    [[nodiscard]] std::chrono::steady_clock::time_point world_now(const std::chrono::steady_clock::time_point now) const;
    void toggle_world_pause();
    // Model animation and scene time.
    void advance_animation(const std::chrono::steady_clock::time_point now);
    // Fixed simulation ticks, then this frame's interpolated instances and debug lines.
    void step_simulation(const std::chrono::steady_clock::time_point now);
    // Particle effects advance on the frame clock and are drawn back to front from the camera.
    void step_particles(const std::chrono::steady_clock::time_point now);

    // Dev panels, the statistics panel, and debug lines (sandbox_session_ui.cpp).
    // The dev panels, drawn by the renderer inside its overlay frame; button presses are applied in
    // the next frame (apply_panel_actions).
    void draw_panels();
    // The camera the view uses now (free, tactical, or orbit), and a switch of mode (the free camera
    // starts from the current view).
    [[nodiscard]] eaw::VisualCamera current_camera() const;
    void set_camera_mode(eaw::SandboxCameraMode mode);
    void apply_panel_actions();
    void update_debug_lines();
    // Refreshes the statistics panel (four times a second, so the numbers stay readable).
    void update_overlay(const std::chrono::steady_clock::time_point now);
    // F1 and the statistics panel refresh.
    void update_panel(const eaw::InputState& input, const std::chrono::steady_clock::time_point now);
};

} // namespace eaw
