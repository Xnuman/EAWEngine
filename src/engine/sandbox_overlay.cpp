#include "sandbox_overlay.hpp"

#include "scene_presentation.hpp"
#include "text_format.hpp"

#include <algorithm>

namespace eaw {

std::vector<std::string> sandbox_overlay_lines(const SandboxOverlayView& view)
{
    const auto& stats = view.stats;
    const auto mebibytes = [](const std::uint64_t bytes)
    {
        return std::to_string(bytes / (1024ULL * 1024ULL));
    };
    const auto used_slots = stats.texture_slot_capacity - std::min<std::size_t>(stats.free_texture_slots + stats.pending_texture_slots, stats.texture_slot_capacity);
    const auto frames = static_cast<double>(view.frames);
    std::vector<std::string> lines{
        "EAWEngine sandbox | " + view.adapter_name,
        std::to_string(stats.width) + "x" + std::to_string(stats.height) + " | " +
            (view.vsync      ? "vsync"
             : stats.tearing ? "immediate (tearing)"
                             : "immediate") +
            " | latency " + std::to_string(view.frame_latency) + " | input " + (view.raw_input ? "raw (" + std::to_string(view.raw_events) + " events last frame)" : std::string("window messages")),
        "",
        "FPS " + format_fixed(frames / view.seconds, 1) + " | CPU " + format_fixed(view.seconds * 1000.0 / frames, 2) + " ms | GPU " + format_fixed(stats.gpu_frame_milliseconds, 2) + " ms",
        "Draw calls " + std::to_string(stats.draw_calls) + " | triangles " + std::to_string(stats.triangles) + " | instances " + std::to_string(stats.instances),
        "Video memory " + mebibytes(stats.video_memory_usage) + " / " + mebibytes(stats.video_memory_budget) + " MiB budget",
        "Textures " + std::to_string(stats.live_textures) + " resident, " + std::to_string(stats.pending_texture_uploads) + " streaming | slots " + std::to_string(used_slots) + "/" +
            std::to_string(stats.texture_slot_capacity) + " | retiring " + std::to_string(stats.pending_retirements),
        "",
        "Scene: " + view.scene_status,
        "Camera: " + format_fixed(view.eye[0], 0) + ", " + format_fixed(view.eye[1], 0) + ", " + format_fixed(view.eye[2], 0),
    };
    if (view.animation_status)
    {
        lines.push_back("Animation: " + *view.animation_status + (view.animation_playing ? "" : " (paused)"));
    }
    auto selected_text = view.selected_text;
    if (view.world != nullptr)
    {
        const auto simulation = view.world->stats();
        lines.push_back("Simulation: tick " + std::to_string(simulation.tick) + " at 30 Hz | " + std::to_string(simulation.entities) + " entities | dropped ticks " +
                        std::to_string(view.dropped_ticks) + " | blocked moves " + std::to_string(simulation.blocked_moves) + " | separating " + std::to_string(simulation.separated_pairs) + " pairs");
        lines.push_back("Paths: " + std::to_string(simulation.path_searches) + " planned | " + std::to_string(simulation.partial_paths) + " partial | " + std::to_string(simulation.path_expansions) +
                        " nodes expanded | tick " + format_fixed(view.last_tick_ms, 2) + " ms, slowest " + format_fixed(view.slowest_tick_ms, 2) + " ms");
        lines.push_back("Combat: " + std::to_string(simulation.shots_fired) + " shots | " + std::to_string(simulation.hits) + " hits | " + std::to_string(simulation.kills) + " kills | " +
                        std::to_string(simulation.projectiles) + " in flight");
        if (view.selection != nullptr && view.selection->size() == 1)
        {
            for (const auto& entity : view.world->snapshot().entities)
            {
                if (view.selection->contains(entity_key(entity.id)) && entity.max_health > 0.0F)
                {
                    selected_text += " | owner " + std::to_string(entity.owner) + " | health " + format_fixed(entity.health, 0) + "/" + format_fixed(entity.max_health, 0) +
                                     (entity.max_shield > 0.0F ? " | shield " + format_fixed(entity.shield, 0) + "/" + format_fixed(entity.max_shield, 0) : std::string{});
                }
            }
        }
    }
    lines.push_back("Selected: " + selected_text + " | orders issued " + std::to_string(view.orders_issued));
    if (view.spawn_type)
    {
        lines.push_back("Spawn: " + *view.spawn_type + " for owner " + std::to_string(view.spawn_owner) + " (N at cursor, Tab next of " + std::to_string(view.spawn_types) + ", O owner)");
    }
    lines.push_back("Hot reload: " + view.reload_status);
    lines.push_back("");
    lines.push_back(view.camera == SandboxCameraMode::model  ? "Click select | B bounds | F1 panel | drag orbit, wheel zoom, P pause, Left/Right animation"
                    : view.camera == SandboxCameraMode::free ? "Free camera: click select | drag rotate, right-drag pan | C RTS camera | B bounds | F1 panel"
                                                             : "RTS: click/drag select, right-click move/attack | WASD pan, wheel zoom | C free camera | B bounds | G passability | F1 panel");
    return lines;
}

} // namespace eaw
