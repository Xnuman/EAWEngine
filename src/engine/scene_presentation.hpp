#pragma once

// Bridges simulation snapshots to renderer instances. A loaded scene's non-background instances
// become entity templates (mesh, original transform, colors) keyed by entity id, and each
// spawnable type a template keyed by its visual (battle.hpp), so entities spawned by order are
// drawn without their ids being known in advance. Each frame the interpolated snapshot re-poses
// them. Background layers (sky domes) stay static.

#include "battle.hpp"
#include "eaw/simulation.hpp"
#include "eaw/visual_scene.hpp"
#include "visual_scene_builder.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eaw {

// Re-poses `base` (row-vector transform whose model forward axis points along `base_heading` about +Z):
// keeps its scale and tilt, rotates it about world +Z by `heading - base_heading`, and moves it to
// `position`.
// `pitch` raises the nose above the horizontal and `bank` rolls about the forward axis (positive
// lowers the left side, as in a left turn), both in radians.
[[nodiscard]] std::array<float, 16> pose_transform(const std::array<float, 16>& base, float base_heading, const SimVector3& position, float heading, float pitch = 0.0F, float bank = 0.0F);

// Presentation bank of a flyer turning at `yaw_rate` radians per second (an engine choice: about
// 0.6 radians of bank per radian per second, at most 60 degrees).
[[nodiscard]] float flight_bank(float yaw_rate) noexcept;

[[nodiscard]] inline std::uint64_t entity_key(const EntityId id) noexcept
{
    return (static_cast<std::uint64_t>(id.generation) << 32U) | id.index;
}

class ScenePresentation final
{
public:
    // Records each placed entity's template from `scene.instances` under its id (`ids` parallels
    // `entities`, as Battle::map_entity_ids; empty ids are skipped) and one template per spawnable
    // type under spawnable_visual(type index).
    ScenePresentation(const VisualScene& scene, std::span<const MapEntitySpawn> entities, std::span<const std::optional<EntityId>> ids, std::span<const SpawnableType> spawnable);

    // Builds this frame's instances: static backgrounds first, then one per snapshot entity in
    // snapshot order. `entities_out[i]` names the entity behind `instances_out[i]` (invalid for
    // backgrounds), so picks and selections can be mapped back.
    void build(std::span<const EntitySnapshot> snapshot, const std::unordered_set<std::uint64_t>& selected, std::vector<VisualInstance>& instances_out, std::vector<EntityId>& entities_out) const;

    [[nodiscard]] const std::string& mesh_name(std::uint32_t mesh) const;
    [[nodiscard]] const std::array<float, 3>& mesh_minimum(std::uint32_t mesh) const;
    [[nodiscard]] const std::array<float, 3>& mesh_maximum(std::uint32_t mesh) const;

private:
    struct Template
    {
        VisualInstance instance;
        float base_heading{};
    };
    std::vector<VisualInstance> backgrounds_;
    std::unordered_map<std::uint64_t, Template> templates_;
    // By spawnable type index (visual - spawnable_visual_base).
    std::vector<Template> spawn_templates_;
    std::vector<std::string> mesh_names_;
    std::vector<std::array<float, 3>> mesh_minimum_;
    std::vector<std::array<float, 3>> mesh_maximum_;
};

} // namespace eaw
