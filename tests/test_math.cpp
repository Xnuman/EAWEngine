#include "test_support.hpp"

#include "math/matrix.hpp"
#include "math/quaternion.hpp"
#include "math/scalar.hpp"
#include "math/transform.hpp"
#include "math/vector.hpp"

#include <cmath>

namespace {

using namespace eaw_test;

constexpr float tolerance = 1.0e-5F;

bool close_to(const float a, const float b)
{
    return std::fabs(a - b) < tolerance;
}

bool close_to(const eaw::Vector3& a, const eaw::Vector3& b)
{
    return close_to(a.x, b.x) && close_to(a.y, b.y) && close_to(a.z, b.z);
}

bool close_to(const eaw::Quaternion& a, const eaw::Quaternion& b)
{
    return close_to(a.x, b.x) && close_to(a.y, b.y) && close_to(a.z, b.z) && close_to(a.w, b.w);
}

bool close_to(const eaw::Matrix4& a, const eaw::Matrix4& b)
{
    for (std::size_t i = 0; i < a.values.size(); ++i)
    {
        if (!close_to(a.values[i], b.values[i]))
        {
            return false;
        }
    }
    return true;
}

void test_math_vectors()
{
    const eaw::Vector3 x{1.0F, 0.0F, 0.0F};
    const eaw::Vector3 y{0.0F, 1.0F, 0.0F};
    require(eaw::cross(x, y) == eaw::Vector3{0.0F, 0.0F, 1.0F} && eaw::dot(x, y) == 0.0F, "cross and dot must follow the right-hand rule");
    require(close_to(eaw::length(eaw::Vector3{3.0F, 4.0F, 0.0F}), 5.0F) && close_to(eaw::normalize(eaw::Vector3{0.0F, 0.0F, 2.0F}), {0.0F, 0.0F, 1.0F}), "length and normalize");
    require(eaw::normalize(eaw::Vector3{}) == eaw::Vector3{} && eaw::normalize_or(eaw::Vector3{}, y) == y, "a zero vector must normalize to the fallback");
    require(eaw::is_finite(x) && !eaw::is_finite(eaw::Vector3{0.0F, std::nanf(""), 0.0F}), "is_finite must reject a non-finite component");
    eaw::Vector3 indexed{1.0F, 2.0F, 3.0F};
    indexed[2] = 4.0F;
    require(indexed[0] == 1.0F && indexed[1] == 2.0F && indexed.z == 4.0F && eaw::Vector4{1.0F, 2.0F, 3.0F, 5.0F}[3] == 5.0F, "index access must reach each component");
    require(eaw::lerp(x, y, 0.5F) == eaw::Vector3{0.5F, 0.5F, 0.0F} && (x * 2.0F - y) / 2.0F == eaw::Vector3{1.0F, -0.5F, 0.0F}, "vector arithmetic");
    require(close_to(eaw::degrees_to_radians(180.0F), eaw::pi) && close_to(eaw::radians_to_degrees(eaw::half_pi), 90.0F), "angle conversions");
}

void test_math_matrices()
{
    // Row vectors: x turns toward y about z, and a product applies its left factor first.
    const auto quarter = eaw::half_pi;
    require(close_to(eaw::transform({1.0F, 0.0F, 0.0F}, eaw::rotation_z(quarter)), {0.0F, 1.0F, 0.0F}) && close_to(eaw::transform({0.0F, 1.0F, 0.0F}, eaw::rotation_x(quarter)), {0.0F, 0.0F, 1.0F}) &&
                close_to(eaw::transform({0.0F, 0.0F, 1.0F}, eaw::rotation_y(quarter)), {1.0F, 0.0F, 0.0F}),
            "positive rotations must turn each axis toward the next");
    require(close_to(eaw::transform({1.0F, 0.0F, 0.0F}, eaw::rotation_z(quarter) * eaw::rotation_x(quarter)), {0.0F, 0.0F, 1.0F}), "a product must apply its left factor first");

    const auto transform = eaw::compose_transform({2.0F, 2.0F, 2.0F}, eaw::axis_angle({0.0F, 0.0F, 1.0F}, quarter), {10.0F, 0.0F, 5.0F});
    require(close_to(eaw::transform_point({1.0F, 0.0F, 0.0F}, transform), {10.0F, 2.0F, 5.0F}) && close_to(eaw::transform_direction({1.0F, 0.0F, 0.0F}, transform), {0.0F, 2.0F, 0.0F}) &&
                eaw::translation(transform) == eaw::Vector3{10.0F, 0.0F, 5.0F},
            "points are scaled, rotated, and translated; directions are not translated");
    const auto inverse = eaw::inverse(transform);
    require(inverse.has_value() && close_to(transform * *inverse, eaw::Matrix4{}), "a transform times its inverse must be the identity");
    eaw::Matrix4 singular;
    singular(2, 2) = 0.0F;
    require(!eaw::inverse(singular).has_value(), "a singular matrix must have no inverse");
}

void test_math_quaternions_and_transforms()
{
    const auto about_z = eaw::axis_angle({0.0F, 0.0F, 1.0F}, eaw::half_pi);
    require(close_to(about_z * about_z, eaw::axis_angle({0.0F, 0.0F, 1.0F}, eaw::pi)), "composing rotations about one axis must add their angles");
    require(close_to(eaw::slerp(eaw::Quaternion{}, eaw::axis_angle({0.0F, 0.0F, 1.0F}, eaw::pi * 0.5F), 0.5F), eaw::axis_angle({0.0F, 0.0F, 1.0F}, eaw::pi * 0.25F)),
            "slerp halfway must rotate half the angle");
    const eaw::Quaternion flipped{-about_z.x, -about_z.y, -about_z.z, -about_z.w};
    require(close_to(eaw::slerp(about_z, flipped, 0.5F), about_z), "slerp must take the shorter arc between equivalent rotations");

    const eaw::Vector3 scale{1.5F, 2.0F, 0.5F};
    const auto rotation = eaw::normalize_or(eaw::Quaternion{0.2F, -0.4F, 0.1F, 0.9F}, {});
    const eaw::Vector3 offset{3.0F, -7.0F, 11.0F};
    const auto parts = eaw::decompose_transform(eaw::compose_transform(scale, rotation, offset));
    require(close_to(parts.scale, scale) && close_to(parts.rotation, rotation) && close_to(parts.translation, offset), "decompose must recover the parts compose was given");
    auto mirrored = eaw::compose_transform(scale, rotation, offset);
    for (std::size_t column = 0; column < 3; ++column)
    {
        mirrored(0, column) = -mirrored(0, column);
    }
    const auto mirrored_parts = eaw::decompose_transform(mirrored);
    require(mirrored_parts.scale.x < 0.0F && close_to(eaw::compose_transform(mirrored_parts.scale, mirrored_parts.rotation, mirrored_parts.translation), mirrored),
            "a mirrored transform must decompose with a negative X scale that rebuilds it");
}

const ModuleRegistrar registrar("math", {
                                            {"test_math_vectors", &test_math_vectors},
                                            {"test_math_matrices", &test_math_matrices},
                                            {"test_math_quaternions_and_transforms", &test_math_quaternions_and_transforms},
                                        });

} // namespace
