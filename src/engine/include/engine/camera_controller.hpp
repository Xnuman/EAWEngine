#pragma once

#include "render/visual_scene.hpp"

namespace eaw {

// World-space bounds a camera frames and stays within.
struct CameraBounds
{
    Vector3 minimum{};
    Vector3 maximum{};
};

// One frame of camera intent, already converted from device input. Angles are radians, zoom is
// in wheel steps (positive moves closer), and pan is in screen-relative units: +right/+forward,
// where 1.0 moves the focus by the current view distance.
struct CameraInput
{
    float yaw{};
    float pitch{};
    float pan_right{};
    float pan_forward{};
    float zoom_steps{};
    bool reset{};
};

// Orbits a fixed target at the bounds center (model inspection).
class OrbitCamera final
{
public:
    explicit OrbitCamera(const CameraBounds& bounds) noexcept;

    void apply(const CameraInput& input) noexcept;
    [[nodiscard]] VisualCamera camera() const noexcept;

private:
    Vector3 center_{};
    float radius_{1.0F};
    float yaw_{};
    float pitch_{};
    float zoom_{1.0F};
};

// Tactical view over a map: a focus point on the map's ground plane that pans (relative to the
// current heading) and stays inside the map bounds, with heading, a limited pitch range, and a
// zoom distance clamped to the map size. Defaults and limits are engine choices, not values
// measured from the original game.
class TacticalCamera final
{
public:
    explicit TacticalCamera(const CameraBounds& bounds) noexcept;

    void apply(const CameraInput& input) noexcept;
    [[nodiscard]] VisualCamera camera() const noexcept;
    // Places the view: `focus` (x, y; clamped to the bounds, height kept) seen from `distance`
    // (clamped to the camera's range), heading and pitch unchanged.
    void set_view(Vector2 focus, float distance) noexcept;

    [[nodiscard]] Vector3 focus() const noexcept
    {
        return focus_;
    }
    [[nodiscard]] float distance() const noexcept
    {
        return distance_;
    }
    [[nodiscard]] float minimum_distance() const noexcept
    {
        return minimum_distance_;
    }
    [[nodiscard]] float maximum_distance() const noexcept
    {
        return maximum_distance_;
    }

    static constexpr float minimum_pitch = 0.25F;
    static constexpr float maximum_pitch = 1.45F;

private:
    void reset() noexcept;

    CameraBounds bounds_{};
    float extent_{1.0F};
    float minimum_distance_{1.0F};
    float maximum_distance_{1.0F};
    Vector3 focus_{};
    float yaw_{};
    float pitch_{};
    float distance_{1.0F};
};

// A free flying camera for close inspection: an eye that moves along its own view (forward,
// right, and world up) and turns by yaw and pitch, with no bounds. It keeps the field of view and
// depth range it was given, with the near plane brought close for inspecting small parts.
struct FlyInput
{
    // Radians.
    float yaw{};
    float pitch{};
    // World units along the view's right, its forward, and world up (+Z).
    float right{};
    float forward{};
    float up{};
};

class FlyCamera final
{
public:
    // Starts at `from`'s eye looking at its target.
    explicit FlyCamera(const VisualCamera& from) noexcept;

    void apply(const FlyInput& input) noexcept;
    [[nodiscard]] VisualCamera camera() const noexcept;

    // Pitch stays short of straight up or down, where the view's right is undefined.
    static constexpr float maximum_pitch = 1.5F;
    // The nearest the near plane comes (world units; an engine choice for close inspection).
    static constexpr float close_near_plane = 0.5F;
    // How far ahead of the eye the view's target is put (world units; an engine choice).
    static constexpr float fly_target_distance = 100.0F;

private:
    [[nodiscard]] Vector3 forward() const noexcept;

    Vector3 eye_{};
    float yaw_{};
    float pitch_{};
    float vertical_fov_degrees_{};
    float near_plane_{};
    float far_plane_{};
};

} // namespace eaw
