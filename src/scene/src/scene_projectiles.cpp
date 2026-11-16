// How shots look, for scene builds: bolts (custom-render projectiles, the pill atlas), and model
// projectiles, whose meshes join the scene's mesh library or whose particle systems are decoded.

#include "formats/alo_particle_system.hpp"
#include "scene/scene_builder_internal.hpp"

#include <charconv>
#include <cmath>
#include <string>

namespace eaw {
namespace scene_build {
namespace {

std::string_view trim_text(std::string_view text)
{
    constexpr std::string_view space = " \t\r\n";
    const auto first = text.find_first_not_of(space);
    if (first == std::string_view::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(space) - first + 1);
}

} // namespace

void load_projectile_visuals(SceneBuilder& builder, const GameObjectCatalog& catalog, const SurfaceEffectCatalog* surfaces, const std::span<const std::uint32_t> keys, ProjectileVisualTable& table,
                             std::uint32_t& atlas)
{
    read_projectile_visuals(catalog, keys, table, builder.scene.warnings, surfaces);
    if (!table.empty())
    {
        atlas = particle_library_texture(builder, std::string(projectile_atlas_texture), "projectile bolts");
    }
}

void load_projectile_models(SceneBuilder& builder, const GameObjectCatalog& catalog, const bool land, std::unordered_map<std::string, std::optional<std::uint32_t>>& meshes,
                            ProjectileVisualTable& table)
{
    const auto primary = land ? GameModelContext::land : GameModelContext::space;
    const auto secondary = land ? GameModelContext::space : GameModelContext::land;
    for (auto& [key, visual] : table)
    {
        if (visual.bolt)
        {
            continue;
        }
        const XmlDefinition* projectile = nullptr;
        for (const auto* candidate : catalog.find_crc(key))
        {
            if (projectile == nullptr || candidate->type == "Projectile")
            {
                projectile = candidate;
            }
        }
        if (projectile == nullptr)
        {
            continue;
        }
        // Many projectiles name only one context's model and are fired in both (concussion missiles
        // name a land model and fly in space too).
        auto model = catalog.model(*projectile, primary);
        if (!model)
        {
            model = catalog.model(*projectile, secondary);
        }
        if (!model)
        {
            continue;
        }
        if (const auto* node = catalog.field(*projectile, "Scale_Factor").field)
        {
            const auto text = trim_text(node->text);
            float value{};
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error == std::errc{} && end == text.data() + text.size() && std::isfinite(value) && value > 0.0F)
            {
                visual.scale = value;
            }
            else
            {
                builder.scene.warnings.push_back(projectile->name + ": Scale_Factor '" + std::string(text) + "' is not a positive number");
            }
        }
        const auto path = resolve_model_path(builder.vfs, model->value);
        if (!path)
        {
            builder.scene.warnings.push_back(projectile->name + ": projectile model " + model->value + " was not found");
            continue;
        }
        // Some projectiles name a particle system as their model; it plays along the flight.
        const auto bytes = builder.vfs.read(*path);
        if (!bytes)
        {
            builder.scene.warnings.push_back(projectile->name + ": " + bytes.error().message);
            continue;
        }
        if (const auto generic = read_alo<AloGenericData>(*bytes); generic && (generic->type == AloType::particle_system || generic->type == AloType::plugin_particle_system))
        {
            visual.effect = particle_effect(builder, *path);
            continue;
        }
        auto [entry, inserted] = meshes.try_emplace(attached_mesh_key(*path, {}));
        if (inserted)
        {
            entry->second = append_scene_model(builder, *path);
        }
        visual.mesh = entry->second;
    }
}

} // namespace scene_build
} // namespace eaw
