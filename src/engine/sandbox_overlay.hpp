#pragma once

// Text of the sandbox's statistics panel (F1), built from the values it reports.

#include "eaw/d3d12_renderer.hpp"
#include "eaw/simulation.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace eaw {

enum class SandboxCameraMode
{
    // Model inspection (orbit camera).
    model,
    // A map with the free debug camera.
    free,
    // A map with the legacy RTS camera.
    rts,
};

struct SandboxOverlayView
{
    std::string adapter_name;
    D3d12RendererStats stats;
    bool vsync{};
    std::uint32_t frame_latency{};
    bool raw_input{};
    std::uint32_t raw_events{};
    // Frames shown and seconds elapsed since the previous refresh.
    std::uint64_t frames{};
    double seconds{};
    std::string scene_status;
    std::array<float, 3> eye{};
    // Model mode only.
    std::optional<std::string> animation_status;
    bool animation_playing{};
    // What the selection is ("none", one object's description, or a count).
    std::string selected_text;
    const SimulationWorld* world{};
    const std::unordered_set<std::uint64_t>* selection{};
    std::uint64_t dropped_ticks{};
    double last_tick_ms{};
    double slowest_tick_ms{};
    std::uint64_t orders_issued{};
    // The current debug-spawn type, how many types exist, and their owner (maps with spawns).
    std::optional<std::string> spawn_type;
    std::size_t spawn_types{};
    std::uint32_t spawn_owner{};
    std::string reload_status;
    SandboxCameraMode camera{SandboxCameraMode::model};
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
