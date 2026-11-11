#include "camera_controller.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace eaw {
namespace {

constexpr float orbit_default_yaw = -0.65F;
constexpr float orbit_default_pitch = 0.35F;
constexpr float orbit_pitch_limit = 1.45F;
constexpr float zoom_step = 0.88F;

float finite_or(const float value, const float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

Vector3 center_of(const CameraBounds& bounds) noexcept
{
    return {(bounds.minimum[0] + bounds.maximum[0]) * 0.5F, (bounds.minimum[1] + bounds.maximum[1]) * 0.5F, (bounds.minimum[2] + bounds.maximum[2]) * 0.5F};
}

// Bounds with inverted or non-finite axes collapse to the origin so cameras stay well defined.
CameraBounds sanitize(CameraBounds bounds) noexcept
{
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]) || bounds.minimum[axis] > bounds.maximum[axis])
        {
            bounds.minimum[axis] = 0.0F;
            bounds.maximum[axis] = 0.0F;
        }
    }
    return bounds;
}

VisualCamera look_from(const Vector3& target, const float yaw, const float pitch, const float distance, const float near_plane, const float far_plane, const float vertical_fov) noexcept
{
    const auto horizontal = std::cos(pitch) * distance;
    VisualCamera camera;
    camera.target = target;
    camera.eye = {target[0] + std::cos(yaw) * horizontal, target[1] + std::sin(yaw) * horizontal, target[2] + std::sin(pitch) * distance};
    camera.near_plane = near_plane;
    camera.far_plane = far_plane;
    camera.vertical_fov_degrees = vertical_fov;
    return camera;
}

} // namespace

OrbitCamera::OrbitCamera(const CameraBounds& bounds) noexcept
{
    const auto safe = sanitize(bounds);
    center_ = center_of(safe);
    const auto x = (safe.maximum[0] - safe.minimum[0]) * 0.5F;
    const auto y = (safe.maximum[1] - safe.minimum[1]) * 0.5F;
    const auto z = (safe.maximum[2] - safe.minimum[2]) * 0.5F;
    radius_ = std::max(std::sqrt(x * x + y * y + z * z), 0.001F);
    yaw_ = orbit_default_yaw;
    pitch_ = orbit_default_pitch;
}

void OrbitCamera::apply(const CameraInput& input) noexcept
{
    if (input.reset)
    {
        yaw_ = orbit_default_yaw;
        pitch_ = orbit_default_pitch;
        zoom_ = 1.0F;
    }
    yaw_ += finite_or(input.yaw, 0.0F);
    pitch_ = std::clamp(pitch_ + finite_or(input.pitch, 0.0F), -orbit_pitch_limit, orbit_pitch_limit);
    zoom_ = std::clamp(zoom_ * std::pow(1.12F, finite_or(input.zoom_steps, 0.0F)), 0.15F, 12.0F);
}

VisualCamera OrbitCamera::camera() const noexcept
{
    const auto near_plane = std::max(radius_ * 0.01F, 0.001F);
    return look_from(center_, yaw_, pitch_, radius_ * 2.6F / zoom_, near_plane, std::max(radius_ * 20.0F, near_plane + 1.0F), 55.0F);
}

TacticalCamera::TacticalCamera(const CameraBounds& bounds) noexcept : bounds_(sanitize(bounds))
{
    extent_ = std::max({bounds_.maximum[0] - bounds_.minimum[0], bounds_.maximum[1] - bounds_.minimum[1], 1.0F});
    minimum_distance_ = std::max(extent_ * 0.01F, 1.0F);
    maximum_distance_ = std::max(extent_ * 2.0F, minimum_distance_);
    reset();
}

void TacticalCamera::reset() noexcept
{
    focus_ = center_of(bounds_);
    // Look north (+Y) from the south edge, tilted well down toward the battle plane.
    yaw_ = -std::numbers::pi_v<float> * 0.5F;
    pitch_ = 0.95F;
    distance_ = std::clamp(extent_ * 0.75F, minimum_distance_, maximum_distance_);
}

void TacticalCamera::set_view(const Vector2 focus, const float distance) noexcept
{
    focus_[0] = std::clamp(finite_or(focus[0], focus_[0]), bounds_.minimum[0], bounds_.maximum[0]);
    focus_[1] = std::clamp(finite_or(focus[1], focus_[1]), bounds_.minimum[1], bounds_.maximum[1]);
    distance_ = std::clamp(finite_or(distance, distance_), minimum_distance_, maximum_distance_);
}

void TacticalCamera::apply(const CameraInput& input) noexcept
{
    if (input.reset)
    {
        reset();
    }
    yaw_ = std::remainder(yaw_ + finite_or(input.yaw, 0.0F), 2.0F * std::numbers::pi_v<float>);
    pitch_ = std::clamp(pitch_ + finite_or(input.pitch, 0.0F), minimum_pitch, maximum_pitch);
    distance_ = std::clamp(distance_ * std::pow(zoom_step, finite_or(input.zoom_steps, 0.0F)), minimum_distance_, maximum_distance_);

    // Screen-relative panning on the ground plane. Forward points from the eye toward the focus;
    // right is the view's right (forward x up).
    const Vector2 forward{-std::cos(yaw_), -std::sin(yaw_)};
    const Vector2 right{forward.y, -forward.x};
    const auto pan_right = finite_or(input.pan_right, 0.0F) * distance_;
    const auto pan_forward = finite_or(input.pan_forward, 0.0F) * distance_;
    focus_[0] = std::clamp(focus_[0] + right[0] * pan_right + forward[0] * pan_forward, bounds_.minimum[0], bounds_.maximum[0]);
    focus_[1] = std::clamp(focus_[1] + right[1] * pan_right + forward[1] * pan_forward, bounds_.minimum[1], bounds_.maximum[1]);
}

VisualCamera TacticalCamera::camera() const noexcept
{
    const auto near_plane = std::max(distance_ * 0.005F, 0.1F);
    return look_from(focus_, yaw_, pitch_, distance_, near_plane, distance_ + extent_ * 3.0F, 50.0F);
}

FlyCamera::FlyCamera(const VisualCamera& from) noexcept
    : eye_(from.eye), vertical_fov_degrees_(from.vertical_fov_degrees), near_plane_(std::min(from.near_plane, close_near_plane)), far_plane_(from.far_plane)
{
    const auto view = from.target - from.eye;
    yaw_ = std::atan2(view.y, view.x);
    pitch_ = std::clamp(std::atan2(view.z, std::hypot(view.x, view.y)), -maximum_pitch, maximum_pitch);
}

Vector3 FlyCamera::forward() const noexcept
{
    return {std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_)};
}

void FlyCamera::apply(const FlyInput& input) noexcept
{
    const auto finite = [](const float value)
    {
        return std::isfinite(value) ? value : 0.0F;
    };
    yaw_ = std::remainder(yaw_ + finite(input.yaw), 2.0F * std::numbers::pi_v<float>);
    pitch_ = std::clamp(pitch_ + finite(input.pitch), -maximum_pitch, maximum_pitch);
    const Vector3 right{std::sin(yaw_), -std::cos(yaw_), 0.0F};
    eye_ = eye_ + forward() * finite(input.forward) + right * finite(input.right) + Vector3{0.0F, 0.0F, finite(input.up)};
}

VisualCamera FlyCamera::camera() const noexcept
{
    VisualCamera camera;
    camera.eye = eye_;
    // A target well ahead: users of the camera read the eye-target distance as a view distance.
    camera.target = eye_ + forward() * fly_target_distance;
    camera.up = {0.0F, 0.0F, 1.0F};
    camera.vertical_fov_degrees = vertical_fov_degrees_;
    camera.near_plane = near_plane_;
    camera.far_plane = std::max(far_plane_, near_plane_ * 2.0F);
    return camera;
}

} // namespace eaw
