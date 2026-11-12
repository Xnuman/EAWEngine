#include "presentation/projectile_visuals.hpp"

#include "math/scalar.hpp"
#include "core/string_utils.hpp"
#include "assets/legacy_units.hpp"
#include "assets/type_fields.hpp"
#include "battle/object_types.hpp"
#include "presentation/scene_presentation.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string_view>

namespace eaw {
namespace {

// Comma-separated whole numbers ("3,0", "104,181,230,255"); empty when any part is not one.
template <class T> std::vector<T> numbers(const std::string_view text)
{
    std::vector<T> values;
    for (const auto part : split_list(text))
    {
        T value{};
        const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), value);
        if (error != std::errc{} || end != part.data() + part.size())
        {
            return {};
        }
        values.push_back(value);
    }
    return values;
}

// The widest rocket sway drawn: stock offsets are 5 to 15, and one of 180 is of unknown unit (an
// engine choice).
constexpr float max_drawn_sway = 50.0F;

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
        const TypeFields fields(catalog, *projectile, &warnings);
        const auto render = numbers<int>(fields.text("Projectile_Custom_Render"));
        ProjectileVisual visual;
        const auto particle_key = [&](const std::string_view field) -> std::uint32_t
        {
            const auto name = fields.text(field);
            return name.empty() ? 0U : game_object_name_crc(name);
        };
        visual.impact_effects = {particle_key("Projectile_Absorbed_By_Shields_Particle"), particle_key("Projectile_Object_Detonation_Particle"),
                                 particle_key("Projectile_Object_Armor_Reduced_Detonation_Particle"), particle_key("Projectile_Lifetime_Detonation_Particle")};
        const auto positive = [&](const std::string_view field)
        {
            const auto value = fields.number(field).value_or(0.0F);
            return value > 0.0F ? value : 0.0F;
        };
        if (const auto gravity = positive("Projectile_Grenade_Gravity"); gravity > 0.0F)
        {
            const auto lob = positive("Projectile_Grenade_Gravity_Lob_Mod");
            visual.lob_gravity = legacy_units::acceleration_per_frame(gravity * (lob > 0.0F ? lob : 1.0F));
        }
        visual.sway = std::min(positive("Projectile_Rocket_Curve_Offset"), max_drawn_sway);
        visual.sway_after = positive("Projectile_Rocket_Straight_Distance");
        visual.bolt = render.size() == 1 && (render.front() == 1 || render.front() == 2);
        if (!visual.bolt)
        {
            table.emplace(key, visual);
            continue;
        }
        visual.tinted = render.front() == 2;
        const auto number = [&](const std::string_view field, const float fallback)
        {
            const auto value = fields.number(field);
            if (value && *value > 0.0F)
            {
                return *value;
            }
            if (value)
            {
                fields.warn(field, fields.text(field), "a positive number");
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
        if (const auto slot = numbers<std::uint32_t>(fields.text("Projectile_Texture_Slot")); slot.size() == 2 && slot[0] < projectile_atlas_cells && slot[1] < projectile_atlas_cells)
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
        else if (!fields.text("Projectile_Texture_Slot").empty())
        {
            warnings.push_back(projectile->name + ": Projectile_Texture_Slot '" + std::string(fields.text("Projectile_Texture_Slot")) + "' is not a cell of the 4x4 atlas");
        }
        if (const auto color = numbers<int>(fields.text("Projectile_Laser_Color")); color.size() >= 3)
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
        // Between the previous tick and this one, as entities are drawn; the bolt's head leads.
        const auto pose = projectile_pose(projectile, visual, interpolation, tick_seconds);
        const auto& d = pose.direction;
        const auto back = visual.length * 0.5F;
        VisualParticle sprite;
        sprite.position = {pose.position.x - d.x * back, pose.position.y - d.y * back, pose.position.z - d.z * back};
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

ProjectilePose projectile_pose(const ProjectileSnapshot& projectile, const ProjectileVisual& visual, const float interpolation, const float tick_seconds) noexcept
{
    const auto& d = projectile.direction;
    const auto back = projectile.speed * tick_seconds * (1.0F - std::clamp(interpolation, 0.0F, 1.0F));
    ProjectilePose pose{{projectile.position.x - d.x * back, projectile.position.y - d.y * back, projectile.position.z - d.z * back}, d};
    const auto flown = std::max(0.0F, projectile.flown - back);
    const auto remaining = std::max(0.0F, projectile.remaining + back);
    const auto total = flown + remaining;
    if (total <= 0.0F || projectile.speed <= 0.0F)
    {
        return pose;
    }
    // Horizontal heading of the straight path, and its left perpendicular.
    const auto level = std::hypot(d.x, d.y);
    const auto ahead_x = level > 0.0F ? d.x / level : 1.0F;
    const auto ahead_y = level > 0.0F ? d.y / level : 0.0F;
    Vector3 tangent{d.x * projectile.speed, d.y * projectile.speed, d.z * projectile.speed};
    if (visual.lob_gravity > 0.0F)
    {
        // A ballistic arc over the chord: height g/2 t (T - t), rising at g/2 (T - 2t).
        const auto t = flown / projectile.speed;
        const auto time = total / projectile.speed;
        pose.position.z += visual.lob_gravity * 0.5F * t * (time - t);
        tangent.z += visual.lob_gravity * 0.5F * (time - 2.0F * t);
    }
    if (visual.sway > 0.0F && flown > visual.sway_after && total > visual.sway_after)
    {
        const auto span = total - visual.sway_after;
        const auto phase = pi * (flown - visual.sway_after) / span;
        const auto side = (projectile.serial % 2U) == 0U ? visual.sway : -visual.sway;
        const auto offset = side * std::sin(phase);
        const auto rate = side * pi / span * std::cos(phase) * projectile.speed;
        pose.position.x += -ahead_y * offset;
        pose.position.y += ahead_x * offset;
        tangent.x += -ahead_y * rate;
        tangent.y += ahead_x * rate;
    }
    const auto length = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z);
    if (length > 0.0F)
    {
        pose.direction = {tangent.x / length, tangent.y / length, tangent.z / length};
    }
    return pose;
}

Matrix4 projectile_model_transform(const ProjectileSnapshot& projectile, const ProjectileVisual& visual, const float interpolation, const float tick_seconds)
{
    const auto pose = projectile_pose(projectile, visual, interpolation, tick_seconds);
    const auto& d = pose.direction;
    const auto& position = pose.position;
    const auto heading = std::atan2(d.y, d.x);
    const auto pitch = std::asin(std::clamp(d.z, -1.0F, 1.0F));
    auto scale = Matrix4::identity();
    scale(0, 0) = scale(1, 1) = scale(2, 2) = visual.scale;
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
