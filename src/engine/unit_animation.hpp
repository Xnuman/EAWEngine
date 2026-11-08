#pragma once

// Unit animation on maps, on the presentation side: nothing here feeds back into the simulation.
// Each frame, every drawn entity's animation role follows from its snapshot:
//   moving                         MOVE, else WALKMOVE, else IDLE;
//   a target, standing             ATTACK, else ATTACKIDLE, else IDLE;
//   otherwise                      IDLE.
// The clip variant comes from the entity's id. A clip restarts when the entity's role changes;
// idle, move, and attack-idle loops start at an id-dependent phase so neighbours do not move in
// lockstep. Poses are sampled at pose slots (the clip's own frames, at least 30 per second) and
// shared: every entity showing the same mesh, clip, and slot uses one palette range, so the cost
// follows the number of distinct poses on screen, not the number of units. These choices are the
// engine's; the original's animation selection (turns, flinches, deaths, crouching) is not
// modeled yet.

#include "visual_scene_builder.hpp"

#include "eaw/simulation.hpp"
#include "eaw/visual_scene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace eaw {

// The role an entity's snapshot asks for, before falling back to the clips its mesh has.
[[nodiscard]] AnimationRole wanted_animation_role(const EntitySnapshot& entity) noexcept;

class UnitAnimator final
{
public:
    // `rigs` parallel the scene's meshes; `rest_palette` is the scene's rest palette
    // (VisualMapAsset). Both are copied (clips and models are shared).
    UnitAnimator(std::span<const VisualRig> rigs, std::span<const std::array<float, 16>> rest_palette);

    struct Stats
    {
        // Entities shown in an animation, distinct poses drawn, poses sampled this frame (cache
        // misses), and entities left in their rest pose because the palette was full.
        std::size_t animated{};
        std::size_t poses{};
        std::size_t sampled{};
        std::size_t over_budget{};
        std::size_t palette_entries{};
    };

    // `instances[i]` shows `entities[i]` (invalid for backgrounds), as ScenePresentation::build
    // produces them; `snapshot` holds the entities' state. Sets every instance's bone offset (rest
    // pose or a shared animated pose) and fills `palette` with the rest palette followed by this
    // frame's poses, at most `max_palette` entries. `seconds` is presentation time.
    void animate(std::span<const EntitySnapshot> snapshot, std::span<const EntityId> entities, std::span<VisualInstance> instances, double seconds, std::size_t max_palette,
                 std::vector<std::array<float, 16>>& palette);
    [[nodiscard]] const Stats& stats() const noexcept;

private:
    struct EntityState
    {
        std::uint32_t mesh{};
        AnimationRole role{};
        std::size_t clip{};
        double start{};
        std::uint64_t frame{};
    };
    // The clip an entity of `mesh` shows for `wanted` (nullopt: rest pose).
    [[nodiscard]] std::optional<std::size_t> choose_clip(std::uint32_t mesh, AnimationRole wanted, EntityId id) const;

    std::vector<VisualRig> rigs_;
    std::vector<std::array<float, 16>> rest_palette_;
    std::unordered_map<std::uint64_t, EntityState> states_;
    // Sampled poses by (mesh, clip, slot), kept across frames up to a size limit.
    std::unordered_map<std::uint64_t, std::vector<std::array<float, 16>>> pose_cache_;
    std::size_t cached_entries_{};
    // This frame's palette offset of each pose key.
    std::unordered_map<std::uint64_t, std::uint32_t> frame_offsets_;
    std::unordered_map<std::uint64_t, std::size_t> snapshot_index_;
    std::uint64_t frame_{};
    Stats stats_;
};

} // namespace eaw
