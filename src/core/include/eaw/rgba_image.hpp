#pragma once

// An 8-bit RGBA image (top row first, four bytes per pixel), the engine's screen captures, and the
// comparison image regression uses. PNG reading and writing is eaw/png_file.hpp.

#include "eaw/status.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eaw {

struct RgbaImage
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] bool valid() const noexcept
    {
        return width != 0 && height != 0 && pixels.size() == static_cast<std::size_t>(width) * height * 4U;
    }
};

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
