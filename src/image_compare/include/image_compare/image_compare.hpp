#pragma once

// Image regression: how two RGBA images differ (the capture checks, EAWEngine compare-images).

#include "core/status.hpp"
#include "formats/rgba_image.hpp"

#include <cstdint>

namespace eaw {

// How two images of the same size differ, by colour channel (alpha ignored).
struct ImageDifference
{
    // Mean absolute channel difference over every pixel and colour channel (0..255).
    double mean_error{};
    // Largest channel difference of any pixel.
    std::uint32_t max_error{};
    // Pixels whose largest channel difference exceeds the threshold.
    std::uint64_t changed_pixels{};
    std::uint64_t pixels{};
    // The first image dimmed to a quarter, with each changed pixel's difference in red (scaled
    // four times, so small differences show).
    RgbaImage visualization;
};

// Errors name a size mismatch or an image whose pixel buffer does not match its size.
[[nodiscard]] Result<ImageDifference> compare_images(const RgbaImage& expected, const RgbaImage& actual, std::uint8_t threshold = 8);

} // namespace eaw
