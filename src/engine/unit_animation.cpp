#include "assets/pose.hpp"
#include "unit_animation.hpp"

#include "scene_presentation.hpp"
#include "assets/animation.hpp"
#include "math/matrix.hpp"

#include <algorithm>
#include <cmath>

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
// Frames longer than this (a stall, a hidden window) count as this long.
constexpr double longest_frame_seconds = 0.25;
// Sampled poses kept across frames (matrices); beyond it the cache starts over.
constexpr std::size_t pose_cache_limit = 4U * 1024U * 1024U;

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
    return static_cast<std::size_t>(hash);
}

std::int32_t UnitAnimator::choose_clip(const std::uint32_t mesh, const AnimationRole role, const EntityId id) const
{
    const auto& clips = rigs_[mesh].clips;
    const auto first = std::ranges::find(clips, role, &VisualClip::role);
    if (first == clips.end())
    {
        return -1;
    }
    const auto count = static_cast<std::uint64_t>(std::ranges::count(clips, role, &VisualClip::role));
    return static_cast<std::int32_t>(first - clips.begin()) + static_cast<std::int32_t>(mix(entity_key(id)) % count);
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
    append_rig_local_palette(rig, pose, out);
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
    if (instance.mesh >= rigs_.size() || !rigs_[instance.mesh].skeleton)
    {
        return;
    }
    auto clip = choose_clip(instance.mesh, AnimationRole::die, id);
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
        return;
    }
    if (corpses_.size() >= maximum_corpses)
    {
        corpses_.erase(corpses_.begin());
    }
    const auto duration = static_cast<double>(animation_duration(*rigs_[instance.mesh].clips[static_cast<std::size_t>(clip)].animation));
    const auto lifetime = style.persistence_seconds ? std::max(static_cast<double>(*style.persistence_seconds), duration) : duration + corpse_linger_seconds;
    corpses_.push_back({instance, clip, seconds, lifetime});
}

void UnitAnimator::add_cue(const EntityId entity, const Vector3& position, const VisualClip& clip, const double from, const double to, const double loop_frames)
{
    if (to > from)
    {
        cues_.push_back({entity, position, clip.path, from, to});
    }
    else if (to < from && loop_frames > 0.0)
    {
        // The loop wrapped: to its end, then from its start.
        cues_.push_back({entity, position, clip.path, from, loop_frames});
        cues_.push_back({entity, position, clip.path, -1.0, to});
    }
}

const std::vector<AnimationCue>& UnitAnimator::cues() const noexcept
{
    return cues_;
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
        if (!entities[index].valid() || rig.clips.empty() || !rig.skeleton)
        {
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
            state.walk = choose_clip(instance.mesh, AnimationRole::walk_move, entity.id);
            state.move = choose_clip(instance.mesh, AnimationRole::move, entity.id);
            state.attack = choose_clip(instance.mesh, AnimationRole::attack, entity.id);
            state.attack_idle = choose_clip(instance.mesh, AnimationRole::attack_idle, entity.id);
            state.last_shot = entity.last_shot_tick;
            state.display_speed = std::isfinite(entity.speed) ? entity.speed : 0.0F;
            state.idle_start = seconds - unit_fraction(key, 0x1d1eULL) * 60.0;
            state.stride_phase = unit_fraction(key, 0x57e9ULL);
            state.attack_weight = wanted == AnimationRole::attack && (state.attack >= 0 || state.attack_idle >= 0) ? 1.0F : 0.0F;
            state.last_seconds = seconds;
        }
        const auto dt = std::clamp(seconds - state.last_seconds, 0.0, longest_frame_seconds);
        state.last_seconds = seconds;
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
        // down by how fast the unit goes against the speed the clip stands for.
        if (entity.max_speed > 0.0F)
        {
            double rate = 0.0;
            const auto stride_rate = [&](const AnimationRole role, const std::int32_t clip, const float weight)
            {
                if (clip < 0 || weight <= 0.0F || role == AnimationRole::idle)
                {
                    return;
                }
                const auto reference = role == AnimationRole::walk_move ? entity.max_speed * walk_speed_fraction : entity.max_speed;
                const auto playback =
                    std::clamp(state.display_speed / reference, minimum_playback_rate, maximum_playback_rate) * (role == AnimationRole::walk_move ? profile.walk_rate : profile.move_rate);
                rate += static_cast<double>(weight * playback) / static_cast<double>(animation_duration(*rigs_[instance.mesh].clips[static_cast<std::size_t>(clip)].animation));
            };
            stride_rate(blend.lower, lower, 1.0F - blend.blend);
            stride_rate(blend.upper, upper, blend.blend);
            state.stride_phase = std::fmod(state.stride_phase + rate * dt, 1.0);
        }

        // Aiming fades in while standing with a target, and out otherwise.
        const auto aim_clip = state.attack_idle >= 0 ? state.attack_idle : state.attack;
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
        if (state.attack >= 0 && state.attack_idle >= 0)
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
                    add_cue(entity.id, position, visual_clip, state.cue_frame, frame, static_cast<double>(visual_clip.animation->loop_frame_count));
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
            add_cue(entity.id, position, visual_clip, from, frame, 0.0);
            state.fire_cue_frame = frame;
        }
        else
        {
            state.fire_cue_frame = -1.0;
        }
        PoseKey pose_key{instance.mesh, {}};
        pose_key.layers[0] = layer(lower, blend.lower, locomotion * (1.0F - blend.blend));
        pose_key.layers[1] = upper == lower ? Layer{} : layer(upper, blend.upper, locomotion * blend.blend);
        if (upper == lower)
        {
            pose_key.layers[0] = layer(lower, blend.lower, locomotion);
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
        // Nothing to show but the rest pose: keep the rest offset.
        if (layers == 0 || (layers == 1 && pose_key.layers[0].clip == rest_clip))
        {
            continue;
        }
        if (const auto offset = place(pose_key, max_palette, palette))
        {
            instance.bone_offset = *offset;
            ++stats_.animated;
            stats_.blending += layers > 1 ? 1U : 0U;
        }
    }
    // Corpses: their DIE clip once, then its last frame until they are removed.
    std::erase_if(corpses_,
                  [&](const Corpse& corpse)
                  {
                      return seconds - corpse.died > corpse.lifetime;
                  });
    for (auto& corpse : corpses_)
    {
        const auto& die_clip = rigs_[corpse.instance.mesh].clips[static_cast<std::size_t>(corpse.clip)];
        const auto duration = static_cast<double>(animation_duration(*die_clip.animation));
        const auto time = std::clamp(seconds - corpse.died, 0.0, duration * 0.9999);
        const auto cue_frame = time * static_cast<double>(die_clip.animation->frames_per_second);
        add_cue({}, translation(corpse.instance.transform), die_clip, corpse.cue_frame, cue_frame, 0.0);
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
