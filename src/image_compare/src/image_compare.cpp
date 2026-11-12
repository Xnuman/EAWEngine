#include "image_compare/image_compare.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace eaw {

Result<ImageDifference> compare_images(const RgbaImage& expected, const RgbaImage& actual, const std::uint8_t threshold)
{
    if (!expected.valid() || !actual.valid())
    {
        return make_error("image comparison needs two complete images");
    }
    if (expected.width != actual.width || expected.height != actual.height)
    {
        return make_error("images differ in size: " + std::to_string(expected.width) + "x" + std::to_string(expected.height) + " and " + std::to_string(actual.width) + "x" +
                          std::to_string(actual.height));
    }
    ImageDifference result;
    result.pixels = static_cast<std::uint64_t>(expected.width) * expected.height;
    result.visualization = expected;
    std::uint64_t total = 0;
    for (std::size_t pixel = 0; pixel < result.pixels; ++pixel)
    {
        const auto offset = pixel * 4U;
        std::uint32_t largest = 0;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const auto difference = static_cast<std::uint32_t>(std::abs(static_cast<int>(expected.pixels[offset + channel]) - static_cast<int>(actual.pixels[offset + channel])));
            total += difference;
            largest = std::max(largest, difference);
        }
        result.max_error = std::max(result.max_error, largest);
        auto* shown = &result.visualization.pixels[offset];
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            shown[channel] = static_cast<std::uint8_t>(shown[channel] / 4U);
        }
        shown[3] = 255;
        if (largest > threshold)
        {
            ++result.changed_pixels;
            shown[0] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, 64U + largest * 4U));
            shown[1] = 0;
            shown[2] = 0;
        }
    }
    result.mean_error = static_cast<double>(total) / static_cast<double>(result.pixels * 3U);
    return result;
}

} // namespace eaw
