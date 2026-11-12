#include "control/terrain_height.hpp"

#include "control/camera_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eaw {

std::optional<Vector3> intersect_terrain(const VisualCamera& camera, const float width, const float height, const float x, const float y, const GroundMap& field)
{
    if (!field.valid())
    {
        return std::nullopt;
    }
    // Ray through the pixel: from the eye toward the point at depth 1 (unproject at depth 0 gives the
    // near-plane point on the same ray).
    const auto near_point = unproject_screen(camera, width, height, x, y, 0.0F);
    auto direction = near_point - camera.eye;
    const auto ray_length = length(direction);
    if (!(ray_length > 0.0F))
    {
        return std::nullopt;
    }
    direction = direction / ray_length;

    // Clip the ray to the map's bounding box so the march covers only the map.
    auto minimum_height = *std::ranges::min_element(field.heights);
    auto maximum_height = *std::ranges::max_element(field.heights);
    if (field.liquid_level)
    {
        minimum_height = std::min(minimum_height, *field.liquid_level);
        maximum_height = std::max(maximum_height, *field.liquid_level);
    }
    const Vector3 box_minimum{0.0F, 0.0F, minimum_height};
    const Vector3 box_maximum{static_cast<float>(field.width - 1) * field.spacing, static_cast<float>(field.height - 1) * field.spacing, maximum_height};
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
        return camera.eye + direction * t;
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
