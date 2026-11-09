#pragma once

// Rotation quaternions (x, y, z vector part, w scalar part), as Alamo files store them.

#include "math/vector.hpp"

namespace eaw {

struct Quaternion
{
    float x{};
    float y{};
    float z{};
    float w{1.0F};

    friend constexpr bool operator==(const Quaternion&, const Quaternion&) = default;
};

// Hamilton product: the result applies `right` first, then `left`.
[[nodiscard]] constexpr Quaternion operator*(const Quaternion& left, const Quaternion& right) noexcept
{
    return {left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y, left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
            left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w, left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z};
}

[[nodiscard]] constexpr float dot(const Quaternion& a, const Quaternion& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

// Rotation by `radians` about a unit `axis`.
[[nodiscard]] Quaternion axis_angle(const Vector3& axis, float radians) noexcept;
// `q` scaled to unit length, or `fallback` when its length is zero or not finite.
[[nodiscard]] Quaternion normalize_or(const Quaternion& q, const Quaternion& fallback) noexcept;
// Spherical interpolation along the shorter arc; nearly parallel rotations are interpolated
// linearly and renormalized.
[[nodiscard]] Quaternion slerp(const Quaternion& from, Quaternion to, float amount) noexcept;

} // namespace eaw
