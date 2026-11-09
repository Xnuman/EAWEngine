#include "math/matrix.hpp"

#include <cmath>
#include <utility>

namespace eaw {
namespace {

// Gauss-Jordan elimination treats a pivot this small as zero: the matrix is singular.
constexpr float singular_pivot = 1.0e-8F;
// inverse_affine treats a 3x3 determinant this small as zero.
constexpr float singular_determinant = 1.0e-12F;

} // namespace

Matrix3 operator*(const Matrix3& left, const Matrix3& right) noexcept
{
    Matrix3 product;
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            product(row, column) = left(row, 0) * right(0, column) + left(row, 1) * right(1, column) + left(row, 2) * right(2, column);
        }
    }
    return product;
}

// Right-handed rotations of row vectors: a positive angle turns the next axis toward the one after
// it (x toward y about z, y toward z about x, z toward x about y).
Matrix3 rotation_x(const float radians) noexcept
{
    const auto c = std::cos(radians);
    const auto s = std::sin(radians);
    return {{1.0F, 0.0F, 0.0F, 0.0F, c, s, 0.0F, -s, c}};
}

Matrix3 rotation_y(const float radians) noexcept
{
    const auto c = std::cos(radians);
    const auto s = std::sin(radians);
    return {{c, 0.0F, -s, 0.0F, 1.0F, 0.0F, s, 0.0F, c}};
}

Matrix3 rotation_z(const float radians) noexcept
{
    const auto c = std::cos(radians);
    const auto s = std::sin(radians);
    return {{c, s, 0.0F, -s, c, 0.0F, 0.0F, 0.0F, 1.0F}};
}

Matrix4 operator*(const Matrix4& left, const Matrix4& right) noexcept
{
    Matrix4 product;
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            float value = 0.0F;
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                value += left(row, inner) * right(inner, column);
            }
            product(row, column) = value;
        }
    }
    return product;
}

std::optional<Matrix4> inverse(const Matrix4& matrix) noexcept
{
    // The matrix beside the identity; row operations turn the left half into the identity and the
    // right half into the inverse.
    constexpr std::size_t size = 4;
    float augmented[size][2 * size]{};
    for (std::size_t row = 0; row < size; ++row)
    {
        for (std::size_t column = 0; column < size; ++column)
        {
            augmented[row][column] = matrix(row, column);
        }
        augmented[row][row + size] = 1.0F;
    }
    for (std::size_t pivot = 0; pivot < size; ++pivot)
    {
        auto best = pivot;
        for (std::size_t row = pivot + 1; row < size; ++row)
        {
            if (std::fabs(augmented[row][pivot]) > std::fabs(augmented[best][pivot]))
            {
                best = row;
            }
        }
        if (std::fabs(augmented[best][pivot]) < singular_pivot)
        {
            return std::nullopt;
        }
        if (best != pivot)
        {
            for (std::size_t column = 0; column < 2 * size; ++column)
            {
                std::swap(augmented[pivot][column], augmented[best][column]);
            }
        }
        const auto divisor = augmented[pivot][pivot];
        for (float& value : augmented[pivot])
        {
            value /= divisor;
        }
        for (std::size_t row = 0; row < size; ++row)
        {
            if (row == pivot)
            {
                continue;
            }
            const auto factor = augmented[row][pivot];
            for (std::size_t column = 0; column < 2 * size; ++column)
            {
                augmented[row][column] -= factor * augmented[pivot][column];
            }
        }
    }
    Matrix4 result;
    for (std::size_t row = 0; row < size; ++row)
    {
        for (std::size_t column = 0; column < size; ++column)
        {
            result(row, column) = augmented[row][column + size];
        }
    }
    return result;
}

std::optional<Matrix4> inverse_affine(const Matrix4& matrix) noexcept
{
    const auto& m = matrix.values;
    const auto a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const auto c00 = e * i - f * h;
    const auto c01 = c * h - b * i;
    const auto c02 = b * f - c * e;
    const auto determinant = a * c00 + d * c01 + g * c02;
    if (std::abs(determinant) < singular_determinant)
    {
        return std::nullopt;
    }
    const auto s = 1.0F / determinant;
    Matrix4 result;
    auto& r = result.values;
    r[0] = c00 * s;
    r[1] = c01 * s;
    r[2] = c02 * s;
    r[4] = (f * g - d * i) * s;
    r[5] = (a * i - c * g) * s;
    r[6] = (c * d - a * f) * s;
    r[8] = (d * h - e * g) * s;
    r[9] = (b * g - a * h) * s;
    r[10] = (a * e - b * d) * s;
    r[12] = -(m[12] * r[0] + m[13] * r[4] + m[14] * r[8]);
    r[13] = -(m[12] * r[1] + m[13] * r[5] + m[14] * r[9]);
    r[14] = -(m[12] * r[2] + m[13] * r[6] + m[14] * r[10]);
    r[15] = 1.0F;
    return result;
}

} // namespace eaw
