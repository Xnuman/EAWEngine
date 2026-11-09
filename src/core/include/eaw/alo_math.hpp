#pragma once

// Vectors, quaternions, and 4x4 matrices as Alamo files store them (row-vector convention,
// translation in elements 12..14), and the transforms the decoders and the engine compute with
// them.

#include "eaw/status.hpp"

#include <array>

namespace eaw {

struct AloVector3
{
    float x{};
    float y{};
    float z{};
};

struct AloVector2
{
    float x{};
    float y{};
};

struct AloVector4
{
    float x{};
    float y{};
    float z{};
    float w{};
};

struct AloQuaternion
{
    float x{};
    float y{};
    float z{};
    float w{1.0F};
};

struct AloMatrix4
{
    std::array<float, 16> values{};
};

[[nodiscard]] AloMatrix4 alo_identity_matrix() noexcept;
[[nodiscard]] AloMatrix4 alo_multiply(const AloMatrix4& left, const AloMatrix4& right) noexcept;
// An error when the matrix is singular.
[[nodiscard]] Result<AloMatrix4> alo_inverse(const AloMatrix4& matrix);
[[nodiscard]] AloVector3 alo_transform_point(const AloVector3& point, const AloMatrix4& matrix) noexcept;
[[nodiscard]] AloMatrix4 alo_compose_transform(const AloVector3& scale, const AloQuaternion& rotation, const AloVector3& translation) noexcept;

// Splits an affine transform into scale, rotation, and translation, so that alo_compose_transform
// rebuilds it (exactly up to rounding when it has no shear). A mirrored matrix (negative
// determinant) gets a negative X scale.
struct AloTransformParts
{
    AloVector3 scale{1.0F, 1.0F, 1.0F};
    AloQuaternion rotation;
    AloVector3 translation;
};
[[nodiscard]] AloTransformParts alo_decompose_transform(const AloMatrix4& matrix) noexcept;

} // namespace eaw
