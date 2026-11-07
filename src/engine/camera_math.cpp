#include "camera_math.hpp"

#include <cmath>
#include <numbers>

namespace eaw {
namespace {

using Vector = std::array<float, 3>;

Vector subtract(const Vector& a, const Vector& b) noexcept
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Vector cross(const Vector& a, const Vector& b) noexcept
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

float dot(const Vector& a, const Vector& b) noexcept
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vector normalize(const Vector& value) noexcept
{
    const auto length = std::sqrt(dot(value, value));
    return length > 1.0e-12F ? Vector{value[0] / length, value[1] / length, value[2] / length} : Vector{0.0F, 0.0F, 1.0F};
}

// Left-handed look-at basis: forward toward the target, right = up x forward, true up = forward x right.
struct Basis
{
    Vector right;
    Vector up;
    Vector forward;
};

Basis camera_basis(const VisualCamera& camera) noexcept
{
    const auto forward = normalize(subtract(camera.target, camera.eye));
    const auto right = normalize(cross(camera.up, forward));
    return {right, cross(forward, right), forward};
}

float tangent_half_fov(const VisualCamera& camera) noexcept
{
    return std::tan(camera.vertical_fov_degrees * std::numbers::pi_v<float> / 360.0F);
}

// View-space direction (not normalized) through a client pixel at view depth 1.
Vector pixel_direction(const VisualCamera& camera, const float width, const float height, const float x, const float y) noexcept
{
    const auto aspect = width / std::max(height, 1.0F);
    const auto ndc_x = (x + 0.5F) / width * 2.0F - 1.0F;
    const auto ndc_y = 1.0F - (y + 0.5F) / height * 2.0F;
    const auto tangent = tangent_half_fov(camera);
    return {ndc_x * tangent * aspect, ndc_y * tangent, 1.0F};
}

Vector to_world(const VisualCamera& camera, const Vector& view) noexcept
{
    const auto basis = camera_basis(camera);
    return {camera.eye[0] + basis.right[0] * view[0] + basis.up[0] * view[1] + basis.forward[0] * view[2],
            camera.eye[1] + basis.right[1] * view[0] + basis.up[1] * view[1] + basis.forward[1] * view[2],
            camera.eye[2] + basis.right[2] * view[0] + basis.up[2] * view[1] + basis.forward[2] * view[2]};
}

} // namespace

std::optional<std::array<float, 2>> project_to_screen(const VisualCamera& camera, const float width, const float height, const std::array<float, 3>& point)
{
    const auto basis = camera_basis(camera);
    const auto relative = subtract(point, camera.eye);
    const auto depth = dot(relative, basis.forward);
    if (depth <= 1.0e-6F)
    {
        return std::nullopt;
    }
    const auto tangent = tangent_half_fov(camera);
    const auto aspect = width / std::max(height, 1.0F);
    const auto ndc_x = dot(relative, basis.right) / (depth * tangent * aspect);
    const auto ndc_y = dot(relative, basis.up) / (depth * tangent);
    return std::array<float, 2>{(ndc_x + 1.0F) * 0.5F * width - 0.5F, (1.0F - ndc_y) * 0.5F * height - 0.5F};
}

std::array<float, 3> unproject_screen(const VisualCamera& camera, const float width, const float height, const float x, const float y, const float depth)
{
    // Inverse of the perspective depth mapping: z_ndc = f / (f - n) - n f / ((f - n) z_view).
    const auto near_plane = camera.near_plane;
    const auto far_plane = camera.far_plane;
    const auto view_depth = near_plane * far_plane / (far_plane - depth * (far_plane - near_plane));
    auto direction = pixel_direction(camera, width, height, x, y);
    for (auto& component : direction)
    {
        component *= view_depth;
    }
    return to_world(camera, direction);
}

std::optional<std::array<float, 3>> intersect_ground_plane(const VisualCamera& camera, const float width, const float height, const float x, const float y, const float plane_z)
{
    const auto far_point = to_world(camera, pixel_direction(camera, width, height, x, y));
    const auto direction = subtract(far_point, camera.eye);
    if (std::fabs(direction[2]) < 1.0e-6F)
    {
        return std::nullopt;
    }
    const auto t = (plane_z - camera.eye[2]) / direction[2];
    if (t <= 0.0F)
    {
        return std::nullopt;
    }
    return std::array<float, 3>{camera.eye[0] + direction[0] * t, camera.eye[1] + direction[1] * t, plane_z};
}

} // namespace eaw
