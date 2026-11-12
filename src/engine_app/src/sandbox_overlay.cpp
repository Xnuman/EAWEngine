#include "engine_app/sandbox_overlay.hpp"

#include "sandbox/text_format.hpp"

#include <algorithm>

namespace eaw::engine_app {

std::vector<std::string> sandbox_overlay_lines(const SandboxOverlayView& view)
{
    const auto& stats = view.stats;
    const auto mebibytes = [](const std::uint64_t bytes)
    {
        return std::to_string(bytes / (1024ULL * 1024ULL));
    };
    const auto used_slots = stats.texture_slot_capacity - std::min<std::size_t>(stats.free_texture_slots + stats.pending_texture_slots, stats.texture_slot_capacity);
    const auto frames = static_cast<double>(view.frames);
    return {
        view.adapter_name,
        std::to_string(stats.width) + "x" + std::to_string(stats.height) + " | " +
            (view.vsync      ? "vsync"
             : stats.tearing ? "immediate (tearing)"
                             : "immediate") +
            " | latency " + std::to_string(view.frame_latency),
        "",
        "FPS " + format_fixed(frames / view.seconds, 1) + " | CPU " + format_fixed(view.seconds * 1000.0 / frames, 2) + " ms | GPU " + format_fixed(stats.gpu_frame_milliseconds, 2) + " ms",
        "Draw calls " + std::to_string(stats.draw_calls) + " | triangles " + std::to_string(stats.triangles) + " | instances " + std::to_string(stats.instances),
        "Detail levels: " + std::to_string(stats.reduced_detail_instances) + " reduced | " + std::to_string(stats.fading_instances) + " fading",
        "Bone palette " + std::to_string(stats.bone_transforms),
        "Particles " + std::to_string(view.particles) + " in " + std::to_string(view.particle_effects) + " effects (" + std::to_string(view.attached_effects) + " attached) | dropped " +
            std::to_string(view.dropped_particles) + " | emitters without a pass " + std::to_string(view.skipped_emitters),
        "",
        "Video memory " + mebibytes(stats.video_memory_usage) + " / " + mebibytes(stats.video_memory_budget) + " MiB budget",
        "Textures " + std::to_string(stats.live_textures) + " resident, " + std::to_string(stats.pending_texture_uploads) + " streaming | slots " + std::to_string(used_slots) + "/" +
            std::to_string(stats.texture_slot_capacity) + " | retiring " + std::to_string(stats.pending_retirements),
    };
}

} // namespace eaw::engine_app
