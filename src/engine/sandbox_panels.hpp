#pragma once

// The sandbox's interactive development panels, drawn with Dear ImGui inside the renderer's overlay
// frame (D3d12Renderer::set_overlay_ui): a menu (F1) that shows or hides the other panels and view
// toggles, and a spawn panel that chooses what to spawn, how many, and for which owner. The
// statistics panel itself is drawn by the renderer from text (sandbox_overlay). Panel state is plain
// data owned by the sandbox; button presses come back as actions it applies in its next frame.

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace eaw {

struct SandboxPanels
{
    bool menu{};
    bool statistics{};
    bool spawn{};
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

// The menu: panel visibility and the view toggles it is handed (bounds, passability).
void draw_sandbox_menu(SandboxPanels& panels, bool& show_bounds, bool& show_passability);
[[nodiscard]] SpawnPanelActions draw_spawn_panel(SandboxPanels& panels, const SpawnPanelView& view);

} // namespace eaw
