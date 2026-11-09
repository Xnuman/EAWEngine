// Matrix and quaternion math for Alamo transforms (eaw/alo_math.hpp).

#include "eaw/alo_math.hpp"

#include <algorithm>
#include <cctype>
#include <bit>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

namespace eaw {

AloMatrix4 alo_identity_matrix() noexcept
{
    AloMatrix4 matrix;
    matrix.values[0] = 1.0F;
    matrix.values[5] = 1.0F;
    matrix.values[10] = 1.0F;
    matrix.values[15] = 1.0F;
    return matrix;
}

AloMatrix4 alo_multiply(const AloMatrix4& left, const AloMatrix4& right) noexcept
{
    AloMatrix4 result;
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            float value = 0.0F;
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                value += left.values[row * 4 + inner] * right.values[inner * 4 + column];
            }
            result.values[row * 4 + column] = value;
        }
    }
    return result;
}

Result<AloMatrix4> alo_inverse(const AloMatrix4& matrix)
{
    float augmented[4][8]{};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            augmented[row][column] = matrix.values[row * 4 + column];
        }
        augmented[row][row + 4] = 1.0F;
    }
    for (std::size_t pivot = 0; pivot < 4; ++pivot)
    {
        auto best = pivot;
        for (std::size_t row = pivot + 1; row < 4; ++row)
        {
            if (std::fabs(augmented[row][pivot]) > std::fabs(augmented[best][pivot]))
            {
                best = row;
            }
        }
        if (std::fabs(augmented[best][pivot]) < 1.0e-8F)
        {
            return make_error("ALO bone transform is singular");
        }
        if (best != pivot)
        {
            for (std::size_t column = 0; column < 8; ++column)
            {
                std::swap(augmented[pivot][column], augmented[best][column]);
            }
        }
        const auto divisor = augmented[pivot][pivot];
        for (float& value : augmented[pivot])
        {
            value /= divisor;
        }
        for (std::size_t row = 0; row < 4; ++row)
        {
            if (row == pivot)
            {
                continue;
            }
            const auto factor = augmented[row][pivot];
            for (std::size_t column = 0; column < 8; ++column)
            {
                augmented[row][column] -= factor * augmented[pivot][column];
            }
        }
    }
    AloMatrix4 result;
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            result.values[row * 4 + column] = augmented[row][column + 4];
        }
    }
    return result;
}

AloVector3 alo_transform_point(const AloVector3& point, const AloMatrix4& matrix) noexcept
{
    return {point.x * matrix.values[0] + point.y * matrix.values[4] + point.z * matrix.values[8] + matrix.values[12],
            point.x * matrix.values[1] + point.y * matrix.values[5] + point.z * matrix.values[9] + matrix.values[13],
            point.x * matrix.values[2] + point.y * matrix.values[6] + point.z * matrix.values[10] + matrix.values[14]};
}

AloTransformParts alo_decompose_transform(const AloMatrix4& matrix) noexcept
{
    const auto& m = matrix.values;
    AloTransformParts parts;
    parts.translation = {m[12], m[13], m[14]};
    const auto length = [](const float x, const float y, const float z)
    {
        return std::sqrt(x * x + y * y + z * z);
    };
    parts.scale = {length(m[0], m[1], m[2]), length(m[4], m[5], m[6]), length(m[8], m[9], m[10])};
    const auto determinant = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
    if (determinant < 0.0F)
    {
        parts.scale.x = -parts.scale.x;
    }
    if (parts.scale.x == 0.0F || parts.scale.y == 0.0F || parts.scale.z == 0.0F || !std::isfinite(determinant))
    {
        return parts;
    }
    // Rows of the rotation (row-vector convention, as alo_compose_transform writes them).
    const std::array<float, 9> r{m[0] / parts.scale.x, m[1] / parts.scale.x, m[2] / parts.scale.x, m[4] / parts.scale.y, m[5] / parts.scale.y,
                                 m[6] / parts.scale.y, m[8] / parts.scale.z, m[9] / parts.scale.z, m[10] / parts.scale.z};
    const auto trace = r[0] + r[4] + r[8];
    AloQuaternion q;
    if (trace > 0.0F)
    {
        const auto s = 0.5F / std::sqrt(trace + 1.0F);
        q = {(r[5] - r[7]) * s, (r[6] - r[2]) * s, (r[1] - r[3]) * s, 0.25F / s};
    }
    else if (r[0] > r[4] && r[0] > r[8])
    {
        const auto s = 2.0F * std::sqrt(std::max(1.0F + r[0] - r[4] - r[8], 0.0F));
        q = {0.25F * s, (r[1] + r[3]) / s, (r[2] + r[6]) / s, (r[5] - r[7]) / s};
    }
    else if (r[4] > r[8])
    {
        const auto s = 2.0F * std::sqrt(std::max(1.0F + r[4] - r[0] - r[8], 0.0F));
        q = {(r[1] + r[3]) / s, 0.25F * s, (r[5] + r[7]) / s, (r[6] - r[2]) / s};
    }
    else
    {
        const auto s = 2.0F * std::sqrt(std::max(1.0F + r[8] - r[0] - r[4], 0.0F));
        q = {(r[2] + r[6]) / s, (r[5] + r[7]) / s, 0.25F * s, (r[1] - r[3]) / s};
    }
    const auto norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (norm > 0.0F && std::isfinite(norm))
    {
        parts.rotation = {q.x / norm, q.y / norm, q.z / norm, q.w / norm};
    }
    return parts;
}

AloMatrix4 alo_compose_transform(const AloVector3& scale, const AloQuaternion& rotation, const AloVector3& translation) noexcept
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
    auto matrix = alo_identity_matrix();
    matrix.values[0] = (1.0F - 2.0F * (yy + zz)) * scale.x;
    matrix.values[1] = (2.0F * (xy + wz)) * scale.x;
    matrix.values[2] = (2.0F * (xz - wy)) * scale.x;
    matrix.values[4] = (2.0F * (xy - wz)) * scale.y;
    matrix.values[5] = (1.0F - 2.0F * (xx + zz)) * scale.y;
    matrix.values[6] = (2.0F * (yz + wx)) * scale.y;
    matrix.values[8] = (2.0F * (xz + wy)) * scale.z;
    matrix.values[9] = (2.0F * (yz - wx)) * scale.z;
    matrix.values[10] = (1.0F - 2.0F * (xx + yy)) * scale.z;
    matrix.values[12] = translation.x;
    matrix.values[13] = translation.y;
    matrix.values[14] = translation.z;
    return matrix;
}

} // namespace eaw
