#include "math/quaternion.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {
namespace {

// Above this cosine of the angle between two rotations, slerp's sine weights lose precision, so it
// interpolates linearly instead.
constexpr float slerp_linear_threshold = 0.9995F;

} // namespace

Quaternion axis_angle(const Vector3& axis, const float radians) noexcept
{
    const auto half = 0.5F * radians;
    const auto s = std::sin(half);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
}

Quaternion normalize_or(const Quaternion& q, const Quaternion& fallback) noexcept
{
    const auto l = std::sqrt(dot(q, q));
    return l > 0.0F && std::isfinite(l) ? Quaternion{q.x / l, q.y / l, q.z / l, q.w / l} : fallback;
}

Quaternion slerp(const Quaternion& from, Quaternion to, const float amount) noexcept
{
    auto cosine = dot(from, to);
    if (cosine < 0.0F)
    {
        cosine = -cosine;
        to = {-to.x, -to.y, -to.z, -to.w};
    }
    if (cosine > slerp_linear_threshold)
    {
        const Quaternion blended{from.x + (to.x - from.x) * amount, from.y + (to.y - from.y) * amount, from.z + (to.z - from.z) * amount, from.w + (to.w - from.w) * amount};
        return normalize_or(blended, blended);
    }
    const auto theta = std::acos(std::clamp(cosine, -1.0F, 1.0F));
    const auto sine = std::sin(theta);
    const auto from_weight = std::sin((1.0F - amount) * theta) / sine;
    const auto to_weight = std::sin(amount * theta) / sine;
    return {from.x * from_weight + to.x * to_weight, from.y * from_weight + to.y * to_weight, from.z * from_weight + to.z * to_weight, from.w * from_weight + to.w * to_weight};
}

} // namespace eaw
