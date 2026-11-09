#include "projectile_visuals.hpp"

#include "scene_presentation.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string_view>

namespace eaw {
namespace {

std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n'))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
    {
        text.remove_suffix(1);
    }
    return text;
}

// Comma-separated numbers ("3,0", "104,181,230,255"); empty when any part does not parse.
template <class T> std::vector<T> numbers(const std::string_view text)
{
    std::vector<T> values;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const auto comma = text.find(',', start);
        const auto part = trim(text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
        T value{};
        const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), value);
        if (error != std::errc{} || end != part.data() + part.size())
        {
            return {};
        }
        values.push_back(value);
        if (comma == std::string_view::npos)
        {
            break;
        }
        start = comma + 1;
    }
    return values;
}

} // namespace

void read_projectile_visuals(const GameObjectCatalog& catalog, const std::span<const std::uint32_t> keys, ProjectileVisualTable& table, std::vector<std::string>& warnings)
{
    for (const auto key : keys)
    {
        if (key == 0 || table.contains(key))
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
        const auto text = [&](const std::string_view field) -> std::string_view
        {
            const auto* node = catalog.field(*projectile, field).field;
            return node != nullptr ? trim(node->text) : std::string_view{};
        };
        const auto render = numbers<int>(text("Projectile_Custom_Render"));
        ProjectileVisual visual;
        const auto particle_key = [&](const std::string_view field) -> std::uint32_t
        {
            const auto name = text(field);
            return name.empty() ? 0U : game_object_name_crc(name);
        };
        visual.impact_effects = {particle_key("Projectile_Absorbed_By_Shields_Particle"), particle_key("Projectile_Object_Detonation_Particle"),
                                 particle_key("Projectile_Object_Armor_Reduced_Detonation_Particle"), particle_key("Projectile_Lifetime_Detonation_Particle")};
        visual.bolt = render.size() == 1 && (render.front() == 1 || render.front() == 2);
        if (!visual.bolt)
        {
            table.emplace(key, visual);
            continue;
        }
        visual.tinted = render.front() == 2;
        const auto number = [&](const std::string_view field, const float fallback)
        {
            const auto values = numbers<float>(text(field));
            if (values.size() == 1 && std::isfinite(values.front()) && values.front() > 0.0F)
            {
                return values.front();
            }
            if (!text(field).empty())
            {
                warnings.push_back(projectile->name + ": " + std::string(field) + " '" + std::string(text(field)) + "' is not a positive number");
            }
            return fallback;
        };
        visual.width = number("Projectile_Width", 1.0F);
        visual.length = number("Projectile_Length", 1.0F);
        // Render 2 draws a teardrop whose length is given in widths (inferred from the original:
        // the 5 x 10 turbolaser streaks are about 50 units long; see the header).
        if (visual.tinted)
        {
            visual.length *= visual.width;
        }
        if (const auto slot = numbers<std::uint32_t>(text("Projectile_Texture_Slot")); slot.size() == 2 && slot[0] < projectile_atlas_cells && slot[1] < projectile_atlas_cells)
        {
            visual.column = slot[0];
            visual.row = slot[1];
        }
        else if (slot.size() == 1 && slot[0] < projectile_atlas_cells * projectile_atlas_cells)
        {
            // Mods also write a single cell index ("2"), counted along rows of the atlas.
            visual.column = slot[0] % projectile_atlas_cells;
            visual.row = slot[0] / projectile_atlas_cells;
        }
        else if (!text("Projectile_Texture_Slot").empty())
        {
            warnings.push_back(projectile->name + ": Projectile_Texture_Slot '" + std::string(text("Projectile_Texture_Slot")) + "' is not a cell of the 4x4 atlas");
        }
        if (const auto color = numbers<int>(text("Projectile_Laser_Color")); color.size() >= 3)
        {
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                const auto value = channel < color.size() ? color[channel] : 255;
                visual.color[channel] = static_cast<float>(std::clamp(value, 0, 255)) / 255.0F;
            }
        }
        table.emplace(key, visual);
    }
}

void collect_projectile_keys(const CombatProfile& profile, std::vector<std::uint32_t>& keys)
{
    for (const auto& weapon : profile.weapons)
    {
        if (weapon.projectile_visual != 0)
        {
            keys.push_back(weapon.projectile_visual);
        }
    }
}

void collect_impact_effect_keys(const ProjectileVisualTable& table, std::vector<std::uint32_t>& keys)
{
    for (const auto& [key, visual] : table)
    {
        for (const auto effect : visual.impact_effects)
        {
            if (effect != 0)
            {
                keys.push_back(effect);
            }
        }
    }
}

std::uint32_t impact_effect(const ProjectileVisual& visual, const ImpactKind kind) noexcept
{
    const auto index = static_cast<std::size_t>(kind);
    return index < visual.impact_effects.size() ? visual.impact_effects[index] : 0U;
}

void append_projectile_sprites(const std::span<const ProjectileSnapshot> current, const ProjectileVisualTable& table, const std::uint32_t atlas, const float interpolation, const float tick_seconds,
                               std::vector<VisualParticle>& sprites)
{
    constexpr float cell = 1.0F / static_cast<float>(projectile_atlas_cells);
    for (const auto& projectile : current)
    {
        const auto found = table.find(projectile.visual);
        if (found == table.end() || !found->second.bolt)
        {
            continue;
        }
        const auto& visual = found->second;
        const auto& d = projectile.direction;
        // Between the previous tick and this one, as entities are drawn; the bolt's head leads.
        const auto back = projectile.speed * tick_seconds * (1.0F - std::clamp(interpolation, 0.0F, 1.0F)) + visual.length * 0.5F;
        VisualParticle sprite;
        sprite.position = {projectile.position.x - d.x * back, projectile.position.y - d.y * back, projectile.position.z - d.z * back};
        sprite.axis = {d.x, d.y, d.z};
        sprite.half_size = visual.width * 0.5F;
        sprite.tail = visual.length;
        sprite.uv_rect = {static_cast<float>(visual.column) * cell, static_cast<float>(visual.row) * cell, cell, cell};
        sprite.color = visual.color;
        sprite.texture = atlas;
        sprite.shape = visual.tinted ? VisualParticleShape::teardrop : VisualParticleShape::beam;
        sprite.blend = VisualParticleBlend::additive;
        sprites.push_back(sprite);
    }
}

std::array<float, 16> projectile_model_transform(const ProjectileSnapshot& projectile, const ProjectileVisual& visual, const float interpolation, const float tick_seconds)
{
    const auto& d = projectile.direction;
    const auto back = projectile.speed * tick_seconds * (1.0F - std::clamp(interpolation, 0.0F, 1.0F));
    const SimVector3 position{projectile.position.x - d.x * back, projectile.position.y - d.y * back, projectile.position.z - d.z * back};
    const auto heading = std::atan2(d.y, d.x);
    const auto pitch = std::asin(std::clamp(d.z, -1.0F, 1.0F));
    auto scale = visual_identity_transform;
    scale[0] = scale[5] = scale[10] = visual.scale;
    return pose_transform(scale, identity_model_heading, position, heading, pitch);
}

void append_projectile_instances(const std::span<const ProjectileSnapshot> current, const ProjectileVisualTable& table, const float interpolation, const float tick_seconds,
                                 std::vector<VisualInstance>& instances, std::vector<std::uint32_t>& serials)
{
    for (const auto& projectile : current)
    {
        const auto found = table.find(projectile.visual);
        if (found == table.end() || found->second.bolt || !found->second.mesh)
        {
            continue;
        }
        VisualInstance instance;
        instance.mesh = *found->second.mesh;
        instance.transform = projectile_model_transform(projectile, found->second, interpolation, tick_seconds);
        instances.push_back(instance);
        serials.push_back(projectile.serial);
    }
}

} // namespace eaw
