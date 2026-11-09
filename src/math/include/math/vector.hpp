#pragma once

// Two-, three-, and four-component float vectors. Plain scalar code, so results are the same on
// every machine and build (the simulation's state hash depends on it).

#include <cmath>
#include <cstddef>

namespace eaw {

struct Vector2
{
    float x{};
    float y{};

    // Component by axis index (0 = x); an index past the last component reads the last one.
    [[nodiscard]] constexpr float operator[](const std::size_t axis) const noexcept
    {
        return axis == 0 ? x : y;
    }
    [[nodiscard]] constexpr float& operator[](const std::size_t axis) noexcept
    {
        return axis == 0 ? x : y;
    }
    friend constexpr bool operator==(const Vector2&, const Vector2&) = default;
};

struct Vector3
{
    float x{};
    float y{};
    float z{};

    // Component by axis index (0 = x); an index past the last component reads the last one.
    [[nodiscard]] constexpr float operator[](const std::size_t axis) const noexcept
    {
        return axis == 0 ? x : axis == 1 ? y : z;
    }
    [[nodiscard]] constexpr float& operator[](const std::size_t axis) noexcept
    {
        return axis == 0 ? x : axis == 1 ? y : z;
    }
    friend constexpr bool operator==(const Vector3&, const Vector3&) = default;
};

struct Vector4
{
    float x{};
    float y{};
    float z{};
    float w{};

    // Component by axis index (0 = x); an index past the last component reads the last one.
    [[nodiscard]] constexpr float operator[](const std::size_t axis) const noexcept
    {
        return axis == 0 ? x : axis == 1 ? y : axis == 2 ? z : w;
    }
    [[nodiscard]] constexpr float& operator[](const std::size_t axis) noexcept
    {
        return axis == 0 ? x : axis == 1 ? y : axis == 2 ? z : w;
    }
    friend constexpr bool operator==(const Vector4&, const Vector4&) = default;
};

// Vector2

[[nodiscard]] constexpr Vector2 operator+(const Vector2& a, const Vector2& b) noexcept
{
    return {a.x + b.x, a.y + b.y};
}

[[nodiscard]] constexpr Vector2 operator-(const Vector2& a, const Vector2& b) noexcept
{
    return {a.x - b.x, a.y - b.y};
}

[[nodiscard]] constexpr Vector2 operator-(const Vector2& a) noexcept
{
    return {-a.x, -a.y};
}

[[nodiscard]] constexpr Vector2 operator*(const Vector2& a, const float s) noexcept
{
    return {a.x * s, a.y * s};
}

[[nodiscard]] constexpr Vector2 operator*(const float s, const Vector2& a) noexcept
{
    return a * s;
}

[[nodiscard]] constexpr Vector2 operator/(const Vector2& a, const float s) noexcept
{
    return {a.x / s, a.y / s};
}

constexpr Vector2& operator+=(Vector2& a, const Vector2& b) noexcept
{
    return a = a + b;
}

constexpr Vector2& operator-=(Vector2& a, const Vector2& b) noexcept
{
    return a = a - b;
}

constexpr Vector2& operator*=(Vector2& a, const float s) noexcept
{
    return a = a * s;
}

[[nodiscard]] constexpr float dot(const Vector2& a, const Vector2& b) noexcept
{
    return a.x * b.x + a.y * b.y;
}

[[nodiscard]] inline float length(const Vector2& a) noexcept
{
    return std::sqrt(dot(a, a));
}

[[nodiscard]] inline bool is_finite(const Vector2& a) noexcept
{
    return std::isfinite(a.x) && std::isfinite(a.y);
}

// Vector3

[[nodiscard]] constexpr Vector3 operator+(const Vector3& a, const Vector3& b) noexcept
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] constexpr Vector3 operator-(const Vector3& a, const Vector3& b) noexcept
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] constexpr Vector3 operator-(const Vector3& a) noexcept
{
    return {-a.x, -a.y, -a.z};
}

[[nodiscard]] constexpr Vector3 operator*(const Vector3& a, const float s) noexcept
{
    return {a.x * s, a.y * s, a.z * s};
}

[[nodiscard]] constexpr Vector3 operator*(const float s, const Vector3& a) noexcept
{
    return a * s;
}

[[nodiscard]] constexpr Vector3 operator/(const Vector3& a, const float s) noexcept
{
    return {a.x / s, a.y / s, a.z / s};
}

constexpr Vector3& operator+=(Vector3& a, const Vector3& b) noexcept
{
    return a = a + b;
}

constexpr Vector3& operator-=(Vector3& a, const Vector3& b) noexcept
{
    return a = a - b;
}

constexpr Vector3& operator*=(Vector3& a, const float s) noexcept
{
    return a = a * s;
}

[[nodiscard]] constexpr float dot(const Vector3& a, const Vector3& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] constexpr Vector3 cross(const Vector3& a, const Vector3& b) noexcept
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] inline float length(const Vector3& a) noexcept
{
    return std::sqrt(dot(a, a));
}

[[nodiscard]] inline float distance(const Vector3& a, const Vector3& b) noexcept
{
    return length(a - b);
}

[[nodiscard]] inline bool is_finite(const Vector3& a) noexcept
{
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

// `a` scaled to unit length, or `fallback` when its length is zero or not finite.
[[nodiscard]] inline Vector3 normalize_or(const Vector3& a, const Vector3& fallback) noexcept
{
    const auto l = length(a);
    return l > 0.0F && std::isfinite(l) ? a / l : fallback;
}

// `a` scaled to unit length, or the zero vector when it has no direction.
[[nodiscard]] inline Vector3 normalize(const Vector3& a) noexcept
{
    return normalize_or(a, {});
}

[[nodiscard]] constexpr Vector3 lerp(const Vector3& a, const Vector3& b, const float amount) noexcept
{
    return {a.x + (b.x - a.x) * amount, a.y + (b.y - a.y) * amount, a.z + (b.z - a.z) * amount};
}

// Vector4

[[nodiscard]] constexpr Vector4 operator+(const Vector4& a, const Vector4& b) noexcept
{
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}

[[nodiscard]] constexpr Vector4 operator-(const Vector4& a, const Vector4& b) noexcept
{
    return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}

[[nodiscard]] constexpr Vector4 operator*(const Vector4& a, const float s) noexcept
{
    return {a.x * s, a.y * s, a.z * s, a.w * s};
}

[[nodiscard]] constexpr Vector4 operator*(const float s, const Vector4& a) noexcept
{
    return a * s;
}

[[nodiscard]] constexpr float dot(const Vector4& a, const Vector4& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

[[nodiscard]] constexpr Vector4 lerp(const Vector4& a, const Vector4& b, const float amount) noexcept
{
    return {a.x + (b.x - a.x) * amount, a.y + (b.y - a.y) * amount, a.z + (b.z - a.z) * amount, a.w + (b.w - a.w) * amount};
}

// GPU-facing structures embed these types, so their layout is fixed.
static_assert(sizeof(Vector2) == 2 * sizeof(float));
static_assert(sizeof(Vector3) == 3 * sizeof(float));
static_assert(sizeof(Vector4) == 4 * sizeof(float));

} // namespace eaw
