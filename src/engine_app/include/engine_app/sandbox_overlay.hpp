#pragma once

// Text of the sandbox's statistics panel: graphics numbers only (frame times, draw work, detail
// levels, particles, memory, textures).

#include "render/d3d12_renderer.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eaw::engine_app {

// The window's input over some frames: raw input records read and the time reading all input took
// (PlatformWindow::poll_events), to check that high polling rate mice stay cheap.
struct InputLoad
{
    std::uint64_t frames{};
    std::uint64_t records{};
    std::uint32_t max_records{};
    double seconds{};
    double max_seconds{};

    void add(std::uint32_t frame_records, double frame_seconds);
};

// "Input raw | 12.0 records a frame (max 40) | read 0.02 ms (max 0.10)".
[[nodiscard]] std::string input_load_line(const InputLoad& load, bool raw_input);

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
    InputLoad input;
    bool raw_input{};
};

[[nodiscard]] std::vector<std::string> sandbox_overlay_lines(const SandboxOverlayView& view);

} // namespace eaw::engine_app
