#pragma once

// Bridges simulation snapshots to renderer instances. A loaded scene's non-background instances
// become entity templates (mesh, original transform, colors) keyed by entity id, and each
// spawnable type a template keyed by its visual (battle.hpp), so entities spawned by order are
// drawn without their ids being known in advance. Each frame the interpolated snapshot re-poses
// them. Background layers (sky domes) stay static.

#include "render/visual_scene.hpp"
#include "simulation/simulation.hpp"
#include "battle/battle.hpp"
#include "battle/object_types.hpp"
#include "presentation/entity_look.hpp"
#include "presentation/visual_rig.hpp"

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
[[nodiscard]] Matrix4 pose_transform(const Matrix4& base, float base_heading, const Vector3& position, float heading, float pitch = 0.0F, float bank = 0.0F);

// Presentation bank of a flyer turning at `yaw_rate` radians per second: about 0.6 radians of bank
// per radian per second (an engine choice), at most `limit` (its type's Bank_Turn_Angle).
[[nodiscard]] float flight_bank(float yaw_rate, float limit = default_bank_limit) noexcept;

[[nodiscard]] inline std::uint64_t entity_key(const EntityId id) noexcept
{
    return (static_cast<std::uint64_t>(id.generation) << 32U) | id.index;
}

// The first entity index of decorations (ScenePresentation::decoration_snapshots), far above the
// indices the simulation hands out.
inline constexpr std::uint32_t decoration_id_base = 0x40000000U;

// The shield flash (GameConstants' Shield_Flash_Scale "1.0, 1.1, 1.25" and Shield_Flash_Duration
// 0.1, read as one step per scale; the stock values, not read from the file yet).
inline constexpr std::array<float, 3> shield_flash_scales{1.0F, 1.1F, 1.25F};
inline constexpr double shield_flash_step = 0.1;

class ScenePresentation final
{
public:
    // Records each placed entity's template from `scene.instances` under its id (`ids` parallels
    // `entities`, as Battle::map_entity_ids; empty ids are skipped) and one template per spawnable
    // type under spawnable_visual(type index).
    ScenePresentation(const VisualScene& scene, std::span<const MapEntity> entities, std::span<const MapEntityLook> looks, std::span<const std::optional<EntityId>> ids,
                      std::span<const SpawnableLook> spawnable, std::span<const Decoration> decorations = {});

    // Builds this frame's instances: static backgrounds first, then one per snapshot entity in
    // snapshot order. `entities_out[i]` names the entity behind `instances_out[i]` (invalid for
    // backgrounds), so picks and selections can be mapped back.
    // `animations_out`, when given, receives each instance's AnimationProfile (backgrounds a default).
    // `seconds` is presentation time: an entity whose shields flash (flash_shields) is followed by
    // its shield mesh, drawn without an entity.
    void build(std::span<const EntitySnapshot> snapshot, const std::unordered_set<std::uint64_t>& selected, std::vector<VisualInstance>& instances_out, std::vector<EntityId>& entities_out,
               std::vector<AnimationProfile>* animations_out = nullptr, double seconds = 0.0) const;

    // Shield flashes: a shot an entity's shields absorbed (ImpactSnapshot::target, kind shield) shows
    // its model's shield mesh, if it has one, growing through shield_flash_scales for
    // shield_flash_step seconds each, from `seconds` (presentation time).
    // Not called for ordinary hits (they show the projectile's Projectile_Absorbed_By_Shields_Particle):
    // kept for the shield abilities (SHIELD_FLARE, MISSILE_SHIELD, DEFEND), not implemented yet.
    void flash_shields(std::span<const ImpactSnapshot> impacts, double seconds);

    // Meshes added to the renderer's mesh library at runtime (D3d12Renderer::add_meshes), in order,
    // and spawnable types added to the battle (Battle::add_spawnable), in the same order as there.
    void add_meshes(std::span<const VisualMesh> meshes);
    // Idle snapshots of the decorations, with ids of their own (decoration_id_base on): append them to
    // the world's snapshot so decorations are posed, animated, and given their effects like
    // entities, though the simulation never sees them.
    [[nodiscard]] std::span<const EntitySnapshot> decoration_snapshots() const noexcept
    {
        return decoration_snapshots_;
    }

    void add_spawnable(std::span<const SpawnableLook> types);

    // The instance a dead entity leaves behind, posed where it died, with its type's corpse style,
    // and forgets the entity's template; nullopt when the entity was never drawn or its type leaves
    // no corpse (DeathStyle::removed).
    struct Corpse
    {
        VisualInstance instance;
        DeathStyle style;
    };
    [[nodiscard]] std::optional<Corpse> corpse_instance(const DeathSnapshot& death);

    // The mesh `entity` is drawn with and the bones of its hardpoint `hardpoint`
    // (MapEntityLook::hardpoints); nullopt when it is not drawn or has no such hardpoint.
    struct HardpointBones
    {
        std::uint32_t mesh{};
        HardpointLook bones;
        // Where the entity is drawn (its instance transform, without flight bank).
        Matrix4 transform{Matrix4::identity()};
    };
    [[nodiscard]] std::optional<HardpointBones> hardpoint_bones(const EntitySnapshot& entity, std::uint32_t hardpoint) const;

    [[nodiscard]] const std::string& mesh_name(std::uint32_t mesh) const;
    [[nodiscard]] const Vector3& mesh_minimum(std::uint32_t mesh) const;
    [[nodiscard]] const Vector3& mesh_maximum(std::uint32_t mesh) const;

private:
    struct Template
    {
        VisualInstance instance;
        float base_heading{};
        DeathStyle death;
        float bank_limit{default_bank_limit};
        AnimationProfile animation;
        std::vector<DamageStep> damage;
        std::optional<std::uint32_t> shield_mesh;
        std::vector<HardpointLook> hardpoints;
    };
    std::vector<VisualInstance> backgrounds_;
    // Entity key -> presentation time of its last shield hit (flash_shields).
    std::unordered_map<std::uint64_t, double> shield_hits_;
    // The map's own surfaces that no entity or decoration draws (the water plane, terrain tracks),
    // drawn every frame after the backgrounds.
    std::vector<VisualInstance> scenery_;
    std::unordered_map<std::uint64_t, Template> templates_;
    // By spawnable type index (visual - spawnable_visual_base); none for a hidden type.
    std::vector<std::optional<Template>> spawn_templates_;
    std::vector<EntitySnapshot> decoration_snapshots_;
    std::vector<std::string> mesh_names_;
    std::vector<Vector3> mesh_minimum_;
    std::vector<Vector3> mesh_maximum_;

    // An entity's template: placed entities by id, spawned ones by their type's visual; null when
    // it has none.
    [[nodiscard]] const Template* template_of(EntityId id, std::uint32_t visual) const;
};

} // namespace eaw
