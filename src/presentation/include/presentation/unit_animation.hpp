#pragma once

// Unit animation on maps, on the presentation side: nothing here feeds back into the simulation.
// Each drawn entity blends up to three clips of its mesh, chosen from its snapshot:
//
// - Locomotion, by speed. The snapshot's speed is smoothed (about 0.15 s), and the unit's place on
//   the IDLE (standing) - WALKMOVE (40% of its top speed) - MOVE (top speed) scale picks two
//   neighbouring clips and a blend between them; a mesh without WALKMOVE blends IDLE into MOVE
//   by a quarter of its top speed. WALKMOVE and MOVE share one normalized phase so steps stay in
//   step while they blend, and play faster or slower with the speed (0.5x to 1.5x) to limit foot
//   sliding. IDLE plays on its own (AnimationProfile::loop_first_idle says how).
// - Attack. A unit with a target that stands still fades its aiming pose (ATTACKIDLE) in over
//   0.2 s, over its locomotion, and out again when it moves or loses its target. Every shot (the
//   snapshot's last_shot_tick) plays ATTACK once from its start, fading back to the aiming pose
//   over 0.15 s after it ends; a turreted unit firing on the move plays it over its locomotion.
//   A mesh without ATTACKIDLE loops ATTACK as its aiming pose, one without ATTACK only aims.
//
// - Death. A unit combat destroyed leaves a corpse (add_corpse) that plays a DIE variant once,
//   holds its last frame for 4 s, and is then removed; corpses of meshes without a DIE clip are
//   kept only when they show a damage state (rubble, at rest pose), else they vanish at once. Corpses are drawn after the living units with
//   invalid entity ids, so they cannot be picked.
//
// Clip variants come from the entity's id; loops start at an id-dependent phase so neighbours do
// not move in lockstep. Poses are sampled at pose slots (each clip's frames, at least 30 per
// second) with the blend and attack weights rounded to eighths, and shared: every entity at the
// same mesh, clips, slots, and weights uses one palette range, so units at rest or at full speed
// share as before and only units in mid-transition need their own. These rules and numbers are
// the engine's (the original's selection and blending are not documented); turns, flinches,
// deaths, and crouching are not modeled yet.

#include "render/visual_scene.hpp"
#include "simulation/simulation.hpp"
#include "battle/object_types.hpp"
#include "presentation/entity_look.hpp"
#include "presentation/visual_rig.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eaw {

// The role an entity's snapshot asks for, before falling back to the clips its mesh has: attack
// for a unit with a target that is not moving, move for a moving one, idle otherwise.
[[nodiscard]] AnimationRole wanted_animation_role(const EntitySnapshot& entity) noexcept;

// Weights of the locomotion clips at a speed: `lower` and `upper` are roles (idle, walk_move,
// move) and `blend` how far the pose is from lower to upper (0..1). `has_walk` tells whether the
// mesh has a WALKMOVE clip; `top_speed` is the unit's top speed (no blend without one).
struct LocomotionBlend
{
    AnimationRole lower{AnimationRole::idle};
    AnimationRole upper{AnimationRole::idle};
    float blend{};
};
[[nodiscard]] LocomotionBlend locomotion_blend(float speed, float top_speed, bool has_walk) noexcept;

// What a unit's animation played in one frame, for the events placed in animations (footsteps,
// hydraulics; assets/animation_events): the clip and the frames it passed, after `from_frame` up to
// and including `to_frame` (a loop that wrapped is reported as two cues). Units report the clip
// they show most of their locomotion (when it has at least animation_cue_minimum_weight of the
// pose) and an ATTACK that plays; corpses report their DIE clip with an invalid entity.
struct AnimationCue
{
    EntityId entity;
    Vector3 position;
    // VisualClip::path of the clip.
    std::string_view clip;
    double from_frame{};
    double to_frame{};
};

// The least weight of the pose a locomotion clip needs to report its events (an engine choice: a
// clip blending out does not step).
inline constexpr float animation_cue_minimum_weight = 0.25F;

class UnitAnimator final
{
public:
    // `rigs` parallel the scene's meshes; `rest_palette` is the scene's rest palette
    // (VisualMapAsset). Both are copied (clips and models are shared).
    UnitAnimator(std::span<const VisualRig> rigs, std::span<const Matrix4> rest_palette);

    struct Stats
    {
        // Entities shown in an animation, of them blending two or more clips, distinct poses drawn,
        // poses sampled this frame (cache misses), and entities left in their rest pose because
        // the palette was full.
        std::size_t animated{};
        std::size_t blending{};
        std::size_t poses{};
        std::size_t sampled{};
        std::size_t over_budget{};
        std::size_t palette_entries{};
        // Corpses drawn this frame.
        std::size_t corpses{};
    };

    // `instances[i]` shows `entities[i]` (invalid for backgrounds), as ScenePresentation::build
    // produces them; `snapshot` holds the entities' state. Sets every instance's bone offset (rest
    // pose or a shared animated pose), appends the corpses still shown (with invalid entity ids),
    // and fills `palette` with the rest palette followed by this frame's poses, at most
    // `max_palette` entries. `seconds` is presentation time and should not go backwards (a step back
    // counts as no time passing).
    void animate(std::span<const EntitySnapshot> snapshot, std::vector<EntityId>& entities, std::vector<VisualInstance>& instances, double seconds, std::size_t max_palette,
                 std::vector<Matrix4>& palette, std::span<const AnimationProfile> animations = {});
    // Keeps a dead entity's instance (ScenePresentation::corpse_instance) as a corpse that plays a
    // DIE clip from `seconds` on; without one, only an instance in a damage state stays (rubble),
    // for the style's persistence or for good. At most 4096 corpses are kept (the oldest go first).
    // `style` picks the DIE variant and how long the corpse stays (DeathStyle); a crushed death plays
    // the style's crushed clip when the mesh has it.
    void add_corpse(const VisualInstance& instance, EntityId id, double seconds, const DeathStyle& style = {}, bool crushed = false);
    [[nodiscard]] const Stats& stats() const noexcept;
    // What the last animate() played (valid until the next one).
    [[nodiscard]] const std::vector<AnimationCue>& cues() const noexcept;
    // A wreck that went in the last animate() with an effect to play there (DeathStyle::end_effect).
    struct EndedCorpse
    {
        Vector3 position;
        std::uint32_t effect{};
    };
    // Valid until the next animate(); wrecks dropped for the corpse limit are not listed.
    [[nodiscard]] const std::vector<EndedCorpse>& ended_corpses() const noexcept;
    // Rigs of meshes added to the scene at runtime (a SpawnablePackage's rigs and rest palette,
    // rest offsets relative to it); they must follow the meshes the animator already knows.
    void add_rigs(std::span<const VisualRig> rigs, std::span<const Matrix4> rest_palette);
    [[nodiscard]] std::size_t rig_count() const noexcept;

private:
    // One weighted clip of a pose: clip index in the rig (-1 for none, -2 for the rest pose), pose
    // slot, and weight in eighths.
    struct Layer
    {
        std::int32_t clip{-1};
        std::uint32_t slot{};
        std::uint32_t eighths{};
        bool operator==(const Layer&) const = default;
    };
    struct PoseKey
    {
        std::uint32_t mesh{};
        std::array<Layer, 4> layers{};
        // A turret's aim, in steps (turret_yaw_step, barrel_pitch_step), and its bones (-1: none).
        std::int32_t turret{-1};
        std::int32_t barrel{-1};
        std::int16_t yaw{};
        std::int16_t pitch{};
        bool operator==(const PoseKey&) const = default;
    };
    struct PoseKeyHash
    {
        std::size_t operator()(const PoseKey& key) const noexcept;
    };
    struct EntityState
    {
        std::uint32_t mesh{};
        // Chosen clip per role (-1 when the mesh has none).
        std::int32_t idle{-1};
        std::int32_t walk{-1};
        std::int32_t move{-1};
        std::int32_t attack{-1};
        std::int32_t attack_idle{-1};
        // The last shot seen (snapshot tick) and seconds since it.
        std::uint64_t last_shot{};
        double since_shot{1.0e9};
        float display_speed{};
        double idle_start{};
        // Idles played to their end so far (picks the next random variant).
        std::uint64_t idle_plays{};
        // Normalized phase shared by WALKMOVE and MOVE.
        double stride_phase{};
        float attack_weight{};
        double attack_time{};
        double last_seconds{};
        std::uint64_t frame{};
        // The locomotion clip reported last frame and where it was (frames), and ATTACK's place.
        std::int32_t cue_clip{-1};
        double cue_frame{};
        double fire_cue_frame{-1.0};
        // A turret's current aim relative to the unit's facing (radians).
        float turret_yaw{};
        float barrel_pitch{};
        // Transitions: the heading and motion seen last frame, the turn in place under way (-1 right,
        // 1 left, 0 none) and since when, and the one-shot clip playing (-1 none) and since when.
        float last_heading{};
        bool was_moving{};
        int turning{};
        double turn_start{};
        std::int32_t transition{-1};
        double transition_start{};
        bool frame_seen{};
    };

    struct Corpse
    {
        VisualInstance instance;
        std::int32_t clip{};
        double died{};
        // When it is removed (seconds after `died`).
        double lifetime{};
        // Where its DIE clip was last frame (frames; -1 before the first).
        double cue_frame{-1.0};
        std::uint32_t end_effect{};
    };
    // Reports the frames a clip passed from `from` to `to` (a loop of `loop_frames` wraps).
    void add_cue(EntityId entity, const Vector3& position, const VisualClip& clip, double from, double to, double loop_frames);
    // A variant of `role` for `id` (`salt` 0 for its first choice, another for later ones).
    [[nodiscard]] std::int32_t choose_clip(std::uint32_t mesh, AnimationRole role, EntityId id, std::uint64_t salt = 0) const;
    // The clip of `mesh` with `role` and `variant`, or -1.
    [[nodiscard]] std::int32_t find_clip(std::uint32_t mesh, AnimationRole role, std::uint32_t variant) const;
    // The index of the bone of `mesh` whose name has `key` (game_object_name_crc), or -1.
    [[nodiscard]] std::int32_t bone_of(std::uint32_t mesh, std::uint32_t key);
    // This frame's palette offset of a pose, sampling it when needed; nullopt when the palette is
    // full.
    [[nodiscard]] std::optional<std::uint32_t> place(const PoseKey& key, std::size_t max_palette, std::vector<Matrix4>& palette);
    [[nodiscard]] std::uint32_t slot_of(std::uint32_t mesh, std::int32_t clip, double seconds) const;
    [[nodiscard]] std::uint32_t slot_count(std::uint32_t mesh, std::int32_t clip) const;
    void sample(const PoseKey& key, std::vector<Matrix4>& out) const;

    std::vector<VisualRig> rigs_;
    std::vector<Matrix4> rest_palette_;
    std::unordered_map<std::uint64_t, EntityState> states_;
    // Sampled poses, kept across frames up to a size limit.
    std::unordered_map<PoseKey, std::vector<Matrix4>, PoseKeyHash> pose_cache_;
    std::size_t cached_entries_{};
    // This frame's palette offset of each pose.
    std::unordered_map<PoseKey, std::uint32_t, PoseKeyHash> frame_offsets_;
    std::unordered_map<std::uint64_t, std::size_t> snapshot_index_;
    // Bone indices by mesh and name key (bone_of).
    std::unordered_map<std::uint64_t, std::int32_t> bones_;
    std::vector<Corpse> corpses_;
    std::vector<AnimationCue> cues_;
    std::vector<EndedCorpse> ended_corpses_;
    std::uint64_t frame_{};
    Stats stats_;
};

} // namespace eaw
