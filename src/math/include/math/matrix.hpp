#pragma once

// 3x3 and 4x4 float matrices in the row-vector convention (p' = p * M), stored row-major: a 4x4
// affine transform keeps its translation in elements 12..14, as Alamo files and the renderer's
// constant buffers do. A product `a * b` applies `a` first, then `b`.

#include "math/vector.hpp"

#include <array>
#include <cstddef>
#include <optional>

namespace eaw {

struct Matrix3
{
    std::array<float, 9> values{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};

    [[nodiscard]] constexpr float operator()(const std::size_t row, const std::size_t column) const noexcept
    {
        return values[row * 3 + column];
    }
    [[nodiscard]] constexpr float& operator()(const std::size_t row, const std::size_t column) noexcept
    {
        return values[row * 3 + column];
    }
    friend constexpr bool operator==(const Matrix3&, const Matrix3&) = default;
};

struct Matrix4
{
    std::array<float, 16> values{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};

    [[nodiscard]] constexpr float operator()(const std::size_t row, const std::size_t column) const noexcept
    {
        return values[row * 4 + column];
    }
    [[nodiscard]] constexpr float& operator()(const std::size_t row, const std::size_t column) noexcept
    {
        return values[row * 4 + column];
    }
    friend constexpr bool operator==(const Matrix4&, const Matrix4&) = default;
};

static_assert(sizeof(Matrix4) == 16 * sizeof(float));

// Matrix3: rotations and products.

[[nodiscard]] Matrix3 operator*(const Matrix3& left, const Matrix3& right) noexcept;
[[nodiscard]] Matrix3 rotation_x(float radians) noexcept;
[[nodiscard]] Matrix3 rotation_y(float radians) noexcept;
[[nodiscard]] Matrix3 rotation_z(float radians) noexcept;

[[nodiscard]] constexpr Vector3 transform(const Vector3& v, const Matrix3& m) noexcept
{
    return {v.x * m.values[0] + v.y * m.values[3] + v.z * m.values[6], v.x * m.values[1] + v.y * m.values[4] + v.z * m.values[7], v.x * m.values[2] + v.y * m.values[5] + v.z * m.values[8]};
}

// Matrix4: products, inverse, and point and direction transforms.

[[nodiscard]] Matrix4 operator*(const Matrix4& left, const Matrix4& right) noexcept;
// Nothing when the matrix is singular.
[[nodiscard]] std::optional<Matrix4> inverse(const Matrix4& matrix) noexcept;

// A position: rotated, scaled, and translated.
[[nodiscard]] constexpr Vector3 transform_point(const Vector3& p, const Matrix4& m) noexcept
{
    return {p.x * m.values[0] + p.y * m.values[4] + p.z * m.values[8] + m.values[12], p.x * m.values[1] + p.y * m.values[5] + p.z * m.values[9] + m.values[13],
            p.x * m.values[2] + p.y * m.values[6] + p.z * m.values[10] + m.values[14]};
}

// A direction: rotated and scaled, not translated.
[[nodiscard]] constexpr Vector3 transform_direction(const Vector3& d, const Matrix4& m) noexcept
{
    return {d.x * m.values[0] + d.y * m.values[4] + d.z * m.values[8], d.x * m.values[1] + d.y * m.values[5] + d.z * m.values[9], d.x * m.values[2] + d.y * m.values[6] + d.z * m.values[10]};
}

[[nodiscard]] constexpr Vector3 translation(const Matrix4& m) noexcept
{
    return {m.values[12], m.values[13], m.values[14]};
}

} // namespace eaw
