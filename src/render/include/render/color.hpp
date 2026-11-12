#pragma once

// Colors of the render contract: RGBA and RGB channels as floats, not clamped (emissive and
// lighting values above 1 are meaningful). What a value means (linear multiplier, authored color)
// is the field's to say. Plain aggregates: `Color{1.0F, 0.5F, 0.0F, 1.0F}`; the channels in
// order also by index, for code that treats them alike.

#include <cmath>
#include <cstddef>

namespace eaw {

struct Color
{
    float r{};
    float g{};
    float b{};
    float a{};

    [[nodiscard]] constexpr float& operator[](const std::size_t channel) noexcept
    {
        return channel == 0 ? r : channel == 1 ? g : channel == 2 ? b : a;
    }
    [[nodiscard]] constexpr float operator[](const std::size_t channel) const noexcept
    {
        return channel == 0 ? r : channel == 1 ? g : channel == 2 ? b : a;
    }
    friend constexpr bool operator==(const Color&, const Color&) = default;
};

struct ColorRgb
{
    float r{};
    float g{};
    float b{};

    [[nodiscard]] constexpr float& operator[](const std::size_t channel) noexcept
    {
        return channel == 0 ? r : channel == 1 ? g : b;
    }
    [[nodiscard]] constexpr float operator[](const std::size_t channel) const noexcept
    {
        return channel == 0 ? r : channel == 1 ? g : b;
    }
    friend constexpr bool operator==(const ColorRgb&, const ColorRgb&) = default;
};

// Uploaded to the GPU as they are (vertices, materials, particles).
static_assert(sizeof(Color) == 4 * sizeof(float));
static_assert(sizeof(ColorRgb) == 3 * sizeof(float));

inline constexpr Color white{1.0F, 1.0F, 1.0F, 1.0F};

[[nodiscard]] inline bool is_finite(const Color& color) noexcept
{
    return std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b) && std::isfinite(color.a);
}

[[nodiscard]] inline bool is_finite(const ColorRgb& color) noexcept
{
    return std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b);
}

} // namespace eaw
