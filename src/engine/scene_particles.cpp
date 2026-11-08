// Particle effects for scene builds: decoding particle ALOs through the asset cache, loading their
// textures, and attaching them to model proxies.

#include "scene_builder_internal.hpp"

#include <algorithm>
#include <filesystem>
#include <unordered_map>

namespace eaw {
namespace scene_build {
namespace {

// The decoded system, or why there is none; `mesh` marks a file that is a model, not a system.
struct CachedParticleSystem
{
    std::optional<ParticleSystem> system;
    std::string error;
    bool mesh{};
};

std::optional<std::string> resolve_effect_path(const AssetReader& vfs, const std::string& reference)
{
    if (auto resolved = resolve_model_path(vfs, reference))
    {
        return resolved;
    }
    // Proxy names carry neither directory nor extension.
    auto name = std::filesystem::path(reference);
    if (!name.has_extension())
    {
        name += ".alo";
    }
    return resolve_model_path(vfs, (std::filesystem::path("Data/Art/Models") / name).generic_string());
}

// The mesh rigidly attached to the proxy bone's parent (the reference's rule for a proxy's mesh), in
// the proxy bone's space; null when there is none.
std::shared_ptr<const ParticleEmitMesh> proxy_emit_mesh(const AloModel& model, const std::uint32_t proxy_bone)
{
    const auto parent = model.bones[proxy_bone].parent;
    if (parent < 0)
    {
        return nullptr;
    }
    const auto bind_pose = model.bind_pose();
    const auto to_proxy = alo_inverse(bind_pose[proxy_bone]);
    if (!to_proxy || static_cast<std::size_t>(parent) >= bind_pose.size())
    {
        return nullptr;
    }
    for (const auto& mesh : model.meshes)
    {
        if (!mesh.attached_bone || *mesh.attached_bone != static_cast<std::uint32_t>(parent))
        {
            continue;
        }
        // Rigid mesh vertices are in their bone's space: to the model, then to the proxy bone.
        const auto transform = alo_multiply(bind_pose[static_cast<std::size_t>(parent)], *to_proxy);
        const auto origin = alo_transform_point({}, transform);
        auto emit = std::make_shared<ParticleEmitMesh>();
        for (const auto& submesh : mesh.submeshes)
        {
            const auto first = static_cast<std::uint32_t>(emit->positions.size());
            for (const auto& vertex : submesh.vertices)
            {
                const auto position = alo_transform_point(vertex.position, transform);
                const auto tip = alo_transform_point(vertex.normal, transform);
                emit->positions.push_back({position.x, position.y, position.z});
                emit->normals.push_back({tip.x - origin.x, tip.y - origin.y, tip.z - origin.z});
            }
            for (std::size_t index = 0; index + 2 < submesh.indices.size(); index += 3)
            {
                if (submesh.indices[index] < submesh.vertices.size() && submesh.indices[index + 1] < submesh.vertices.size() && submesh.indices[index + 2] < submesh.vertices.size())
                {
                    for (std::size_t corner = 0; corner < 3; ++corner)
                    {
                        emit->indices.push_back(first + submesh.indices[index + corner]);
                    }
                }
            }
        }
        return emit->positions.empty() ? nullptr : emit;
    }
    return nullptr;
}

} // namespace

std::shared_ptr<const ParticleEffect> particle_effect(SceneBuilder& builder, const std::string& reference)
{
    const auto key = fold_ascii(reference);
    if (const auto existing = builder.particles.effects.find(key); existing != builder.particles.effects.end())
    {
        return existing->second;
    }
    auto& slot = builder.particles.effects[key];
    const auto path = resolve_effect_path(builder.vfs, reference);
    if (!path)
    {
        builder.scene.warnings.push_back("particle effect " + reference + ": file not found");
        return nullptr;
    }
    const auto loaded = cached<CachedParticleSystem>(builder.cache, "particles", fold_ascii(*path), builder.vfs,
                                                     [&](const AssetReader& reader)
                                                     {
                                                         CachedParticleSystem result;
                                                         const auto bytes = reader.read(*path);
                                                         if (!bytes)
                                                         {
                                                             result.error = bytes.error().message;
                                                             return result;
                                                         }
                                                         if (particle_file_kind(*bytes) == ParticleFileKind::other)
                                                         {
                                                             result.mesh = true;
                                                             return result;
                                                         }
                                                         auto system = ParticleSystem::parse(*bytes);
                                                         if (!system)
                                                         {
                                                             result.error = system.error().message;
                                                             return result;
                                                         }
                                                         result.system = std::move(system).value();
                                                         return result;
                                                     });
    if (!loaded->system)
    {
        if (!loaded->mesh)
        {
            builder.scene.warnings.push_back(*path + ": " + loaded->error);
        }
        return nullptr;
    }
    auto effect = std::make_shared<ParticleEffect>();
    effect->name = std::filesystem::path(*path).stem().string();
    effect->system = *loaded->system;
    for (const auto& warning : effect->system.warnings)
    {
        builder.scene.warnings.push_back(*path + ": " + warning);
    }
    for (const auto& emitter : effect->system.emitters)
    {
        auto index = invalid_visual_texture;
        const auto texture_key = fold_ascii(emitter.texture);
        if (const auto known = builder.particles.texture_indices.find(texture_key); known != builder.particles.texture_indices.end())
        {
            index = known->second;
        }
        else
        {
            const auto texture = cached_texture(builder.vfs, builder.cache, emitter.texture);
            if (texture->texture)
            {
                index = static_cast<std::uint32_t>(builder.particles.textures.size());
                builder.particles.textures.push_back(*texture->texture);
            }
            else
            {
                builder.scene.warnings.push_back(*path + ": particle texture " + emitter.texture + ": " + texture->error);
            }
            builder.particles.texture_indices.emplace(texture_key, index);
        }
        effect->textures.push_back(index);
    }
    slot = std::move(effect);
    return slot;
}

void note_damage_bones(SceneBuilder& builder, const std::string& model_path, const CombatProfile& combat)
{
    for (const auto& hardpoint : combat.hardpoints)
    {
        if (hardpoint.damage_bone >= 0)
        {
            builder.damage_bones[fold_ascii(model_path)].insert(static_cast<std::uint32_t>(hardpoint.damage_bone));
        }
        if (hardpoint.decal_bone >= 0)
        {
            builder.decal_bones[fold_ascii(model_path)].insert(static_cast<std::uint32_t>(hardpoint.decal_bone));
        }
    }
}

std::vector<VisualEffectAttachment> proxy_effects(SceneBuilder& builder, const AloModel& model, const std::string& model_path)
{
    std::vector<VisualEffectAttachment> attachments;
    if (!builder.textures)
    {
        return attachments;
    }
    const auto damage = builder.damage_bones.find(fold_ascii(model_path));
    // The damage bone at or above `bone`, or -1.
    const auto damage_bone_of = [&](std::uint32_t bone) -> std::int32_t
    {
        if (damage == builder.damage_bones.end())
        {
            return -1;
        }
        for (std::size_t depth = 0; depth <= model.bones.size() && bone < model.bones.size(); ++depth)
        {
            if (damage->second.contains(bone))
            {
                return static_cast<std::int32_t>(bone);
            }
            const auto parent = model.bones[bone].parent;
            if (parent < 0)
            {
                break;
            }
            bone = static_cast<std::uint32_t>(parent);
        }
        return -1;
    };
    for (const auto& proxy : model.proxies)
    {
        // Hidden proxies (damage effects, alternate states) are shown by gameplay, which does not
        // drive them yet.
        if (proxy.hidden || proxy.bone >= model.bones.size())
        {
            continue;
        }
        // State-variant tags as on meshes: _ALT1 and up belong to damaged states (the intact model is
        // shown), and of several detail levels the first is kept; the tag is not part of the file name.
        auto name = proxy.name;
        if (const auto variant = alo_name_variant(name); variant.kind != AloVariantKind::none)
        {
            if (variant.index != 0)
            {
                continue;
            }
            name.resize(name.rfind('_'));
        }
        if (auto effect = particle_effect(builder, name))
        {
            const bool from_mesh = std::ranges::any_of(effect->system.emitters,
                                                       [](const ParticleEmitter& emitter)
                                                       {
                                                           return emitter.emit_from_mesh != 0;
                                                       });
            auto emit_mesh = from_mesh ? proxy_emit_mesh(model, proxy.bone) : nullptr;
            attachments.push_back({std::move(effect), proxy.bone, proxy.name, damage_bone_of(proxy.bone), std::move(emit_mesh)});
        }
    }
    return attachments;
}

} // namespace scene_build

void renumber_effect_textures(const std::span<VisualRig> rigs, const std::span<std::shared_ptr<const ParticleEffect>> effects, const std::span<const std::uint32_t> renderer_textures)
{
    std::unordered_map<const ParticleEffect*, std::shared_ptr<const ParticleEffect>> renumbered;
    const auto renumber = [&](std::shared_ptr<const ParticleEffect>& effect)
    {
        if (!effect)
        {
            return;
        }
        auto [entry, inserted] = renumbered.try_emplace(effect.get());
        if (inserted)
        {
            auto copy = std::make_shared<ParticleEffect>(*effect);
            for (auto& texture : copy->textures)
            {
                texture = texture < renderer_textures.size() ? renderer_textures[texture] : invalid_visual_texture;
            }
            entry->second = std::move(copy);
        }
        effect = entry->second;
    };
    for (auto& rig : rigs)
    {
        for (auto& attachment : rig.effects)
        {
            renumber(attachment.effect);
        }
    }
    for (auto& effect : effects)
    {
        renumber(effect);
    }
}

} // namespace eaw
