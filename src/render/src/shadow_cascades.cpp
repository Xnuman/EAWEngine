#include "render/shadow_cascades.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eaw {
namespace {

// Slice ends closer than this to the camera are moved out to it (world units).
constexpr float minimum_near = 0.1F;
// Sphere radii are rounded up to this step, so floating-point noise does not change a cascade's
// size (and texel size) from frame to frame.
constexpr float radius_step = 1.0F / 16.0F;
// The light-space depth range is widened by this share at both ends, so casters on the scene's
// bounds are not clipped.
constexpr float depth_margin = 0.01F;

std::array<Vector3, 8> box_corners(const Vector3& minimum, const Vector3& maximum)
{
    return {Vector3{minimum.x, minimum.y, minimum.z}, Vector3{maximum.x, minimum.y, minimum.z}, Vector3{minimum.x, maximum.y, minimum.z}, Vector3{maximum.x, maximum.y, minimum.z},
            Vector3{minimum.x, minimum.y, maximum.z}, Vector3{maximum.x, minimum.y, maximum.z}, Vector3{minimum.x, maximum.y, maximum.z}, Vector3{maximum.x, maximum.y, maximum.z}};
}

} // namespace

ShadowCascades compute_shadow_cascades(const VisualCamera& camera, const float aspect, const Vector3& toward_light, const Vector3& scene_minimum, const Vector3& scene_maximum,
                                       const ShadowCascadeSettings& settings)
{
    ShadowCascades result;
    const auto light = normalize_or(toward_light, Vector3{});
    const auto view = normalize_or(camera.target - camera.eye, Vector3{});
    if (length(light) == 0.0F || length(view) == 0.0F || !is_finite(scene_minimum) || !is_finite(scene_maximum) || settings.resolution == 0 || !(aspect > 0.0F))
    {
        return result;
    }

    // The slices span the camera's planes, ending at the scene's far edge along the view direction
    // when that is nearer, rounded up to a step of sqrt 2 so that panning moves the end (and every
    // cascade's size) only now and then, and never past the maximum distance.
    const auto corners = box_corners(scene_minimum, scene_maximum);
    float scene_far = std::numeric_limits<float>::lowest();
    for (const auto& corner : corners)
    {
        scene_far = std::max(scene_far, dot(corner - camera.eye, view));
    }
    const float near_distance = std::max(camera.near_plane, minimum_near);
    const float stepped_far = scene_far > near_distance ? std::exp2(std::ceil(std::log2(scene_far) * 2.0F) * 0.5F) : 0.0F;
    const float far_distance = std::min({camera.far_plane, stepped_far, settings.maximum_distance});
    if (!(far_distance > near_distance))
    {
        return result;
    }

    // The camera's basis and the half extents of its view per unit of distance.
    const auto right = normalize_or(cross(view, camera.up), Vector3{1.0F, 0.0F, 0.0F});
    const auto up = cross(right, view);
    const float half_height = std::tan(camera.vertical_fov_degrees * 0.5F * 0.017453292F);
    const float half_width = half_height * aspect;

    // The light's basis: depth runs along the light's travel (away from it).
    const auto depth_axis = -light;
    const auto light_x = normalize_or(cross(std::fabs(depth_axis.z) < 0.99F ? Vector3{0.0F, 0.0F, 1.0F} : Vector3{0.0F, 1.0F, 0.0F}, depth_axis), Vector3{1.0F, 0.0F, 0.0F});
    const auto light_y = cross(depth_axis, light_x);

    const auto count = std::clamp<std::uint32_t>(settings.cascade_count, 1U, static_cast<std::uint32_t>(maximum_shadow_cascades));
    const float blend = std::clamp(settings.split_blend, 0.0F, 1.0F);
    float slice_near = near_distance;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const float share = static_cast<float>(index + 1U) / static_cast<float>(count);
        const float logarithmic = near_distance * std::pow(far_distance / near_distance, share);
        const float even = near_distance + (far_distance - near_distance) * share;
        const float slice_far = index + 1U == count ? far_distance : blend * logarithmic + (1.0F - blend) * even;

        // The slice's eight corners, and a sphere around them: the center is on the view axis
        // where the near and far corners are equally distant, kept within the slice.
        const float near_reach2 = (half_width * half_width + half_height * half_height) * slice_near * slice_near;
        const float far_reach2 = (half_width * half_width + half_height * half_height) * slice_far * slice_far;
        const float center_distance = std::clamp((slice_far * slice_far - slice_near * slice_near + far_reach2 - near_reach2) / (2.0F * (slice_far - slice_near)), slice_near, slice_far);
        const auto center = camera.eye + view * center_distance;
        float radius = 0.0F;
        for (const float distance_along : {slice_near, slice_far})
        {
            for (const float sx : {-1.0F, 1.0F})
            {
                for (const float sy : {-1.0F, 1.0F})
                {
                    const auto corner = camera.eye + view * distance_along + right * (sx * half_width * distance_along) + up * (sy * half_height * distance_along);
                    radius = std::max(radius, length(corner - center));
                }
            }
        }
        radius = std::ceil(radius / radius_step) * radius_step;
        const float texel = 2.0F * radius / static_cast<float>(settings.resolution);

        // The center in light space, snapped to whole texels.
        const float center_x = std::floor(dot(center, light_x) / texel) * texel;
        const float center_y = std::floor(dot(center, light_y) / texel) * texel;
        // Depth over the whole scene along the light, and the sphere.
        float depth_near = dot(center, depth_axis) - radius;
        float depth_far = dot(center, depth_axis) + radius;
        for (const auto& corner : corners)
        {
            depth_near = std::min(depth_near, dot(corner, depth_axis));
            depth_far = std::max(depth_far, dot(corner, depth_axis));
        }
        const float margin = (depth_far - depth_near) * depth_margin;
        depth_near -= margin;
        depth_far += margin;

        // World to clip: x and y over the sphere's square, depth over the range (row vectors).
        auto& cascade = result.cascades[index];
        auto& m = cascade.view_projection;
        m = Matrix4::identity();
        const float depth_scale = 1.0F / (depth_far - depth_near);
        for (std::size_t row = 0; row < 3; ++row)
        {
            m(row, 0) = light_x[row] / radius;
            m(row, 1) = light_y[row] / radius;
            m(row, 2) = depth_axis[row] * depth_scale;
            m(row, 3) = 0.0F;
        }
        m(3, 0) = -center_x / radius;
        m(3, 1) = -center_y / radius;
        m(3, 2) = -depth_near * depth_scale;
        m(3, 3) = 1.0F;
        cascade.split_distance = slice_far;
        cascade.texel_size = texel;
        slice_near = slice_far;
    }
    result.count = count;
    return result;
}

} // namespace eaw
