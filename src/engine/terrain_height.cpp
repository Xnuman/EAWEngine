#include "terrain_height.hpp"

#include "camera_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eaw {

bool TerrainHeightField::valid() const noexcept
{
    return width >= 2 && height >= 2 && spacing > 0.0F && heights.size() == static_cast<std::size_t>(width) * height;
}

float TerrainHeightField::terrain_height(const float world_x, const float world_y) const
{
    if (!valid())
    {
        return 0.0F;
    }
    const auto grid_x = std::clamp(world_x / spacing, 0.0F, static_cast<float>(width - 1));
    const auto grid_y = std::clamp(world_y / spacing, 0.0F, static_cast<float>(height - 1));
    const auto x = std::min(static_cast<std::uint32_t>(grid_x), width - 2);
    const auto y = std::min(static_cast<std::uint32_t>(grid_y), height - 2);
    const auto fx = grid_x - static_cast<float>(x);
    const auto fy = grid_y - static_cast<float>(y);
    const auto sample = [&](const std::uint32_t sx, const std::uint32_t sy)
    {
        return heights[static_cast<std::size_t>(sy) * width + sx];
    };
    const auto h00 = sample(x, y);
    const auto h10 = sample(x + 1, y);
    const auto h01 = sample(x, y + 1);
    const auto h11 = sample(x + 1, y + 1);
    // Triangles (x,y)-(x+1,y)-(x+1,y+1) when fx >= fy, otherwise (x,y)-(x+1,y+1)-(x,y+1).
    return fx >= fy ? h00 + fx * (h10 - h00) + fy * (h11 - h10) : h00 + fx * (h11 - h01) + fy * (h01 - h00);
}

float TerrainHeightField::surface_height(const float x, const float y) const
{
    const auto ground = terrain_height(x, y);
    return liquid_level ? std::max(ground, *liquid_level) : ground;
}

std::optional<std::array<float, 3>> intersect_terrain(const VisualCamera& camera, const float width, const float height, const float x, const float y, const TerrainHeightField& field)
{
    if (!field.valid())
    {
        return std::nullopt;
    }
    // Ray through the pixel: from the eye toward the point at depth 1 (unproject at depth 0 gives the
    // near-plane point on the same ray).
    const auto near_point = unproject_screen(camera, width, height, x, y, 0.0F);
    std::array<float, 3> direction{near_point[0] - camera.eye[0], near_point[1] - camera.eye[1], near_point[2] - camera.eye[2]};
    const auto length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (!(length > 0.0F))
    {
        return std::nullopt;
    }
    for (auto& component : direction)
    {
        component /= length;
    }

    // Clip the ray to the map's bounding box so the march covers only the map.
    auto minimum_height = *std::ranges::min_element(field.heights);
    auto maximum_height = *std::ranges::max_element(field.heights);
    if (field.liquid_level)
    {
        minimum_height = std::min(minimum_height, *field.liquid_level);
        maximum_height = std::max(maximum_height, *field.liquid_level);
    }
    const std::array<float, 3> box_minimum{0.0F, 0.0F, minimum_height};
    const std::array<float, 3> box_maximum{static_cast<float>(field.width - 1) * field.spacing, static_cast<float>(field.height - 1) * field.spacing, maximum_height};
    float enter = 0.0F;
    float leave = std::numeric_limits<float>::max();
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        if (std::fabs(direction[axis]) < 1.0e-9F)
        {
            if (camera.eye[axis] < box_minimum[axis] || camera.eye[axis] > box_maximum[axis])
            {
                return std::nullopt;
            }
            continue;
        }
        auto t0 = (box_minimum[axis] - camera.eye[axis]) / direction[axis];
        auto t1 = (box_maximum[axis] - camera.eye[axis]) / direction[axis];
        if (t0 > t1)
        {
            std::swap(t0, t1);
        }
        enter = std::max(enter, t0);
        leave = std::min(leave, t1);
    }
    if (enter > leave)
    {
        return std::nullopt;
    }

    const auto point_at = [&](const float t)
    {
        return std::array<float, 3>{camera.eye[0] + direction[0] * t, camera.eye[1] + direction[1] * t, camera.eye[2] + direction[2] * t};
    };
    const auto above = [&](const float t)
    {
        const auto point = point_at(t);
        return point[2] - field.surface_height(point[0], point[1]);
    };
    // March in half-cell steps until the ray passes below the surface, then bisect.
    const auto step = field.spacing * 0.5F;
    auto previous = enter;
    auto previous_above = above(previous);
    if (previous_above <= 0.0F)
    {
        return point_at(previous);
    }
    for (auto t = enter + step; t <= leave + step; t += step)
    {
        const auto current = std::min(t, leave);
        const auto current_above = above(current);
        if (current_above <= 0.0F)
        {
            auto low = previous;
            auto high = current;
            for (int iteration = 0; iteration < 24; ++iteration)
            {
                const auto middle = (low + high) * 0.5F;
                (above(middle) > 0.0F ? low : high) = middle;
            }
            auto hit = point_at(high);
            hit[2] = field.surface_height(hit[0], hit[1]);
            return hit;
        }
        previous = current;
        previous_above = current_above;
        if (current >= leave)
        {
            break;
        }
    }
    return std::nullopt;
}

} // namespace eaw
