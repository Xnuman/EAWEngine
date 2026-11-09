#pragma once

// Affine transforms built from, and split into, scale, rotation, and translation (the row-vector
// convention of math/matrix.hpp: scale, then rotate, then translate).

#include "math/matrix.hpp"
#include "math/quaternion.hpp"
#include "math/vector.hpp"

namespace eaw {

struct TransformParts
{
    Vector3 scale{1.0F, 1.0F, 1.0F};
    Quaternion rotation;
    Vector3 translation;
};

[[nodiscard]] Matrix4 compose_transform(const Vector3& scale, const Quaternion& rotation, const Vector3& translation) noexcept;

// Splits an affine transform so that compose_transform rebuilds it (exactly up to rounding when it
// has no shear). A mirrored matrix (negative determinant) gets a negative X scale. A matrix with a
// zero scale keeps the identity rotation.
[[nodiscard]] TransformParts decompose_transform(const Matrix4& matrix) noexcept;

} // namespace eaw
