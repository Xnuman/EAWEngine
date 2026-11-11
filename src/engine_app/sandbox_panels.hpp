#pragma once

// The sandbox's interactive development panels, drawn with Dear ImGui inside the renderer's overlay
// frame (D3d12Renderer::set_overlay_ui): a menu (F1) that shows or hides the other panels and view
// toggles, a spawn panel that chooses what to spawn, how many, and for which owner, and a sound
// panel with the volumes. The
// statistics panel itself is drawn by the renderer from text (sandbox_overlay). Panel state is plain
// data owned by the sandbox; button presses come back as actions it applies in its next frame.

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace eaw {

struct SandboxPanels
{
    bool menu{};
    bool statistics{};
    bool spawn{};
    bool sound{};
    // Spawn panel: the search text (case-insensitive substring of type names).
    std::array<char, 96> filter{};
};

// What the spawn panel shows and edits: the type list, which of them are loaded, the chosen index
// into it, the count and owner N and the buttons use, and a status line (loading, waiting).
struct SpawnPanelView
{
    std::span<const std::string> names;
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

// The menu: panel visibility and the view toggles it is handed (bounds, passability).
void draw_sandbox_menu(SandboxPanels& panels, bool& show_bounds, bool& show_passability);
[[nodiscard]] SpawnPanelActions draw_spawn_panel(SandboxPanels& panels, const SpawnPanelView& view);
// True when a volume changed.
[[nodiscard]] bool draw_sound_panel(SandboxPanels& panels, const SoundPanelView& view);

} // namespace eaw
