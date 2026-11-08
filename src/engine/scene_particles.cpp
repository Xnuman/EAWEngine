// Particle effects for scene builds: decoding particle ALOs through the asset cache, loading their
// textures, and attaching them to model proxies.

#include "scene_builder_internal.hpp"

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

std::vector<VisualEffectAttachment> proxy_effects(SceneBuilder& builder, const AloModel& model)
{
    std::vector<VisualEffectAttachment> attachments;
    if (!builder.textures)
    {
        return attachments;
    }
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
            attachments.push_back({std::move(effect), proxy.bone, proxy.name});
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
