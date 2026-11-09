#pragma once

#include "eaw/visual_scene.hpp"

#include <array>

namespace eaw {

// World-space bounds a camera frames and stays within.
struct CameraBounds
{
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
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
    std::array<float, 3> center_{};
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
    void set_view(std::array<float, 2> focus, float distance) noexcept;

    [[nodiscard]] std::array<float, 3> focus() const noexcept
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
    std::array<float, 3> focus_{};
    float yaw_{};
    float pitch_{};
    float distance_{1.0F};
};

} // namespace eaw
