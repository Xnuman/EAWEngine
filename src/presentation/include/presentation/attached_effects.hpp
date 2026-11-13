#pragma once

// Presentation only: plays the particle effects attached to model bones (ALO proxies, such as engine
// glows) for every drawn entity, keeps them at their bones as the entity moves and animates, hides
// them while their bone is hidden, and stops them when the entity is no longer drawn.

#include "render/visual_frame.hpp"
#include "simulation/simulation.hpp"
#include "simulation/simulation_types.hpp"
#include "battle/object_types.hpp"
#include "presentation/entity_look.hpp"
#include "presentation/particle_effects.hpp"
#include "presentation/visual_rig.hpp"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace eaw {

class AttachedEffects
{
public:
    // Rigs parallel the scene's meshes (as UnitAnimator's); add_rigs appends runtime meshes'.
    void set_rigs(std::span<const VisualRig> rigs);
    void add_rigs(std::span<const VisualRig> rigs);

    // `instances` and `entities` are this frame's drawn list (invalid ids, such as corpses, carry no
    // effects); `palette` is the bone palette the instances' bone offsets index. `snapshot` supplies
    // speeds for engine effects, whose intensity follows the entity's speed over its top speed,
    // smoothed over about half a second of `seconds` (presentation time).
    void update(ParticleWorld& world, std::span<const VisualInstance> instances, std::span<const EntityId> entities, std::span<const Matrix4> palette, std::span<const EntitySnapshot> snapshot = {},
                double seconds = 0.0);

    // Stops every attached effect (a scene change).
    void clear(ParticleWorld& world);

    [[nodiscard]] std::size_t playing() const noexcept
    {
        return playing_.size();
    }

private:
    struct Attachment
    {
        std::shared_ptr<const ParticleEffect> effect;
        std::shared_ptr<const ParticleEmitMesh> emit_mesh;
        std::uint32_t bone{};
        // The bone's bind-pose transform in model space (row-vector convention).
        Matrix4 bind{Matrix4::identity()};
        bool engine{};
        std::optional<std::uint32_t> state;
    };
    struct MeshEffects
    {
        std::vector<Attachment> attachments;
        std::uint32_t bone_count{};
        // The palette has a visibility half (bones hidden by the pose collapse there).
        bool visibility_half{};
    };
    struct Playing
    {
        ParticleInstanceId instance;
        std::uint64_t frame{};
    };

    std::vector<MeshEffects> meshes_;
    // (entity index, generation, attachment) -> playing effect.
    std::unordered_map<std::uint64_t, Playing> playing_;
    // Smoothed speed fraction per entity (by entity key), and when it was last updated.
    struct Power
    {
        float value{};
        std::uint64_t frame{};
    };
    std::unordered_map<std::uint64_t, Power> power_;
    double last_seconds_{};
    std::uint64_t frame_{};
};

} // namespace eaw
