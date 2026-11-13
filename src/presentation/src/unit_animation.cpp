#include "presentation/unit_animation.hpp"

#include "math/matrix.hpp"
#include "math/scalar.hpp"
#include "core/string_utils.hpp"
#include "assets/animation.hpp"
#include "assets/game_object_catalog.hpp"
#include "assets/pose.hpp"
#include "assets/skeleton.hpp"
#include "presentation/scene_presentation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eaw {
namespace {

// Engine choices (see unit_animation.hpp).
constexpr float minimum_slots_per_second = 30.0F;
constexpr float speed_smoothing_seconds = 0.15F;
constexpr float attack_fade_seconds = 0.2F;
constexpr double shot_fade_seconds = 0.15;
constexpr float walk_speed_fraction = 0.4F;
constexpr float move_without_walk_fraction = 0.25F;
constexpr float minimum_playback_rate = 0.5F;
constexpr float maximum_playback_rate = 1.5F;
// Layer clip values: no layer, and the mesh's rest pose (for meshes without an IDLE clip).
constexpr std::int32_t no_clip = -1;
constexpr std::int32_t rest_clip = -2;
// Corpses hold their last frame this long after their DIE clip ends, and at most this many are kept.
constexpr double corpse_linger_seconds = 4.0;
constexpr std::size_t maximum_corpses = 4096;
// Turret aims are quantized so that units aiming alike share poses: 128 steps of yaw around the
// circle, 64 of pitch over a half turn (engine choices: under 3 degrees each).
constexpr float turret_yaw_step = two_pi / 128.0F;
constexpr float barrel_pitch_step = pi / 64.0F;
// A unit turns in place when its heading changes faster than this (radians per second) while it
// moves slower than turn_in_place_speed_fraction of its top speed (engine choices).
constexpr float turn_in_place_rate = 0.15F;
constexpr float turn_in_place_speed_fraction = 0.1F;
// Bounds of a transition's playback rate (engine choices; the stock AT-AT walks at 3.7x, the
// Bantha at 5x).
constexpr float minimum_transition_playback = 0.25F;
// Transitions fade in from the pose before them and out into the one after them over this long
// (engine choice).
constexpr double transition_fade_seconds = 0.25;
constexpr float maximum_transition_playback = 6.0F;
// Frames longer than this (a stall, a hidden window) count as this long.
constexpr double longest_frame_seconds = 0.25;
// Sampled poses kept across frames (matrices); beyond it the cache starts over.
constexpr std::size_t pose_cache_limit = 4U * 1024U * 1024U;

// The most idles one frame moves past (a first sight starts up to a minute in), and the salt that
// spreads an entity's successive idle choices.
constexpr std::size_t maximum_idle_switches = 256;
constexpr std::uint64_t idle_variant_salt = 0x9e3779b97f4a7c15ULL;
// A unit with a single IDLE clip that does not loop it waits this long before playing it again
// (seconds, random in between; the owner's choice, 2026-11-13, until the original is observed).
constexpr double single_idle_pause_min_seconds = 2.0;
constexpr double single_idle_pause_max_seconds = 4.0;

std::uint64_t mix(std::uint64_t value) noexcept
{
    // SplitMix64 finalizer: a stable per-entity hash for variants and phases.
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

// A fraction 0..1 of an entity's hash, for phases.
double unit_fraction(const std::uint64_t key, const std::uint64_t salt) noexcept
{
    return static_cast<double>(mix(key ^ salt) % 10000U) / 10000.0;
}

std::uint32_t eighths(const float weight) noexcept
{
    return static_cast<std::uint32_t>(std::lround(std::clamp(weight, 0.0F, 1.0F) * 8.0F));
}

} // namespace

AnimationRole wanted_animation_role(const EntitySnapshot& entity) noexcept
{
    if (entity.moving)
    {
        return AnimationRole::move;
    }
    if (entity.target.valid())
    {
        return AnimationRole::attack;
    }
    return AnimationRole::idle;
}

LocomotionBlend locomotion_blend(const float speed, const float top_speed, const bool has_walk) noexcept
{
    if (!(speed > 0.0F) || !(top_speed > 0.0F) || !std::isfinite(speed) || !std::isfinite(top_speed))
    {
        return {};
    }
    if (!has_walk)
    {
        return {AnimationRole::idle, AnimationRole::move, std::min(speed / (top_speed * move_without_walk_fraction), 1.0F)};
    }
    const auto walk = top_speed * walk_speed_fraction;
    if (speed < walk)
    {
        return {AnimationRole::idle, AnimationRole::walk_move, speed / walk};
    }
    return {AnimationRole::walk_move, AnimationRole::move, std::min((speed - walk) / (top_speed - walk), 1.0F)};
}

UnitAnimator::UnitAnimator(const std::span<const VisualRig> rigs, const std::span<const Matrix4> rest_palette)
    : rigs_(rigs.begin(), rigs.end()), rest_palette_(rest_palette.begin(), rest_palette.end())
{}

void UnitAnimator::add_rigs(const std::span<const VisualRig> rigs, const std::span<const Matrix4> rest_palette)
{
    const auto offset = static_cast<std::uint32_t>(rest_palette_.size());
    rest_palette_.insert(rest_palette_.end(), rest_palette.begin(), rest_palette.end());
    for (auto rig : rigs)
    {
        rig.rest_offset += offset;
        rigs_.push_back(std::move(rig));
    }
}

std::size_t UnitAnimator::rig_count() const noexcept
{
    return rigs_.size();
}

const UnitAnimator::Stats& UnitAnimator::stats() const noexcept
{
    return stats_;
}

std::size_t UnitAnimator::PoseKeyHash::operator()(const PoseKey& key) const noexcept
{
    auto hash = mix(key.mesh);
    for (const auto& layer : key.layers)
    {
        hash = mix(hash ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(layer.clip)) << 32U) ^ (static_cast<std::uint64_t>(layer.slot) << 4U) ^ layer.eighths);
    }
    hash = mix(hash ^ (static_cast<std::uint64_t>(static_cast<std::uint16_t>(key.yaw)) << 16U) ^ static_cast<std::uint16_t>(key.pitch) ^
               (static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.turret)) << 32U));
    hash = mix(hash ^ key.mask);
    return static_cast<std::size_t>(hash);
}

std::int32_t UnitAnimator::bone_of(const std::uint32_t mesh, const std::uint32_t key)
{
    if (key == 0 || mesh >= rigs_.size() || !rigs_[mesh].skeleton)
    {
        return -1;
    }
    const auto lookup = (static_cast<std::uint64_t>(mesh) << 32U) | key;
    if (const auto found = bones_.find(lookup); found != bones_.end())
    {
        return found->second;
    }
    std::int32_t index = -1;
    const auto& bones = rigs_[mesh].skeleton->bones;
    for (std::size_t bone = 0; bone < bones.size(); ++bone)
    {
        if (game_object_name_crc(bones[bone].name) == key)
        {
            index = static_cast<std::int32_t>(bone);
            break;
        }
    }
    bones_.emplace(lookup, index);
    return index;
}

std::int32_t UnitAnimator::find_clip(const std::uint32_t mesh, const AnimationRole role, const std::uint32_t variant) const
{
    const auto& clips = rigs_[mesh].clips;
    const auto found = std::ranges::find_if(clips,
                                            [&](const VisualClip& clip)
                                            {
                                                return clip.role == role && clip.variant == variant;
                                            });
    return found != clips.end() ? static_cast<std::int32_t>(found - clips.begin()) : -1;
}

std::int32_t UnitAnimator::choose_clip(const std::uint32_t mesh, const AnimationRole role, const EntityId id, const std::uint64_t salt) const
{
    const auto& clips = rigs_[mesh].clips;
    const auto first = std::ranges::find(clips, role, &VisualClip::role);
    if (first == clips.end())
    {
        return -1;
    }
    const auto count = static_cast<std::uint64_t>(std::ranges::count(clips, role, &VisualClip::role));
    return static_cast<std::int32_t>(first - clips.begin()) + static_cast<std::int32_t>(mix(entity_key(id) ^ salt) % count);
}

std::uint32_t UnitAnimator::slot_count(const std::uint32_t mesh, const std::int32_t clip) const
{
    const auto& animation = *rigs_[mesh].clips[static_cast<std::size_t>(clip)].animation;
    const auto fps = std::max(animation.frames_per_second, 1.0F);
    const auto frames = static_cast<float>(animation.loop_frame_count);
    return static_cast<std::uint32_t>(std::max(1.0F, std::ceil(frames * std::max(1.0F, std::ceil(minimum_slots_per_second / fps)))));
}

// The slot of a clip at `seconds` into it (looping).
std::uint32_t UnitAnimator::slot_of(const std::uint32_t mesh, const std::int32_t clip, const double seconds) const
{
    const auto duration = static_cast<double>(animation_duration(*rigs_[mesh].clips[static_cast<std::size_t>(clip)].animation));
    const auto slots = slot_count(mesh, clip);
    const auto position = std::fmod(std::max(seconds, 0.0), duration) / duration;
    return std::min(static_cast<std::uint32_t>(position * static_cast<double>(slots)), slots - 1U);
}

void UnitAnimator::sample(const PoseKey& key, std::vector<Matrix4>& out) const
{
    const auto& rig = rigs_[key.mesh];
    std::vector<LocalBone> pose;
    float total = 0.0F;
    for (const auto& layer : key.layers)
    {
        if (layer.clip == no_clip || layer.eighths == 0)
        {
            continue;
        }
        std::vector<LocalBone> layer_pose;
        if (layer.clip == rest_clip)
        {
            layer_pose = bind_local_pose(*rig.skeleton);
        }
        else
        {
            const auto& clip = rig.clips[static_cast<std::size_t>(layer.clip)];
            const auto seconds = static_cast<float>(static_cast<double>(layer.slot) / static_cast<double>(slot_count(key.mesh, layer.clip)) * animation_duration(*clip.animation));
            layer_pose = sample_local_pose(*clip.animation, *rig.skeleton, seconds);
        }
        const auto weight = static_cast<float>(layer.eighths);
        if (pose.empty())
        {
            pose = std::move(layer_pose);
            total = weight;
            continue;
        }
        // Each further clip takes its share of the weight so far: the result is the weighted average.
        total += weight;
        blend_pose(pose, layer_pose, weight / total);
    }
    // The barrel pitches about the model's horizontal axis under the forward one (nose up for a
    // positive pitch), then the turret, barrel included, yaws about the model's up axis
    // (counter-clockwise, as headings).
    std::vector<BoneTurn> turns;
    if (key.barrel >= 0 && key.pitch != 0)
    {
        turns.push_back({static_cast<std::uint32_t>(key.barrel), {-1.0F, 0.0F, 0.0F}, static_cast<float>(key.pitch) * barrel_pitch_step});
    }
    if (key.turret >= 0 && key.yaw != 0)
    {
        turns.push_back({static_cast<std::uint32_t>(key.turret), {0.0F, 0.0F, 1.0F}, static_cast<float>(key.yaw) * turret_yaw_step});
    }
    const auto mask = key.mask != 0 ? masks_by_hash_.find(key.mask) : masks_by_hash_.end();
    append_rig_local_palette(rig, pose, out, turns, mask != masks_by_hash_.end() ? &mask->second : nullptr);
}

void UnitAnimator::destroy_hardpoint(const EntityId entity, const std::uint32_t mesh, const HardpointLook& hardpoint)
{
    if (mesh >= rigs_.size() || !rigs_[mesh].skeleton || (hardpoint.decal_bone < 0 && hardpoint.attachment_bone < 0 && hardpoint.damage_bone < 0))
    {
        return;
    }
    const auto& rig = rigs_[mesh];
    const auto bones = rig.skeleton->bones.size();
    auto& mask = bone_masks_[entity_key(entity)];
    if (mask.mesh != mesh || mask.hidden.size() != bones)
    {
        mask.mesh = mesh;
        mask.hidden = rig.intact_hidden.size() == bones ? rig.intact_hidden : std::vector<bool>(bones, false);
    }
    std::vector<std::int32_t> parents;
    parents.reserve(bones);
    for (const auto& bone : rig.skeleton->bones)
    {
        parents.push_back(bone.parent);
    }
    const auto apply = [&](const std::int32_t root, const bool hide)
    {
        // A whole subtree (scorch meshes and damage proxies hang below their bones).
        if (root < 0 || static_cast<std::size_t>(root) >= bones)
        {
            return;
        }
        const std::array<std::uint32_t, 1> roots{static_cast<std::uint32_t>(root)};
        const auto subtree = bones_in_subtrees(parents, roots);
        for (std::size_t bone = 0; bone < bones; ++bone)
        {
            if (subtree[bone])
            {
                mask.hidden[bone] = hide;
            }
        }
    };
    apply(hardpoint.decal_bone, false);
    apply(hardpoint.damage_bone, false);
    if (hardpoint.attachment_bone >= 0 && static_cast<std::size_t>(hardpoint.attachment_bone) < bones)
    {
        mask.hidden[static_cast<std::size_t>(hardpoint.attachment_bone)] = true;
    }
    // The mask's content hash (never 0, which means the rig's own).
    auto hash = mix(0x6d61736bULL ^ mesh);
    for (std::size_t bone = 0; bone < bones; ++bone)
    {
        hash = mix(hash ^ (mask.hidden[bone] ? bone + 1U : 0U));
    }
    mask.hash = hash != 0 ? hash : 1U;
    masks_by_hash_.emplace(mask.hash, mask.hidden);
}

std::optional<std::uint32_t> UnitAnimator::place(const PoseKey& key, const std::size_t max_palette, std::vector<Matrix4>& palette)
{
    if (const auto placed = frame_offsets_.find(key); placed != frame_offsets_.end())
    {
        return placed->second;
    }
    const auto& rig = rigs_[key.mesh];
    if (palette.size() + rig.palette_size > max_palette)
    {
        ++stats_.over_budget;
        return std::nullopt;
    }
    auto cached = pose_cache_.find(key);
    if (cached == pose_cache_.end())
    {
        std::vector<Matrix4> pose;
        pose.reserve(rig.palette_size);
        sample(key, pose);
        cached_entries_ += pose.size();
        cached = pose_cache_.emplace(key, std::move(pose)).first;
        ++stats_.sampled;
    }
    const auto offset = static_cast<std::uint32_t>(palette.size());
    palette.insert(palette.end(), cached->second.begin(), cached->second.end());
    frame_offsets_.emplace(key, offset);
    return offset;
}

void UnitAnimator::add_corpse(const VisualInstance& instance, const EntityId id, const double seconds, const DeathStyle& style, const bool crushed)
{
    bone_masks_.erase(entity_key(id));
    if (instance.mesh >= rigs_.size())
    {
        return;
    }
    // A mesh without a skeleton has no clips: it can only leave rubble.
    auto clip = rigs_[instance.mesh].skeleton ? choose_clip(instance.mesh, AnimationRole::die, id) : -1;
    if (style.die_variant)
    {
        // The death clone's DIE variant when the mesh has it.
        const auto& clips = rigs_[instance.mesh].clips;
        const auto specific = std::ranges::find_if(clips,
                                                   [&](const VisualClip& candidate)
                                                   {
                                                       return candidate.role == AnimationRole::die && candidate.variant == *style.die_variant;
                                                   });
        if (specific != clips.end())
        {
            clip = static_cast<std::int32_t>(specific - clips.begin());
        }
    }
    if (crushed && style.crushed)
    {
        // The crush clone's clip when the mesh has it.
        const auto& clips = rigs_[instance.mesh].clips;
        const auto specific = std::ranges::find_if(clips,
                                                   [&](const VisualClip& candidate)
                                                   {
                                                       return candidate.role == style.crushed->role && candidate.variant == style.crushed->variant;
                                                   });
        if (specific != clips.end())
        {
            clip = static_cast<std::int32_t>(specific - clips.begin());
        }
    }
    if (clip < 0)
    {
        // Without a DIE clip only a damaged state stays (a structure's rubble, at rest pose): for its
        // persistence, else for good (an engine choice: the original's rubble lasts, as remembered).
        if (instance.state != 0U)
        {
            if (corpses_.size() >= maximum_corpses)
            {
                corpses_.erase(corpses_.begin());
            }
            const auto lifetime = style.persistence_seconds ? static_cast<double>(*style.persistence_seconds) : std::numeric_limits<double>::infinity();
            corpses_.push_back({instance, -1, seconds, lifetime, -1.0, style.end_effect});
        }
        return;
    }
    if (corpses_.size() >= maximum_corpses)
    {
        corpses_.erase(corpses_.begin());
    }
    const auto duration = static_cast<double>(animation_duration(*rigs_[instance.mesh].clips[static_cast<std::size_t>(clip)].animation));
    const auto lifetime = style.persistence_seconds ? std::max(static_cast<double>(*style.persistence_seconds), duration) : duration + corpse_linger_seconds;
    corpses_.push_back({instance, clip, seconds, lifetime, -1.0, style.end_effect});
}

void UnitAnimator::add_cue(const EntityId entity, const Vector3& position, const VisualClip& clip, const double from, const double to, const double loop_frames, const std::size_t instance)
{
    const auto index = static_cast<std::uint32_t>(instance);
    if (to > from)
    {
        cues_.push_back({entity, position, clip.path, from, to, index});
    }
    else if (to < from && loop_frames > 0.0)
    {
        // The loop wrapped: to its end, then from its start.
        cues_.push_back({entity, position, clip.path, from, loop_frames, index});
        cues_.push_back({entity, position, clip.path, -1.0, to, index});
    }
}

std::optional<Vector3> UnitAnimator::bone_position(const AnimationCue& cue, const std::string_view bone, const std::span<const VisualInstance> instances, const std::span<const Matrix4> palette) const
{
    return bone_position(static_cast<std::size_t>(cue.instance), bone, instances, palette);
}

std::optional<Vector3> UnitAnimator::bone_position(const std::size_t index, const std::string_view bone, const std::span<const VisualInstance> instances, const std::span<const Matrix4> palette) const
{
    const auto transform = bone_transform(index, bone, instances, palette);
    return transform ? std::optional<Vector3>(translation(*transform)) : std::nullopt;
}

std::optional<Matrix4> UnitAnimator::bone_transform(const std::size_t index, const std::string_view bone, const std::span<const VisualInstance> instances, const std::span<const Matrix4> palette) const
{
    if (index >= instances.size())
    {
        return std::nullopt;
    }
    const auto& instance = instances[index];
    if (instance.mesh >= rigs_.size() || !rigs_[instance.mesh].skeleton)
    {
        return std::nullopt;
    }
    const auto& skeleton = *rigs_[instance.mesh].skeleton;
    for (std::size_t bone_index = 0; bone_index < skeleton.bones.size(); ++bone_index)
    {
        if (!ascii_equal_ignoring_case(skeleton.bones[bone_index].name, bone))
        {
            continue;
        }
        // The bone's model-space pose: its bind pose through its skinning matrix (as attached
        // effects place theirs); its rest pose when the palette does not reach it.
        const auto bind = skeleton_bind_pose(skeleton);
        const auto skin = static_cast<std::size_t>(instance.bone_offset) + bone_index;
        const auto model = skin < palette.size() ? bind[bone_index] * palette[skin] : bind[bone_index];
        return model * instance.transform;
    }
    return std::nullopt;
}

const std::vector<AnimationCue>& UnitAnimator::cues() const noexcept
{
    return cues_;
}

const std::vector<UnitAnimator::EndedCorpse>& UnitAnimator::ended_corpses() const noexcept
{
    return ended_corpses_;
}

void UnitAnimator::animate(const std::span<const EntitySnapshot> snapshot, std::vector<EntityId>& entities, std::vector<VisualInstance>& instances, const double seconds, const std::size_t max_palette,
                           std::vector<Matrix4>& palette, const std::span<const AnimationProfile> animations)
{
    EAW_ASSERT_MSG(entities.size() == instances.size(), "unit animation needs one entity slot per instance");
    ++frame_;
    stats_ = {};
    cues_.clear();
    palette.assign(rest_palette_.begin(), rest_palette_.end());
    frame_offsets_.clear();
    snapshot_index_.clear();
    for (std::size_t index = 0; index < snapshot.size(); ++index)
    {
        snapshot_index_.emplace(entity_key(snapshot[index].id), index);
    }
    if (cached_entries_ > pose_cache_limit)
    {
        pose_cache_.clear();
        cached_entries_ = 0;
    }
    for (std::size_t index = 0; index < instances.size(); ++index)
    {
        auto& instance = instances[index];
        if (instance.mesh >= rigs_.size())
        {
            continue;
        }
        const auto& rig = rigs_[instance.mesh];
        instance.bone_offset = rig.rest_offset;
        if (!entities[index].valid() || !rig.skeleton)
        {
            continue;
        }
        const auto masked = bone_masks_.find(entity_key(entities[index]));
        const auto mask = masked != bone_masks_.end() && masked->second.mesh == instance.mesh ? masked->second.hash : 0U;
        if (rig.clips.empty())
        {
            // No clips: the rest pose, its own only with a mask (destroyed hardpoints).
            PoseKey rest{instance.mesh, {}};
            rest.layers[0] = {rest_clip, 0, 8U};
            rest.mask = mask;
            if (const auto offset = mask != 0 ? place(rest, max_palette, palette) : std::nullopt)
            {
                instance.bone_offset = *offset;
            }
            continue;
        }
        const auto found = snapshot_index_.find(entity_key(entities[index]));
        if (found == snapshot_index_.end())
        {
            continue;
        }
        const auto& entity = snapshot[found->second];
        const auto key = entity_key(entity.id);
        const auto wanted = wanted_animation_role(entity);
        const auto profile = index < animations.size() ? animations[index] : AnimationProfile{};
        auto& state = states_[key];
        if (state.frame == 0 || state.mesh != instance.mesh)
        {
            state = {};
            state.mesh = instance.mesh;
            state.idle = choose_clip(instance.mesh, AnimationRole::idle, entity.id);
            if (const auto first = find_clip(instance.mesh, AnimationRole::idle, 0U); profile.loop_first_idle && first >= 0)
            {
                state.idle = first;
            }
            state.walk = choose_clip(instance.mesh, AnimationRole::walk_move, entity.id);
            state.move = choose_clip(instance.mesh, AnimationRole::move, entity.id);
            state.attack = choose_clip(instance.mesh, AnimationRole::attack, entity.id);
            state.attack_idle = choose_clip(instance.mesh, AnimationRole::attack_idle, entity.id);
            state.last_shot = entity.last_shot_tick;
            state.display_speed = std::isfinite(entity.speed) ? entity.speed : 0.0F;
            state.idle_start = seconds - unit_fraction(key, 0x1d1eULL) * 60.0;
            state.stride_phase = unit_fraction(key, 0x57e9ULL);
            state.attack_weight = wanted == AnimationRole::attack && state.attack_idle >= 0 ? 1.0F : 0.0F;
            state.last_seconds = seconds;
        }
        const auto dt = std::clamp(seconds - state.last_seconds, 0.0, longest_frame_seconds);
        state.last_seconds = seconds;
        // An idle that ended is followed by a random IDLE variant (AnimationProfile::loop_first_idle);
        // a unit seen for the first time may be several idles in.
        for (std::size_t played = 0; state.idle >= 0 && !profile.loop_first_idle && played < maximum_idle_switches; ++played)
        {
            const auto length = static_cast<double>(animation_duration(*rigs_[instance.mesh].clips[static_cast<std::size_t>(state.idle)].animation)) / static_cast<double>(profile.idle_rate);
            if (length <= 0.0 || seconds - state.idle_start < length)
            {
                break;
            }
            state.idle_start += length;
            state.idle = choose_clip(instance.mesh, AnimationRole::idle, entity.id, ++state.idle_plays * idle_variant_salt);
            if (std::ranges::count(rigs_[instance.mesh].clips, AnimationRole::idle, &VisualClip::role) == 1)
            {
                // The lone idle starts again after a pause, holding its first frame until then.
                const auto pause = unit_fraction(key, state.idle_plays * idle_variant_salt);
                state.idle_start += single_idle_pause_min_seconds + pause * (single_idle_pause_max_seconds - single_idle_pause_min_seconds);
            }
        }
        state.frame = frame_;
        const auto target_speed = std::isfinite(entity.speed) ? std::max(entity.speed, 0.0F) : 0.0F;
        state.display_speed += (target_speed - state.display_speed) * std::min(1.0F, static_cast<float>(dt) / speed_smoothing_seconds);

        // Locomotion: two neighbouring clips by speed, falling back to whatever the mesh has.
        const auto blend = locomotion_blend(state.display_speed, entity.max_speed, state.walk >= 0);
        const auto clip_of = [&](const AnimationRole role)
        {
            switch (role)
            {
            case AnimationRole::walk_move:
                return state.walk;
            case AnimationRole::move:
                if (state.move < 0 && state.walk < 0 && profile.idle_when_moving)
                {
                    return state.idle >= 0 ? state.idle : rest_clip;
                }
                return state.move >= 0 ? state.move : state.walk;
            default:
                return state.idle >= 0 ? state.idle : rest_clip;
            }
        };
        const auto lower = clip_of(blend.lower);
        const auto upper = clip_of(blend.upper);
        // The stride phase advances at the blended rate of the moving clips, each sped up or slowed
        // down by how fast the unit goes against the speed the clip stands for: the engine's
        // reference (top speed, or its walk speed) within the playback bounds, rescaled to the
        // type's own reference speed when its XML gives one (AnimationProfile).
        if (entity.max_speed > 0.0F)
        {
            double rate = 0.0;
            const auto stride_rate = [&](const AnimationRole role, const std::int32_t clip, const float weight)
            {
                if (clip < 0 || weight <= 0.0F || role == AnimationRole::idle)
                {
                    return;
                }
                const bool walking = role == AnimationRole::walk_move;
                const auto engine_reference = walking ? walk_speed_fraction : 1.0F;
                const auto type_reference = walking ? (profile.walk_reference > 0.0F ? profile.walk_reference : walk_speed_fraction) : profile.move_reference;
                const auto playback = std::clamp(state.display_speed / (entity.max_speed * engine_reference), minimum_playback_rate, maximum_playback_rate) * engine_reference / type_reference;
                rate += static_cast<double>(weight * playback) / static_cast<double>(animation_duration(*rigs_[instance.mesh].clips[static_cast<std::size_t>(clip)].animation));
            };
            stride_rate(blend.lower, lower, 1.0F - blend.blend);
            stride_rate(blend.upper, upper, blend.blend);
            state.stride_phase = std::fmod(state.stride_phase + rate * dt, 1.0);
        }

        // Aiming (ATTACKIDLE) fades in while standing with a target, and out otherwise. A mesh without
        // it shows no aiming pose: its ATTACK belongs to the shots (the T4-B's gun recoil looped
        // while it had a target, in range or not; the owner's report).
        const auto aim_clip = state.attack_idle;
        const auto attack_target = wanted == AnimationRole::attack && aim_clip >= 0 ? 1.0F : 0.0F;
        const auto fade = static_cast<float>(dt) / attack_fade_seconds;
        state.attack_weight = attack_target > state.attack_weight ? std::min(attack_target, state.attack_weight + fade) : std::max(attack_target, state.attack_weight - fade);
        state.attack_time = state.attack_weight > 0.0F ? state.attack_time + dt : 0.0;
        // Each new shot restarts ATTACK.
        const bool new_shot = entity.last_shot_tick > state.last_shot;
        if (new_shot)
        {
            state.last_shot = entity.last_shot_tick;
            state.since_shot = 0.0;
        }
        else
        {
            state.since_shot += dt;
        }
        float fire = 0.0F;
        double fire_time = 0.0;
        if (state.attack >= 0)
        {
            const auto duration = static_cast<double>(animation_duration(*rigs_[instance.mesh].clips[static_cast<std::size_t>(state.attack)].animation));
            fire = state.since_shot < duration ? 1.0F : static_cast<float>(std::clamp(1.0 - (state.since_shot - duration) / shot_fade_seconds, 0.0, 1.0));
            fire_time = std::min(state.since_shot, duration * 0.9999);
        }

        const auto layer = [&](const std::int32_t clip, const AnimationRole role, const float weight) -> Layer
        {
            const auto amount = eighths(weight);
            if (clip == no_clip || amount == 0)
            {
                return {};
            }
            if (clip == rest_clip)
            {
                return {rest_clip, 0, amount};
            }
            const auto duration = static_cast<double>(animation_duration(*rigs_[instance.mesh].clips[static_cast<std::size_t>(clip)].animation));
            const auto time = role == AnimationRole::idle ? (seconds - state.idle_start) * static_cast<double>(profile.idle_rate) : state.stride_phase * duration;
            return {clip, slot_of(instance.mesh, clip, time), amount};
        };
        const auto fire_eighths = eighths(fire);
        const auto aim_eighths = std::min(eighths(state.attack_weight * (1.0F - fire)), 8U - fire_eighths);
        const auto locomotion = static_cast<float>(8U - fire_eighths - aim_eighths) / 8.0F;

        // Events placed in the clips: the locomotion clip shown most, and ATTACK while it plays.
        const auto position = translation(instance.transform);
        {
            const auto lower_weight = locomotion * (1.0F - blend.blend);
            const auto upper_weight = upper == lower ? locomotion : locomotion * blend.blend;
            const bool upper_leads = upper != lower && upper_weight > lower_weight;
            const auto clip = upper_leads ? upper : lower;
            const auto role = upper_leads ? blend.upper : blend.lower;
            const auto weight = upper == lower ? locomotion : std::max(lower_weight, upper_weight);
            if (clip >= 0 && weight >= animation_cue_minimum_weight)
            {
                const auto& visual_clip = rigs_[instance.mesh].clips[static_cast<std::size_t>(clip)];
                const auto duration = static_cast<double>(animation_duration(*visual_clip.animation));
                const auto time = role == AnimationRole::idle ? std::fmod(std::max(seconds - state.idle_start, 0.0) * static_cast<double>(profile.idle_rate), std::max(duration, 1.0e-6))
                                                              : state.stride_phase * duration;
                const auto frame = time * static_cast<double>(visual_clip.animation->frames_per_second);
                if (clip == state.cue_clip)
                {
                    add_cue(entity.id, position, visual_clip, state.cue_frame, frame, static_cast<double>(visual_clip.animation->loop_frame_count), index);
                }
                state.cue_clip = clip;
                state.cue_frame = frame;
            }
            else
            {
                state.cue_clip = no_clip;
            }
        }
        if (fire_eighths > 0)
        {
            const auto& visual_clip = rigs_[instance.mesh].clips[static_cast<std::size_t>(state.attack)];
            const auto frame = fire_time * static_cast<double>(visual_clip.animation->frames_per_second);
            // A new shot restarts ATTACK: its events from the first frame on.
            const auto from = new_shot ? -1.0 : state.fire_cue_frame;
            add_cue(entity.id, position, visual_clip, from, frame, 0.0, index);
            state.fire_cue_frame = frame;
        }
        else
        {
            state.fire_cue_frame = -1.0;
        }
        // Walkers' transitions: turning in place plays the turn loop (after its BEGIN clip, and its END
        // clip once the turn stops, with Has_Pre_Turn_Anim); starting and stopping a walk play
        // MOVESTART and the MOVE_END clip of the stride's quarter (Walk_Transition). A one-shot
        // transition replaces locomotion until it ends.
        std::int32_t override_clip = -1;
        double override_time = 0.0;
        // How much of the locomotion the override takes (transitions fade in and out).
        float override_weight = 1.0F;
        {
            const auto rate = dt > 0.0 ? std::remainder(entity.heading - state.last_heading, two_pi) / static_cast<float>(dt) : 0.0F;
            const bool slow = state.display_speed < entity.max_speed * turn_in_place_speed_fraction;
            const int turning = slow && std::fabs(rate) > turn_in_place_rate ? (rate > 0.0F ? 1 : -1) : 0;
            // Moving by the shown speed, not the order: a walker turning in place before it sets off has
            // not started walking yet, and MOVESTART belongs to when it does.
            const bool moving = !slow;
            // Walk transitions play at the move clip's rate at top speed (the type's reference speed
            // against its top speed), turns at the turn rate against the type's turn reference.
            const auto walk_playback = std::clamp(1.0F / std::max(profile.move_reference, 1.0e-3F), minimum_transition_playback, maximum_transition_playback);
            const auto turn_playback = profile.turn_reference > 0.0F ? std::clamp(std::fabs(rate) / profile.turn_reference, minimum_transition_playback, maximum_transition_playback) : 1.0F;
            if (turning != 0)
            {
                state.turn_playback = turn_playback;
            }
            const auto start_transition = [&](const std::int32_t clip, const float playback, const bool starts_walk = false)
            {
                if (clip >= 0)
                {
                    state.transition = clip;
                    state.transition_start = seconds;
                    state.transition_playback = playback;
                    state.transition_starts_walk = starts_walk;
                    state.fading = -1;
                }
            };
            if (state.frame_seen)
            {
                if (profile.walk_transition && moving && !state.was_moving)
                {
                    start_transition(find_clip(instance.mesh, AnimationRole::move_start, 0), walk_playback, true);
                }
                else if (profile.walk_transition && !moving && state.was_moving)
                {
                    start_transition(find_clip(instance.mesh, AnimationRole::move_end, static_cast<std::uint32_t>(std::clamp(state.stride_phase * 4.0, 0.0, 3.0))), walk_playback);
                }
                else if (profile.pre_turn && turning != 0 && state.turning == 0)
                {
                    start_transition(find_clip(instance.mesh, turning > 0 ? AnimationRole::turn_left_begin : AnimationRole::turn_right_begin, 0), state.turn_playback);
                }
                else if (profile.pre_turn && turning == 0 && state.turning != 0)
                {
                    start_transition(find_clip(instance.mesh, state.turning > 0 ? AnimationRole::turn_left_end : AnimationRole::turn_right_end, 0), state.turn_playback);
                }
            }
            if (turning != 0 && turning != state.turning)
            {
                state.turn_time = 0.0;
            }
            else if (turning != 0)
            {
                state.turn_time += dt * static_cast<double>(state.turn_playback);
            }
            state.turning = turning;
            state.was_moving = moving;
            state.last_heading = entity.heading;
            state.frame_seen = true;
            if (state.transition >= 0)
            {
                const auto elapsed = (seconds - state.transition_start) * static_cast<double>(state.transition_playback);
                const auto duration = static_cast<double>(animation_duration(*rigs_[instance.mesh].clips[static_cast<std::size_t>(state.transition)].animation));
                if (elapsed < duration)
                {
                    override_clip = state.transition;
                    override_time = elapsed;
                    override_weight = static_cast<float>(std::clamp((seconds - state.transition_start) / transition_fade_seconds, 0.0, 1.0));
                    // MOVE goes on from where MOVESTART ends: its first frame.
                    if (state.transition_starts_walk)
                    {
                        state.stride_phase = 0.0;
                    }
                }
                else
                {
                    state.fading = state.transition;
                    state.fading_since = state.transition_start + duration / static_cast<double>(state.transition_playback);
                    state.transition = -1;
                }
            }
            // An ended transition holds its last frame while the next pose fades in.
            if (override_clip < 0 && state.fading >= 0)
            {
                const auto since = seconds - state.fading_since;
                if (since < transition_fade_seconds && turning == 0)
                {
                    const auto duration = static_cast<double>(animation_duration(*rigs_[instance.mesh].clips[static_cast<std::size_t>(state.fading)].animation));
                    override_clip = state.fading;
                    override_time = duration * 0.9999;
                    override_weight = static_cast<float>(1.0 - std::clamp(since / transition_fade_seconds, 0.0, 1.0));
                }
                else
                {
                    state.fading = -1;
                }
            }
            if (override_clip < 0 && turning != 0)
            {
                const auto loop = find_clip(instance.mesh, turning > 0 ? AnimationRole::turn_left : AnimationRole::turn_right, 0);
                if (loop >= 0)
                {
                    override_clip = loop;
                    override_time = state.turn_time;
                    override_weight = 1.0F;
                }
            }
        }
        PoseKey pose_key{instance.mesh, {}};
        pose_key.mask = mask;
        pose_key.layers[0] = layer(lower, blend.lower, locomotion * (1.0F - blend.blend));
        pose_key.layers[1] = upper == lower ? Layer{} : layer(upper, blend.upper, locomotion * blend.blend);
        if (upper == lower)
        {
            pose_key.layers[0] = layer(lower, blend.lower, locomotion);
        }
        if (override_clip >= 0)
        {
            // While it fades, the override shares the locomotion with the clip that leads it.
            const auto override_eighths = eighths(locomotion * override_weight);
            const auto rest = eighths(locomotion) > override_eighths ? eighths(locomotion) - override_eighths : 0U;
            const bool upper_leads = upper != lower && blend.blend > 0.5F;
            const auto leading = upper_leads ? upper : lower;
            pose_key.layers[0] = rest > 0U ? layer(leading, upper_leads ? blend.upper : blend.lower, static_cast<float>(rest) / 8.0F) : Layer{};
            pose_key.layers[1] = {override_clip, slot_of(instance.mesh, override_clip, override_time), override_eighths};
        }
        if (aim_eighths > 0)
        {
            pose_key.layers[2] = {aim_clip, slot_of(instance.mesh, aim_clip, state.attack_time), aim_eighths};
        }
        if (fire_eighths > 0)
        {
            pose_key.layers[3] = {state.attack, slot_of(instance.mesh, state.attack, fire_time), fire_eighths};
        }
        const auto layers = std::ranges::count_if(pose_key.layers,
                                                  [](const Layer& value)
                                                  {
                                                      return value.clip != no_clip;
                                                  });
        // A turret turns toward the unit's target at its rate, and back to rest without one.
        if (profile.turret_bone != 0)
        {
            pose_key.turret = bone_of(instance.mesh, profile.turret_bone);
            pose_key.barrel = bone_of(instance.mesh, profile.barrel_bone);
        }
        if (pose_key.turret >= 0)
        {
            float want_pitch = 0.0F;
            if (const auto aimed = entity.target.valid() ? snapshot_index_.find(entity_key(entity.target)) : snapshot_index_.end(); aimed != snapshot_index_.end())
            {
                const auto& to = snapshot[aimed->second].position;
                const auto dx = to.x - entity.position.x;
                const auto dy = to.y - entity.position.y;
                want_pitch = std::atan2(to.z - entity.position.z, std::max(std::hypot(dx, dy), 1.0F));
            }
            want_pitch = std::clamp(want_pitch, -profile.turret_pitch_limit, profile.turret_pitch_limit);
            const auto turn = profile.turret_rate * static_cast<float>(dt);
            // The simulation's turret direction: what is drawn is what fires.
            state.turret_yaw = entity.turret_yaw;
            state.barrel_pitch += std::clamp(want_pitch - state.barrel_pitch, -turn, turn);
            pose_key.yaw = static_cast<std::int16_t>(std::lround(state.turret_yaw / turret_yaw_step));
            pose_key.pitch = pose_key.barrel >= 0 ? static_cast<std::int16_t>(std::lround(state.barrel_pitch / barrel_pitch_step)) : std::int16_t{0};
        }
        const bool aimed = pose_key.yaw != 0 || pose_key.pitch != 0;
        if (!aimed)
        {
            // Unturned turrets share the plain poses.
            pose_key.turret = -1;
            pose_key.barrel = -1;
        }
        // Nothing to show but the rest pose: keep the rest offset (an aimed turret or a mask needs its
        // own).
        if (!aimed && mask == 0 && (layers == 0 || (layers == 1 && pose_key.layers[0].clip == rest_clip)))
        {
            continue;
        }
        if (layers == 0)
        {
            pose_key.layers[0] = {rest_clip, 0, 8U};
        }
        if (const auto offset = place(pose_key, max_palette, palette))
        {
            instance.bone_offset = *offset;
            ++stats_.animated;
            stats_.blending += layers > 1 ? 1U : 0U;
        }
    }
    // Corpses: their DIE clip once, then its last frame until they are removed.
    ended_corpses_.clear();
    std::erase_if(corpses_,
                  [&](const Corpse& corpse)
                  {
                      if (seconds - corpse.died <= corpse.lifetime)
                      {
                          return false;
                      }
                      if (corpse.end_effect != 0)
                      {
                          ended_corpses_.push_back({translation(corpse.instance.transform), corpse.end_effect});
                      }
                      return true;
                  });
    for (auto& corpse : corpses_)
    {
        if (corpse.clip < 0)
        {
            auto instance = corpse.instance;
            instance.bone_offset = rigs_[instance.mesh].rest_offset;
            instances.push_back(instance);
            entities.emplace_back();
            ++stats_.corpses;
            continue;
        }
        const auto& die_clip = rigs_[corpse.instance.mesh].clips[static_cast<std::size_t>(corpse.clip)];
        const auto duration = static_cast<double>(animation_duration(*die_clip.animation));
        const auto time = std::clamp(seconds - corpse.died, 0.0, duration * 0.9999);
        const auto cue_frame = time * static_cast<double>(die_clip.animation->frames_per_second);
        add_cue({}, translation(corpse.instance.transform), die_clip, corpse.cue_frame, cue_frame, 0.0, instances.size());
        corpse.cue_frame = cue_frame;
        PoseKey key{corpse.instance.mesh, {}};
        key.layers[0] = {corpse.clip, slot_of(corpse.instance.mesh, corpse.clip, time), 8U};
        auto instance = corpse.instance;
        instance.bone_offset = rigs_[instance.mesh].rest_offset;
        if (const auto offset = place(key, max_palette, palette))
        {
            instance.bone_offset = *offset;
        }
        instances.push_back(instance);
        entities.emplace_back();
        ++stats_.corpses;
    }
    // Entities not drawn this frame forget their animation state.
    std::erase_if(states_,
                  [&](const auto& entry)
                  {
                      return entry.second.frame != frame_;
                  });
    stats_.poses = frame_offsets_.size();
    stats_.palette_entries = palette.size();
}

} // namespace eaw
