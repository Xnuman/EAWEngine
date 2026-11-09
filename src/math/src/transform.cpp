#include "math/transform.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

Matrix4 compose_transform(const Vector3& scale, const Quaternion& rotation, const Vector3& translation) noexcept
{
    const auto xx = rotation.x * rotation.x;
    const auto yy = rotation.y * rotation.y;
    const auto zz = rotation.z * rotation.z;
    const auto xy = rotation.x * rotation.y;
    const auto xz = rotation.x * rotation.z;
    const auto yz = rotation.y * rotation.z;
    const auto wx = rotation.w * rotation.x;
    const auto wy = rotation.w * rotation.y;
    const auto wz = rotation.w * rotation.z;
    auto matrix = Matrix4::identity();
    matrix(0, 0) = (1.0F - 2.0F * (yy + zz)) * scale.x;
    matrix(0, 1) = (2.0F * (xy + wz)) * scale.x;
    matrix(0, 2) = (2.0F * (xz - wy)) * scale.x;
    matrix(1, 0) = (2.0F * (xy - wz)) * scale.y;
    matrix(1, 1) = (1.0F - 2.0F * (xx + zz)) * scale.y;
    matrix(1, 2) = (2.0F * (yz + wx)) * scale.y;
    matrix(2, 0) = (2.0F * (xz + wy)) * scale.z;
    matrix(2, 1) = (2.0F * (yz - wx)) * scale.z;
    matrix(2, 2) = (1.0F - 2.0F * (xx + yy)) * scale.z;
    matrix(3, 0) = translation.x;
    matrix(3, 1) = translation.y;
    matrix(3, 2) = translation.z;
    return matrix;
}

TransformParts decompose_transform(const Matrix4& matrix) noexcept
{
    const auto& m = matrix.values;
    TransformParts parts;
    parts.translation = translation(matrix);
    parts.scale = {length(Vector3{m[0], m[1], m[2]}), length(Vector3{m[4], m[5], m[6]}), length(Vector3{m[8], m[9], m[10]})};
    const auto determinant = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
    if (determinant < 0.0F)
    {
        parts.scale.x = -parts.scale.x;
    }
    if (parts.scale.x == 0.0F || parts.scale.y == 0.0F || parts.scale.z == 0.0F || !std::isfinite(determinant))
    {
        return parts;
    }
    // Rows of the rotation, as compose_transform writes them.
    const Matrix3 r{{m[0] / parts.scale.x, m[1] / parts.scale.x, m[2] / parts.scale.x, m[4] / parts.scale.y, m[5] / parts.scale.y, m[6] / parts.scale.y, m[8] / parts.scale.z, m[9] / parts.scale.z,
                     m[10] / parts.scale.z}};
    const auto trace = r(0, 0) + r(1, 1) + r(2, 2);
    auto q = Quaternion::identity();
    if (trace > 0.0F)
    {
        const auto s = 0.5F / std::sqrt(trace + 1.0F);
        q = {(r(1, 2) - r(2, 1)) * s, (r(2, 0) - r(0, 2)) * s, (r(0, 1) - r(1, 0)) * s, 0.25F / s};
    }
    else if (r(0, 0) > r(1, 1) && r(0, 0) > r(2, 2))
    {
        const auto s = 2.0F * std::sqrt(std::max(1.0F + r(0, 0) - r(1, 1) - r(2, 2), 0.0F));
        q = {0.25F * s, (r(0, 1) + r(1, 0)) / s, (r(0, 2) + r(2, 0)) / s, (r(1, 2) - r(2, 1)) / s};
    }
    else if (r(1, 1) > r(2, 2))
    {
        const auto s = 2.0F * std::sqrt(std::max(1.0F + r(1, 1) - r(0, 0) - r(2, 2), 0.0F));
        q = {(r(0, 1) + r(1, 0)) / s, 0.25F * s, (r(1, 2) + r(2, 1)) / s, (r(2, 0) - r(0, 2)) / s};
    }
    else
    {
        const auto s = 2.0F * std::sqrt(std::max(1.0F + r(2, 2) - r(0, 0) - r(1, 1), 0.0F));
        q = {(r(0, 2) + r(2, 0)) / s, (r(1, 2) + r(2, 1)) / s, 0.25F * s, (r(0, 1) - r(1, 0)) / s};
    }
    parts.rotation = normalize_or(q, parts.rotation);
    return parts;
}

} // namespace eaw
