// Particle effects for scene builds: decoding particle ALOs through the asset cache, loading their
// textures, and attaching them to model proxies.

#include "scene_builder_internal.hpp"

#include "eaw/alo_interpretation.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
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

std::string_view trim_field(std::string_view text)
{
    constexpr std::string_view space = " \t\r\n";
    const auto first = text.find_first_not_of(space);
    if (first == std::string_view::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(space) - first + 1);
}

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
    const auto to_proxy = inverse(bind_pose[proxy_bone]);
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
        const auto transform = bind_pose[static_cast<std::size_t>(parent)] * *to_proxy;
        const auto origin = transform_point({}, transform);
        auto emit = std::make_shared<ParticleEmitMesh>();
        for (const auto& submesh : mesh.submeshes)
        {
            const auto first = static_cast<std::uint32_t>(emit->positions.size());
            for (const auto& vertex : submesh.vertices)
            {
                const auto position = transform_point(vertex.position, transform);
                const auto tip = transform_point(vertex.normal, transform);
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
    const auto key = ascii_lower(reference);
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
    const auto loaded = cached<CachedParticleSystem>(builder.cache, "particles", ascii_lower(*path), builder.vfs,
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
    // The file's facts become the engine-native description under the build's fidelity profile.
    auto effect = std::make_shared<ParticleEffect>(normalize_particle_system(*loaded->system, builder.particle_fidelity));
    effect->name = std::filesystem::path(*path).stem().string();
    for (const auto& warning : loaded->system->warnings)
    {
        builder.scene.warnings.push_back(*path + ": " + warning);
    }
    for (const auto& emitter : effect->emitters)
    {
        effect->textures.push_back(particle_library_texture(builder, emitter.texture, *path));
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
            builder.damage_bones[ascii_lower(model_path)].insert(static_cast<std::uint32_t>(hardpoint.damage_bone));
        }
        if (hardpoint.decal_bone >= 0)
        {
            builder.decal_bones[ascii_lower(model_path)].insert(static_cast<std::uint32_t>(hardpoint.decal_bone));
        }
        if (hardpoint.engine_bone >= 0)
        {
            builder.engine_bones[ascii_lower(model_path)].insert(static_cast<std::uint32_t>(hardpoint.engine_bone));
        }
    }
}

std::uint32_t particle_library_texture(SceneBuilder& builder, const std::string& reference, const std::string& context)
{
    const auto key = ascii_lower(reference);
    if (const auto known = builder.particles.texture_indices.find(key); known != builder.particles.texture_indices.end())
    {
        return known->second;
    }
    auto index = invalid_visual_texture;
    const auto texture = cached_texture(builder.vfs, builder.cache, reference);
    if (texture->texture)
    {
        index = static_cast<std::uint32_t>(builder.particles.textures.size());
        builder.particles.textures.push_back(*texture->texture);
    }
    else
    {
        builder.scene.warnings.push_back(context + ": particle texture " + reference + ": " + texture->error);
    }
    builder.particles.texture_indices.emplace(key, index);
    return index;
}

void load_effect_objects(SceneBuilder& builder, const GameObjectCatalog& catalog, const std::span<const std::uint32_t> keys, const bool land, ParticleEffectObjectTable& table)
{
    if (!builder.textures)
    {
        return;
    }
    for (const auto key : keys)
    {
        if (key == 0 || table.contains(key))
        {
            continue;
        }
        // The definition a key names: a Particle object when there is one, else a Projectile.
        const auto definition_of = [&](const std::uint32_t crc) -> const XmlDefinition*
        {
            const XmlDefinition* found = nullptr;
            for (const auto* candidate : catalog.find_crc(crc))
            {
                if (found == nullptr || candidate->type == "Particle" || (candidate->type == "Projectile" && found->type != "Particle"))
                {
                    found = candidate;
                }
            }
            return found;
        };
        const auto field_text = [&](const XmlDefinition& definition, const std::string_view field) -> std::string
        {
            const auto* node = catalog.field(definition, field).field;
            return node != nullptr ? std::string(trim_field(node->text)) : std::string{};
        };
        const XmlDefinition* object = definition_of(key);
        if (object == nullptr)
        {
            builder.scene.warnings.push_back("particle object " + std::to_string(key) + " is not defined");
            table.emplace(key, ParticleEffectObject{});
            continue;
        }
        // Death_Explosions may name a Projectile instead of a Particle object (12 stock uses: mines,
        // remote bombs, construction pods, wall destruction). All of them detonate at once (no
        // flight distance), so their look is their detonation particle: lifetime, else object, else
        // ground (an inference from the stock data; their blast damage is gameplay, not played here).
        if (object->type == "Projectile")
        {
            const auto& projectile = *object;
            std::string particle;
            for (const auto field : {"Projectile_Lifetime_Detonation_Particle", "Projectile_Object_Detonation_Particle", "Projectile_Ground_Detonation_Particle"})
            {
                if (particle = field_text(projectile, field); !particle.empty())
                {
                    break;
                }
            }
            object = particle.empty() ? nullptr : definition_of(game_object_name_crc(particle));
            if (object == nullptr || object->type != "Particle")
            {
                builder.scene.warnings.push_back(projectile.name + ": projectile named as an explosion has no detonation Particle object" +
                                                 (particle.empty() ? std::string{} : " ('" + particle + "' is not one") + "; it plays nothing");
                table.emplace(key, ParticleEffectObject{});
                continue;
            }
        }
        const auto text = [&](const std::string_view field)
        {
            return field_text(*object, field);
        };
        ParticleEffectObject entry;
        // The model for the map's context, else the other one (most objects name both).
        auto model = text(land ? "Land_Model_Name" : "Space_Model_Name");
        if (model.empty())
        {
            model = text(land ? "Space_Model_Name" : "Land_Model_Name");
        }
        // An object without a playable effect stays in the table (its impacts play nothing), but is
        // reported: proxies may name meshes, a gameplay Particle object should not.
        if (model.empty())
        {
            builder.scene.warnings.push_back(object->name + ": Particle object names no Space_Model_Name or Land_Model_Name; it plays nothing");
        }
        else
        {
            const auto reported = builder.scene.warnings.size();
            entry.effect = particle_effect(builder, model);
            // particle_effect reports missing and malformed files itself and is silent only for a
            // file that is a model rather than a particle system.
            if (!entry.effect && builder.scene.warnings.size() == reported)
            {
                builder.scene.warnings.push_back(object->name + ": Particle object model " + model + " is a mesh, not a particle system; it plays nothing");
            }
        }
        if (const auto scale = text("Scale_Factor"); !scale.empty())
        {
            float value{};
            const auto [end, error] = std::from_chars(scale.data(), scale.data() + scale.size(), value);
            if (error == std::errc{} && end == scale.data() + scale.size() && std::isfinite(value) && value > 0.0F)
            {
                entry.scale = value;
            }
            else
            {
                builder.scene.warnings.push_back(object->name + ": Scale_Factor '" + scale + "' is not a positive number");
            }
        }
        table.emplace(key, std::move(entry));
    }
}

std::vector<VisualEffectAttachment> proxy_effects(SceneBuilder& builder, const AloModel& model, const std::string& model_path)
{
    std::vector<VisualEffectAttachment> attachments;
    if (!builder.textures)
    {
        return attachments;
    }
    const auto damage = builder.damage_bones.find(ascii_lower(model_path));
    const auto engines = builder.engine_bones.find(ascii_lower(model_path));
    // Whether `bone` or an ancestor is in `bones`.
    const auto under = [&](std::uint32_t bone, const std::unordered_set<std::uint32_t>& bones)
    {
        for (std::size_t depth = 0; depth <= model.bones.size() && bone < model.bones.size(); ++depth)
        {
            if (bones.contains(bone))
            {
                return true;
            }
            const auto parent = model.bones[bone].parent;
            if (parent < 0)
            {
                break;
            }
            bone = static_cast<std::uint32_t>(parent);
        }
        return false;
    };
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
            const bool from_mesh = std::ranges::any_of(effect->emitters,
                                                       [](const ParticleEmitterDesc& emitter)
                                                       {
                                                           return emitter.mesh_emission != ParticleMeshEmission::none;
                                                       });
            auto emit_mesh = from_mesh ? proxy_emit_mesh(model, proxy.bone) : nullptr;
            // Engine effects: the stock "pe_" naming, or a proxy under a hardpoint's Engine_Particles bone.
            const bool engine = ascii_lower(name).starts_with("pe_") || (engines != builder.engine_bones.end() && under(proxy.bone, engines->second));
            attachments.push_back({std::move(effect), proxy.bone, proxy.name, damage_bone_of(proxy.bone), std::move(emit_mesh), engine});
        }
    }
    return attachments;
}

} // namespace scene_build

void renumber_effect_textures(const std::span<VisualRig> rigs, const std::span<std::shared_ptr<const ParticleEffect>> effects, const std::span<const std::uint32_t> renderer_textures,
                              ParticleEffectObjectTable* const objects, ProjectileVisualTable* const projectiles)
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
    if (objects != nullptr)
    {
        for (auto& [key, object] : *objects)
        {
            renumber(object.effect);
        }
    }
    if (projectiles != nullptr)
    {
        for (auto& [key, visual] : *projectiles)
        {
            renumber(visual.effect);
        }
    }
}

} // namespace eaw
