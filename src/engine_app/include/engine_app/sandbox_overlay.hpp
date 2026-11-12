#pragma once

// Text of the sandbox's statistics panel: graphics numbers only (frame times, draw work, detail
// levels, particles, memory, textures).

#include "render/d3d12_renderer.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eaw {

struct SandboxOverlayView
{
    std::string adapter_name;
    D3d12RendererStats stats;
    bool vsync{};
    std::uint32_t frame_latency{};
    // Frames shown and seconds elapsed since the previous refresh.
    std::uint64_t frames{};
    double seconds{};
    // Particle effects playing, live particles, attached (proxy) effects, and particles dropped at
    // capacity and emitters skipped for lack of a pass (cumulative).
    std::size_t particle_effects{};
    std::size_t particles{};
    std::size_t attached_effects{};
    std::size_t dropped_particles{};
    std::size_t skipped_emitters{};
};

[[nodiscard]] std::vector<std::string> sandbox_overlay_lines(const SandboxOverlayView& view);

} // namespace eaw
