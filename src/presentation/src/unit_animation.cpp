#include "presentation/unit_animation.hpp"

#include "unit_animation_internal.hpp"

#include "math/matrix.hpp"
#include "math/quaternion.hpp"
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

using namespace unit_animation_detail;

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
    hash = mix(hash ^ key.mask ^ (key.overlay ? 0x6f766c79ULL : 0U));
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
    for (std::size_t index = 0; index < key.layers.size(); ++index)
    {
        const auto& layer = key.layers[index];
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
        if (key.overlay && index >= 2 && !pose.empty() && layer.clip >= 0)
        {
            // An overlay adds what its clip does to the pose below it: each bone the clip moves turns,
            // shifts, and scales by its change since the clip's first frame, at the layer's weight (an
            // engine reading of Attack_Animation_Is_Overlay: the AT-AT's and AT-ST's attacks move
            // their barrels only, the pod walker's recoils its whole body, which must not undo its
            // walk).
            const auto& animation = *rig.clips[static_cast<std::size_t>(layer.clip)].animation;
            const auto first = sample_local_pose(animation, *rig.skeleton, 0.0F);
            const auto amount = std::min(weight / 8.0F, 1.0F);
            for (const auto& track : animation.tracks)
            {
                const auto bone = static_cast<std::size_t>(track.bone);
                if (!track.animated || bone >= pose.size() || bone >= layer_pose.size() || bone >= first.size())
                {
                    continue;
                }
                auto& shown = pose[bone];
                const auto turn = slerp(Quaternion::identity(), conjugate(first[bone].rotation) * layer_pose[bone].rotation, amount);
                shown.rotation = normalize_or(shown.rotation * turn, shown.rotation);
                shown.translation = shown.translation + (layer_pose[bone].translation - first[bone].translation) * amount;
                const auto grow = [&](const float from, const float to)
                {
                    return from != 0.0F ? 1.0F + (to / from - 1.0F) * amount : 1.0F;
                };
                shown.scale = {shown.scale.x * grow(first[bone].scale.x, layer_pose[bone].scale.x), shown.scale.y * grow(first[bone].scale.y, layer_pose[bone].scale.y),
                               shown.scale.z * grow(first[bone].scale.z, layer_pose[bone].scale.z)};
            }
            continue;
        }
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

void UnitAnimator::add_frame_cues(const EntityFrame& current, const LocomotionPose& locomotion_pose, const AttackPose& attack)
{
    const auto& entity = current.entity;
    const auto& profile = current.profile;
    auto& state = current.state;
    const auto mesh = current.mesh;
    const auto seconds = current.seconds;
    const auto index = current.index;
    const auto& position = current.position;
    const auto& blend = locomotion_pose.blend;
    const auto lower = locomotion_pose.lower;
    const auto upper = locomotion_pose.upper;
    const auto locomotion = attack.locomotion;
    const auto fire_eighths = attack.fire_eighths;
    const auto fire_time = attack.fire_time;
    const auto new_shot = attack.new_shot;
    // Events placed in the clips: the locomotion clip shown most, and ATTACK while it plays.
    {
        const auto lower_weight = locomotion * (1.0F - blend.blend);
        const auto upper_weight = upper == lower ? locomotion : locomotion * blend.blend;
        const bool upper_leads = upper != lower && upper_weight > lower_weight;
        const auto clip = upper_leads ? upper : lower;
        const auto role = upper_leads ? blend.upper : blend.lower;
        const auto weight = upper == lower ? locomotion : std::max(lower_weight, upper_weight);
        if (clip >= 0 && weight >= animation_cue_minimum_weight)
        {
            const auto& visual_clip = rigs_[mesh].clips[static_cast<std::size_t>(clip)];
            const auto duration = static_cast<double>(animation_duration(*visual_clip.animation));
            const auto time =
                role == AnimationRole::idle ? std::fmod(std::max(seconds - state.idle_start, 0.0) * static_cast<double>(profile.idle_rate), std::max(duration, 1.0e-6)) : state.stride_phase * duration;
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
        const auto& visual_clip = rigs_[mesh].clips[static_cast<std::size_t>(state.attack)];
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
        double dt = 0.0;
        auto& state = prepare_state(entity, profile, instance.mesh, key, wanted, seconds, dt);
        const EntityFrame frame{entity, profile, state, instance.mesh, key, wanted, seconds, dt, index, translation(instance.transform)};
        const auto locomotion_pose = animate_locomotion(frame);
        const auto attack = animate_attack(frame);
        add_frame_cues(frame, locomotion_pose, attack);
        const auto replaced = animate_transitions(frame);
        auto pose_key = compose_pose(frame, locomotion_pose, attack, replaced, mask);
        const auto layers = std::ranges::count_if(pose_key.layers,
                                                  [](const Layer& value)
                                                  {
                                                      return value.clip != no_clip;
                                                  });
        aim_turret(frame, snapshot, pose_key);
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
