#include "sandbox/sandbox_debug_lines.hpp"

#include "math/scalar.hpp"
#include "presentation/debug_draw.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace eaw {

std::vector<VisualDebugLine> build_sandbox_debug_lines(const SandboxDebugView& view)
{
    static const GroundMap no_ground;
    const auto& ground = view.ground != nullptr ? *view.ground : no_ground;
    std::vector<VisualDebugLine> lines;
    if (view.presentation != nullptr)
    {
        if (view.show_bounds)
        {
            append_debug_axes(lines, {0.0F, 0.0F, 0.0F}, view.axis_length);
        }
        for (std::size_t index = 0; index < view.instances.size(); ++index)
        {
            const auto& instance = view.instances[index];
            if (instance.background || (!view.show_bounds && !instance.highlighted))
            {
                continue;
            }
            append_debug_box(lines, view.presentation->mesh_minimum(instance.mesh), view.presentation->mesh_maximum(instance.mesh), instance.transform,
                             instance.highlighted ? Color{1.0F, 0.85F, 0.25F, 1.0F} : Color{0.3F, 0.85F, 0.95F, 0.45F});
        }
    }
    if (view.world != nullptr && view.presentation != nullptr)
    {
        const auto& snapshot = view.world->snapshot();
        // Shots in flight as short streaks, green for the debug-spawn owner's side.
        for (const auto& projectile : snapshot.projectiles)
        {
            if (view.projectile_visuals != nullptr)
            {
                if (const auto found = view.projectile_visuals->find(projectile.visual); found != view.projectile_visuals->end() && found->second.drawn())
                {
                    continue;
                }
            }
            const auto& p = projectile.position;
            const auto& d = projectile.direction;
            constexpr float streak = 14.0F;
            lines.push_back({{p.x - d.x * streak, p.y - d.y * streak, p.z - d.z * streak},
                             {p.x, p.y, p.z},
                             projectile.owner == view.friendly_owner ? Color{0.4F, 1.0F, 0.4F, 1.0F} : Color{1.0F, 0.35F, 0.25F, 1.0F}});
        }
        // Health (green over red) and shield (blue) bars above selected or damaged entities.
        std::unordered_map<std::uint64_t, const EntitySnapshot*> by_key;
        by_key.reserve(snapshot.entities.size());
        for (const auto& entity : snapshot.entities)
        {
            by_key.emplace(entity_key(entity.id), &entity);
        }
        for (std::size_t index = 0; index < view.instances.size() && index < view.entities.size(); ++index)
        {
            const auto found = view.entities[index].valid() ? by_key.find(entity_key(view.entities[index])) : by_key.end();
            if (found == by_key.end() || found->second->max_health <= 0.0F)
            {
                continue;
            }
            const auto& entity = *found->second;
            const bool damaged = entity.health < entity.max_health || entity.shield < entity.max_shield;
            if (!damaged && !view.instances[index].highlighted)
            {
                continue;
            }
            const auto& t = view.instances[index].transform.values;
            const auto z_scale = std::sqrt(t[8] * t[8] + t[9] * t[9] + t[10] * t[10]);
            const auto top = t[14] + view.presentation->mesh_maximum(view.instances[index].mesh)[2] * z_scale + 6.0F;
            const float half = 15.0F;
            const auto bar = [&](const float z, const float fraction, const Color& full, const Color& empty)
            {
                const auto split = t[12] - half + 2.0F * half * std::clamp(fraction, 0.0F, 1.0F);
                lines.push_back({{t[12] - half, t[13], z}, {split, t[13], z}, full});
                lines.push_back({{split, t[13], z}, {t[12] + half, t[13], z}, empty});
            };
            bar(top, entity.health / entity.max_health, {0.3F, 1.0F, 0.3F, 1.0F}, {0.8F, 0.1F, 0.1F, 1.0F});
            if (entity.max_shield > 0.0F)
            {
                bar(top + 3.0F, entity.shield / entity.max_shield, {0.35F, 0.65F, 1.0F, 1.0F}, {0.15F, 0.15F, 0.3F, 1.0F});
            }
        }
    }
    if (view.world != nullptr && view.selection != nullptr)
    {
        // Footprint circles of selected units (the separation radius), just above the surface.
        for (const auto& entity : view.world->snapshot().entities)
        {
            if (entity.radius <= 0.0F || !view.selection->contains(entity_key(entity.id)))
            {
                continue;
            }
            constexpr int segments = 24;
            const auto point = [&](const int segment)
            {
                const auto angle = two_pi * static_cast<float>(segment) / static_cast<float>(segments);
                const auto x = entity.position.x + std::cos(angle) * entity.radius;
                const auto y = entity.position.y + std::sin(angle) * entity.radius;
                const auto z = entity.follows_ground && ground.valid() ? ground.surface_height(x, y) + 1.0F : entity.position.z;
                return Vector3{x, y, z};
            };
            for (int segment = 0; segment < segments; ++segment)
            {
                lines.push_back({point(segment), point(segment + 1), {1.0F, 0.85F, 0.25F, 0.7F}});
            }
        }
    }
    if (view.world != nullptr && !ground.valid())
    {
        // Space: the remaining route of every selected moving unit (every one while navigation is
        // shown) in 3D, through its avoidance detour when it steers around a unit in its way.
        for (const auto& entity : view.world->snapshot().entities)
        {
            if (!entity.moving || (!view.show_navigation && !view.selection->contains(entity_key(entity.id))))
            {
                continue;
            }
            auto from = entity.position;
            for (const auto& to : view.world->route(entity.id))
            {
                lines.push_back({from, to, {0.35F, 1.0F, 0.9F, 0.9F}});
                from = to;
            }
        }
    }
    if (view.world != nullptr && ground.valid())
    {
        // The remaining route of every selected moving unit (every one while navigation is shown),
        // draped just above the surface.
        for (const auto& entity : view.world->snapshot().entities)
        {
            if (!entity.moving || (!view.show_navigation && !view.selection->contains(entity_key(entity.id))))
            {
                continue;
            }
            auto from = entity.position;
            for (const auto& to : view.world->route(entity.id))
            {
                const auto length = std::hypot(to.x - from.x, to.y - from.y);
                const auto pieces = std::clamp(static_cast<int>(length / ground.spacing), 1, 256);
                for (int piece = 0; piece < pieces; ++piece)
                {
                    const auto at = [&](const int step)
                    {
                        const auto t = static_cast<float>(step) / static_cast<float>(pieces);
                        const auto x = from.x + (to.x - from.x) * t;
                        const auto y = from.y + (to.y - from.y) * t;
                        const auto z = entity.follows_ground ? ground.surface_height(x, y) + 2.0F : from.z + (to.z - from.z) * t;
                        return Vector3{x, y, z};
                    };
                    lines.push_back({at(piece), at(piece + 1), {0.35F, 1.0F, 0.9F, 0.9F}});
                }
                from = to;
            }
        }
    }
    if (view.show_navigation && view.world != nullptr && ground.valid())
    {
        // Every ground unit's footprint as the simulation measures it: its rectangle at its heading, or
        // its circle, just above the surface.
        const auto lifted = [&](const float x, const float y)
        {
            return Vector3{x, y, ground.surface_height(x, y) + 1.0F};
        };
        for (const auto& entity : view.world->snapshot().entities)
        {
            if (!entity.follows_ground || (entity.radius <= 0.0F && entity.body.half_width <= 0.0F))
            {
                continue;
            }
            const Color color{0.4F, 0.9F, 1.0F, 0.9F};
            if (entity.body.half_width > 0.0F)
            {
                const auto center = footprint_center_at(entity.body, entity.position, entity.heading);
                const auto c = std::cos(entity.heading);
                const auto s = std::sin(entity.heading);
                const auto corner = [&](const float along, const float across)
                {
                    return lifted(center.x + along * c - across * s, center.y + along * s + across * c);
                };
                const auto l = entity.body.half_length;
                const auto w = entity.body.half_width;
                const std::array corners{corner(l, w), corner(-l, w), corner(-l, -w), corner(l, -w)};
                for (std::size_t side = 0; side < corners.size(); ++side)
                {
                    lines.push_back({corners[side], corners[(side + 1U) % corners.size()], color});
                }
                // Its front, so the heading reads.
                lines.push_back({lifted(center.x, center.y), corner(l, 0.0F), color});
                continue;
            }
            constexpr int segments = 16;
            for (int segment = 0; segment < segments; ++segment)
            {
                const auto at = [&](const int step)
                {
                    const auto angle = two_pi * static_cast<float>(step) / static_cast<float>(segments);
                    return lifted(entity.position.x + std::cos(angle) * entity.radius, entity.position.y + std::sin(angle) * entity.radius);
                };
                lines.push_back({at(segment), at(segment + 1), color});
            }
        }
    }
    if (view.show_navigation && ground.valid() && ground.passability.size() == ground.heights.size())
    {
        // The whole map's passability as outlines: each border between two cells of different classes,
        // one of them not plain ground, draped just above the surface and colored by that class (red
        // Steep, blue Water, orange Impassable, green InfantryOnly, magenta Obstacle, white anything
        // else). Outlines rather than a mark on every cell keep a whole map within the renderer's debug
        // line capacity.
        const auto spacing = ground.spacing;
        const auto color_of = [](const std::uint32_t value)
        {
            return value == passability::steep           ? Color{1.0F, 0.25F, 0.2F, 0.85F}
                   : value == passability::water         ? Color{0.25F, 0.55F, 1.0F, 0.85F}
                   : value == passability::impassable    ? Color{1.0F, 0.6F, 0.1F, 0.85F}
                   : value == passability::infantry_only ? Color{0.3F, 1.0F, 0.35F, 0.85F}
                   : value == passability::obstacle      ? Color{1.0F, 0.3F, 1.0F, 0.85F}
                                                         : Color{1.0F, 1.0F, 1.0F, 0.85F};
        };
        const auto lifted = [&](const float x, const float y)
        {
            return Vector3{x, y, ground.surface_height(x, y) + 1.5F};
        };
        const auto border = [&](const std::uint32_t a, const std::uint32_t b, const Vector3& from, const Vector3& to)
        {
            if (a != b)
            {
                lines.push_back({from, to, color_of(a != passability::clear ? a : b)});
            }
        };
        for (std::uint32_t row = 0; row < ground.height; ++row)
        {
            for (std::uint32_t column = 0; column < ground.width; ++column)
            {
                const auto value = ground.passability[static_cast<std::size_t>(row) * ground.width + column];
                // Cell (column, row) covers half a spacing around its sample point.
                const auto x = (static_cast<float>(column) + 0.5F) * spacing;
                const auto y = (static_cast<float>(row) + 0.5F) * spacing;
                if (column + 1U < ground.width)
                {
                    border(value, ground.passability[static_cast<std::size_t>(row) * ground.width + column + 1U], lifted(x, y - spacing), lifted(x, y));
                }
                if (row + 1U < ground.height)
                {
                    border(value, ground.passability[static_cast<std::size_t>(row + 1U) * ground.width + column], lifted(x - spacing, y), lifted(x, y));
                }
            }
        }
    }
    return lines;
}

} // namespace eaw
