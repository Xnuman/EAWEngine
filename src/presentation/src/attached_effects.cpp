#include "presentation/attached_effects.hpp"

#include "assets/skeleton.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {
namespace {

std::uint64_t key_of(const EntityId entity, const std::size_t attachment)
{
    // Generations wrap at 16 bits here; an aliased key only restarts an effect.
    return (static_cast<std::uint64_t>(entity.index) << 32U) | (static_cast<std::uint64_t>(entity.generation & 0xffffU) << 16U) | (attachment & 0xffffU);
}

} // namespace

void AttachedEffects::set_rigs(const std::span<const VisualRig> rigs)
{
    meshes_.clear();
    add_rigs(rigs);
}

void AttachedEffects::add_rigs(const std::span<const VisualRig> rigs)
{
    for (const auto& rig : rigs)
    {
        MeshEffects mesh;
        if (rig.skeleton)
        {
            const auto bind_pose = skeleton_bind_pose(*rig.skeleton);
            mesh.bone_count = static_cast<std::uint32_t>(rig.skeleton->bones.size());
            mesh.visibility_half = mesh.bone_count != 0 && rig.palette_size >= mesh.bone_count * 2U;
            for (const auto& attachment : rig.effects)
            {
                // Hardpoint damage effects hang under bones hidden until the hardpoint is destroyed
                // (UnitAnimator::destroy_hardpoint): the visibility half shows them then.
                if (attachment.effect && attachment.bone < bind_pose.size())
                {
                    mesh.attachments.push_back({attachment.effect, attachment.emit_mesh, attachment.bone, bind_pose[attachment.bone], attachment.engine, attachment.state});
                }
            }
        }
        meshes_.push_back(std::move(mesh));
    }
}

void AttachedEffects::update(ParticleWorld& world, const std::span<const VisualInstance> instances, const std::span<const EntityId> entities, const std::span<const Matrix4> palette,
                             const std::span<const EntitySnapshot> snapshot, const double seconds)
{
    ++frame_;
    const auto elapsed = static_cast<float>(std::clamp(seconds - last_seconds_, 0.0, 0.25));
    last_seconds_ = seconds;
    // Speed fractions this frame, by entity key, smoothed toward the target.
    constexpr float smoothing_seconds = 0.5F;
    const auto blend = 1.0F - std::exp(-elapsed / smoothing_seconds);
    for (const auto& entity : snapshot)
    {
        if (entity.max_speed <= 0.0F)
        {
            continue;
        }
        const auto key = (static_cast<std::uint64_t>(entity.id.index) << 32U) | entity.id.generation;
        const auto target = std::clamp(entity.speed / entity.max_speed, 0.0F, 1.0F);
        auto [entry, inserted] = power_.try_emplace(key, Power{target, frame_});
        if (!inserted)
        {
            entry->second.value += (target - entry->second.value) * blend;
            entry->second.frame = frame_;
        }
    }
    std::erase_if(power_,
                  [&](const auto& entry)
                  {
                      return entry.second.frame != frame_;
                  });
    for (std::size_t index = 0; index < instances.size() && index < entities.size(); ++index)
    {
        const auto& instance = instances[index];
        const auto entity = entities[index];
        if (!entity.valid() || instance.mesh >= meshes_.size())
        {
            continue;
        }
        const auto& mesh = meshes_[instance.mesh];
        for (std::size_t a = 0; a < mesh.attachments.size(); ++a)
        {
            const auto& attachment = mesh.attachments[a];
            const auto skin = static_cast<std::size_t>(instance.bone_offset) + attachment.bone;
            auto bone = attachment.bind;
            // An effect of another damage state stops emitting; its particles live out.
            bool shown = !attachment.state || *attachment.state == instance.state;
            if (skin < palette.size())
            {
                bone = attachment.bind * palette[skin];
            }
            if (mesh.visibility_half)
            {
                // A bone the pose hides collapses to a point in the visibility half.
                const auto visibility = skin + mesh.bone_count;
                if (visibility < palette.size())
                {
                    const auto& m = palette[visibility].values;
                    shown = shown && (m[0] != 0.0F || m[5] != 0.0F || m[10] != 0.0F);
                }
            }
            // The instance's uniform scale (a type's Scale_Factor) sizes the effect, particles
            // included: it is taken out of the transform and given to the particle world.
            auto world_transform = bone * instance.transform;
            const auto effect_scale = without_uniform_scale(instance.transform).scale;
            for (std::size_t element = 0; element < 12; ++element)
            {
                world_transform.values[element] /= effect_scale;
            }
            const auto key = key_of(entity, a);
            auto found = playing_.find(key);
            if (found == playing_.end())
            {
                // A one-shot effect that has ended keeps its entry, so it does not start again.
                if (!shown)
                {
                    continue;
                }
                const auto seed = key * 0x9e3779b97f4a7c15ULL + 1U;
                const auto id = world.play(attachment.effect, world_transform, seed, attachment.emit_mesh, effect_scale);
                if (!id.valid())
                {
                    continue;
                }
                found = playing_.insert_or_assign(key, Playing{id, frame_}).first;
            }
            world.set_transform(found->second.instance, world_transform);
            world.set_emitting(found->second.instance, shown);
            if (attachment.engine)
            {
                // Immobile entities and those without a snapshot keep full intensity.
                const auto power = power_.find((static_cast<std::uint64_t>(entity.index) << 32U) | entity.generation);
                world.set_intensity(found->second.instance, power != power_.end() ? power->second.value : 1.0F);
            }
            found->second.frame = frame_;
        }
    }
    // Entities no longer drawn (destroyed, or their attachment gone) stop emitting.
    for (auto entry = playing_.begin(); entry != playing_.end();)
    {
        if (entry->second.frame != frame_)
        {
            world.stop(entry->second.instance);
            entry = playing_.erase(entry);
        }
        else
        {
            ++entry;
        }
    }
}

void AttachedEffects::clear(ParticleWorld& world)
{
    for (const auto& [key, entry] : playing_)
    {
        world.remove(entry.instance);
    }
    playing_.clear();
}

} // namespace eaw
