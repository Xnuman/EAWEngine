#pragma once

#include "engine/projectile_visuals.hpp"

// The sandbox's debug lines for one frame: bounds and axes, shots in flight, health and shield bars,
// selected units' routes, and the passability overlay.

#include "render/visual_scene.hpp"
#include "simulation/ground_map.hpp"
#include "simulation/simulation.hpp"
#include "engine/scene_presentation.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

namespace eaw {

struct SandboxDebugView
{
    // Null while no scene runs.
    const ScenePresentation* presentation{};
    const SimulationWorld* world{};
    // This frame's instances and the entity behind each (see ScenePresentation::build).
    std::span<const VisualInstance> instances;
    std::span<const EntityId> entities;
    const std::unordered_set<std::uint64_t>* selection{};
    // The map's ground (invalid on space maps and models).
    const GroundMap* ground{};
    // Center of the passability overlay (the tactical camera's focus); none without one.
    std::optional<Vector3> focus;
    bool show_bounds{};
    bool show_passability{};
    float axis_length{1.0F};
    // Shots of this owner are drawn green, the rest red; shots whose projectile has a visual here
    // are drawn as bolts instead (no streak).
    std::uint32_t friendly_owner{};
    const ProjectileVisualTable* projectile_visuals{};
};

[[nodiscard]] std::vector<VisualDebugLine> build_sandbox_debug_lines(const SandboxDebugView& view);

} // namespace eaw
