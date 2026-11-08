#include "unit_animation.hpp"

#include "scene_presentation.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {
namespace {

// Pose slots per second of a clip at least, so slow clips (15 frames per second) still move
// smoothly; an engine choice.
constexpr float minimum_slots_per_second = 30.0F;
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

bool loops_from_phase(const AnimationRole role) noexcept
{
    return role != AnimationRole::attack;
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

UnitAnimator::UnitAnimator(const std::span<const VisualRig> rigs, const std::span<const std::array<float, 16>> rest_palette)
    : rigs_(rigs.begin(), rigs.end()), rest_palette_(rest_palette.begin(), rest_palette.end())
{}

const UnitAnimator::Stats& UnitAnimator::stats() const noexcept
{
    return stats_;
}

std::optional<std::size_t> UnitAnimator::choose_clip(const std::uint32_t mesh, const AnimationRole wanted, const EntityId id) const
{
    if (mesh >= rigs_.size() || rigs_[mesh].clips.empty())
    {
        return std::nullopt;
    }
    const auto& clips = rigs_[mesh].clips;
    std::array<AnimationRole, 3> order{wanted, wanted, AnimationRole::idle};
    if (wanted == AnimationRole::move)
    {
        order = {AnimationRole::move, AnimationRole::walk_move, AnimationRole::idle};
    }
    else if (wanted == AnimationRole::attack)
    {
        order = {AnimationRole::attack, AnimationRole::attack_idle, AnimationRole::idle};
    }
    for (const auto role : order)
    {
        const auto first = std::ranges::find(clips, role, &VisualClip::role);
        if (first == clips.end())
        {
            continue;
        }
        const auto count = static_cast<std::size_t>(std::ranges::count(clips, role, &VisualClip::role));
        const auto variant = static_cast<std::size_t>(mix(entity_key(id)) % count);
        return static_cast<std::size_t>(first - clips.begin()) + variant;
    }
    return std::nullopt;
}

void UnitAnimator::animate(const std::span<const EntitySnapshot> snapshot, const std::span<const EntityId> entities, const std::span<VisualInstance> instances, const double seconds,
                           const std::size_t max_palette, std::vector<std::array<float, 16>>& palette)
{
    EAW_ASSERT_MSG(entities.size() == instances.size(), "unit animation needs one entity slot per instance");
    ++frame_;
    stats_ = {};
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
        if (!entities[index].valid() || rig.clips.empty())
        {
            continue;
        }
        const auto found = snapshot_index_.find(entity_key(entities[index]));
        if (found == snapshot_index_.end())
        {
            continue;
        }
        const auto& entity = snapshot[found->second];
        const auto wanted = wanted_animation_role(entity);
        const auto clip_index = choose_clip(instance.mesh, wanted, entity.id);
        if (!clip_index)
        {
            continue;
        }
        const auto& clip = rig.clips[*clip_index];
        auto& state = states_[entity_key(entity.id)];
        const auto duration = static_cast<double>(clip.animation->duration_seconds());
        if (state.frame == 0 || state.mesh != instance.mesh || state.clip != *clip_index || state.role != wanted)
        {
            state.mesh = instance.mesh;
            state.role = wanted;
            state.clip = *clip_index;
            const auto phase = loops_from_phase(clip.role) ? static_cast<double>(mix(entity_key(entity.id) ^ 0x5bd1e995ULL) % 1000U) / 1000.0 * duration : 0.0;
            state.start = seconds - phase;
        }
        state.frame = frame_;
        const auto fps = std::max(clip.animation->frames_per_second, 1.0F);
        const auto frames = static_cast<double>(clip.animation->playable_frame_count());
        const auto slots = static_cast<std::uint64_t>(std::max(1.0, std::ceil(frames * std::max(1.0F, std::ceil(minimum_slots_per_second / fps)))));
        const auto position = std::fmod(std::max(seconds - state.start, 0.0), duration) / duration;
        const auto slot = std::min(static_cast<std::uint64_t>(position * static_cast<double>(slots)), slots - 1U);
        const auto key = (static_cast<std::uint64_t>(instance.mesh) << 40U) ^ (static_cast<std::uint64_t>(*clip_index) << 24U) ^ slot;
        if (const auto placed = frame_offsets_.find(key); placed != frame_offsets_.end())
        {
            instance.bone_offset = placed->second;
            ++stats_.animated;
            continue;
        }
        if (palette.size() + rig.palette_size > max_palette)
        {
            ++stats_.over_budget;
            continue;
        }
        auto cached = pose_cache_.find(key);
        if (cached == pose_cache_.end())
        {
            std::vector<std::array<float, 16>> pose;
            pose.reserve(rig.palette_size);
            append_rig_palette(rig, *clip.animation, static_cast<float>(static_cast<double>(slot) / static_cast<double>(slots) * duration), pose);
            cached_entries_ += pose.size();
            cached = pose_cache_.emplace(key, std::move(pose)).first;
            ++stats_.sampled;
        }
        const auto offset = static_cast<std::uint32_t>(palette.size());
        palette.insert(palette.end(), cached->second.begin(), cached->second.end());
        frame_offsets_.emplace(key, offset);
        instance.bone_offset = offset;
        ++stats_.animated;
    }
    // Entities not drawn this frame forget their clip.
    std::erase_if(states_,
                  [&](const auto& entry)
                  {
                      return entry.second.frame != frame_;
                  });
    stats_.poses = frame_offsets_.size();
    stats_.palette_entries = palette.size();
}

} // namespace eaw
