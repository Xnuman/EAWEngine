#pragma once

// The sandbox's interactive development panels, drawn with Dear ImGui inside the renderer's overlay
// frame (D3d12Renderer::set_overlay_ui): a bar pinned to the top of the window (F1 shows or hides
// it) with menus that show or hide the panels and the view toggles; the statistics panel
// (graphics numbers, sandbox_overlay); the spawn panel (what to spawn, filtered by category and
// faction, how many, for which owner); the sound panel (volumes); the camera panel (fixed,
// debug, or free camera); and the graphics panel (the renderer's switchable features). Every panel first opens in its own place below the bar, so none covers
// another; afterwards it can be moved and resized, and the renderer's settings file keeps where.
// Panel state is plain data owned by the sandbox; button presses come back as actions it applies
// in its next frame.

#include "render/d3d12_renderer.hpp"
#include "assets/spawn_catalog.hpp"
#include "platform/input_bindings.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace eaw::engine_app {

// How the map camera is driven: the legacy RTS view (fixed heading and pitch), the debug view (the
// same camera, rotated by dragging), or a free flying camera.
enum class SandboxCameraMode
{
    fixed,
    debug,
    free,
};

[[nodiscard]] std::string_view to_string(SandboxCameraMode mode) noexcept;

struct SandboxPanels
{
    // The bar at the top (F1).
    bool bar{true};
    bool statistics{};
    bool spawn{};
    bool sound{};
    bool camera{};
    bool graphics{};
    bool controls{};
    // Spawn panel: the search text (case-insensitive substring of type names), and the chosen
    // category and faction filters (0: all).
    std::array<char, 96> filter{};
    int category{};
    int faction{};
};

// What the spawn panel shows and edits: the type list (with each type's kind and factions in
// `types` when known, parallel to `names`), which of them are loaded, the chosen index into it, the
// count and owner N and the buttons use, and a status line (loading, waiting).
struct SpawnPanelView
{
    std::span<const std::string> names;
    std::span<const SpawnTypeInfo> types;
    std::function<bool(const std::string&)> loaded;
    std::size_t* choice{};
    std::uint32_t* count{};
    std::uint32_t* owner{};
    std::uint32_t neutral_owner{};
    std::string status;
    bool can_spawn{};
};

struct SpawnPanelActions
{
    // Spawn `count` of the chosen type at the view center.
    bool spawn_at_view_center{};
};

// What the sound panel edits: the master volume and one volume per category (0 to 1), named by
// `category_names`; a status line (backend, voices).
struct SoundPanelView
{
    float* master{};
    std::span<float> categories;
    std::span<const std::string_view> category_names;
    std::string status;
};

// What the camera panel edits: the mode (fixed and debug need a map), and the free camera's speed
// in world units per second.
struct CameraPanelView
{
    SandboxCameraMode* mode{};
    bool map{};
    float* fly_speed{};
};

// What the graphics panel edits: the renderer's switchable features, its detail-level settings,
// and whether terrain decals (scorches, footprints, trails) are handed to it.
struct GraphicsPanelView
{
    eaw::D3d12GraphicsSettings* graphics{};
    eaw::VisualLodSettings* lod{};
    bool* decals{};
};

// What the controls panel edits: every action's two keys, and which key slot waits for a key.
struct ControlsPanelView
{
    eaw::InputBindings* bindings{};
    std::optional<std::pair<std::size_t, std::size_t>>* rebinding{};
};

// The category a spawnable XML element type is listed under (Infantry, Vehicles, Heroes,
// Structures, Space units, Props, Other).
[[nodiscard]] std::string_view spawn_category(std::string_view kind) noexcept;

// The bar: panel visibility and the view toggles it is handed (bounds, passability).
void draw_sandbox_bar(SandboxPanels& panels, bool& show_bounds, bool& show_passability);
void draw_statistics_panel(SandboxPanels& panels, std::span<const std::string> lines);
[[nodiscard]] SpawnPanelActions draw_spawn_panel(SandboxPanels& panels, const SpawnPanelView& view);
// True when a volume changed.
[[nodiscard]] bool draw_sound_panel(SandboxPanels& panels, const SoundPanelView& view);
// True when the mode changed.
[[nodiscard]] bool draw_camera_panel(SandboxPanels& panels, const CameraPanelView& view);
// True when a setting changed.
[[nodiscard]] bool draw_graphics_panel(SandboxPanels& panels, const GraphicsPanelView& view);
// True when the bindings changed (a key cleared, or all reset to their defaults).
[[nodiscard]] bool draw_controls_panel(SandboxPanels& panels, const ControlsPanelView& view);

} // namespace eaw::engine_app
