#pragma once

// Cascaded shadow maps for the key light: the camera's view, cut into depth slices between its near
// plane and the far edge of the scene, each covered by an orthographic shadow map looking along the
// light. Each cascade is a bounding sphere of its slice, so its size does not change as the camera
// turns, and its origin is snapped to whole shadow-map texels, so its texels do not slide as the
// camera pans: both keep shadow edges from shimmering. Depth spans the whole scene along the light,
// so casters outside the view still shadow what is in it. No graphics API; every number is an
// engine choice.

#include "math/matrix.hpp"
#include "math/vector.hpp"
#include "render/visual_camera.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace eaw {

inline constexpr std::size_t maximum_shadow_cascades = 4;

struct ShadowCascadeSettings
{
    std::uint32_t cascade_count{3};
    // Texels along each side of a cascade's map.
    std::uint32_t resolution{2048};
    // Split placement between even (0) and logarithmic (1) spacing.
    float split_blend{0.75F};
    // Shadows end this far from the camera at most (world units; the scene's far edge when nearer).
    float maximum_distance{20000.0F};
};

struct ShadowCascade
{
    // World to the cascade's clip space (row-vector convention): x and y in -1..1, depth 0..1.
    Matrix4 view_projection{Matrix4::identity()};
    // The far end of the cascade's slice, as distance along the camera's view direction.
    float split_distance{};
    // The world size of one shadow-map texel.
    float texel_size{};
};

struct ShadowCascades
{
    std::array<ShadowCascade, maximum_shadow_cascades> cascades;
    std::uint32_t count{};
};

// The cascades for `camera` (its aspect ratio given) and a key light shining from
// `toward_light` (from surfaces toward the light, any length), over a scene within
// `scene_minimum`..`scene_maximum`. None when the inputs are degenerate (no light direction, an
// empty scene in front of the camera).
[[nodiscard]] ShadowCascades compute_shadow_cascades(const VisualCamera& camera, float aspect, const Vector3& toward_light, const Vector3& scene_minimum, const Vector3& scene_maximum,
                                                     const ShadowCascadeSettings& settings);

} // namespace eaw
