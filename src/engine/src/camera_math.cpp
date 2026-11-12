#include "engine/camera_math.hpp"

#include "math/scalar.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {
namespace {

// A degenerate camera (eye on its target, or up along the view) looks along +Z.
constexpr Vector3 fallback_axis{0.0F, 0.0F, 1.0F};

// Right-handed look-at basis (as the renderer's): forward toward the target, right = forward x up,
// true up = right x forward.
struct Basis
{
    Vector3 right;
    Vector3 up;
    Vector3 forward;
};

Basis camera_basis(const VisualCamera& camera) noexcept
{
    const auto forward = normalize_or(camera.target - camera.eye, fallback_axis);
    const auto right = normalize_or(cross(forward, camera.up), fallback_axis);
    return {right, cross(right, forward), forward};
}

float tangent_half_fov(const VisualCamera& camera) noexcept
{
    return std::tan(camera.vertical_fov_degrees * pi / 360.0F);
}

// View-space direction (not normalized) through a client pixel at view depth 1.
Vector3 pixel_direction(const VisualCamera& camera, const float width, const float height, const float x, const float y) noexcept
{
    const auto aspect = width / std::max(height, 1.0F);
    const auto ndc_x = (x + 0.5F) / width * 2.0F - 1.0F;
    const auto ndc_y = 1.0F - (y + 0.5F) / height * 2.0F;
    const auto tangent = tangent_half_fov(camera);
    return {ndc_x * tangent * aspect, ndc_y * tangent, 1.0F};
}

Vector3 to_world(const VisualCamera& camera, const Vector3& view) noexcept
{
    const auto basis = camera_basis(camera);
    return camera.eye + basis.right * view.x + basis.up * view.y + basis.forward * view.z;
}

} // namespace

std::optional<Vector2> project_to_screen(const VisualCamera& camera, const float width, const float height, const Vector3& point)
{
    const auto basis = camera_basis(camera);
    const auto relative = point - camera.eye;
    const auto depth = dot(relative, basis.forward);
    if (depth <= 1.0e-6F)
    {
        return std::nullopt;
    }
    const auto tangent = tangent_half_fov(camera);
    const auto aspect = width / std::max(height, 1.0F);
    const auto ndc_x = dot(relative, basis.right) / (depth * tangent * aspect);
    const auto ndc_y = dot(relative, basis.up) / (depth * tangent);
    return Vector2{(ndc_x + 1.0F) * 0.5F * width - 0.5F, (1.0F - ndc_y) * 0.5F * height - 0.5F};
}

Vector3 unproject_screen(const VisualCamera& camera, const float width, const float height, const float x, const float y, const float depth)
{
    // Inverse of the perspective depth mapping: z_ndc = f / (f - n) - n f / ((f - n) z_view).
    const auto near_plane = camera.near_plane;
    const auto far_plane = camera.far_plane;
    const auto view_depth = near_plane * far_plane / (far_plane - depth * (far_plane - near_plane));
    return to_world(camera, pixel_direction(camera, width, height, x, y) * view_depth);
}

std::optional<Vector3> intersect_ground_plane(const VisualCamera& camera, const float width, const float height, const float x, const float y, const float plane_z)
{
    const auto direction = to_world(camera, pixel_direction(camera, width, height, x, y)) - camera.eye;
    if (std::fabs(direction.z) < 1.0e-6F)
    {
        return std::nullopt;
    }
    const auto t = (plane_z - camera.eye.z) / direction.z;
    if (t <= 0.0F)
    {
        return std::nullopt;
    }
    return Vector3{camera.eye.x + direction.x * t, camera.eye.y + direction.y * t, plane_z};
}

} // namespace eaw
