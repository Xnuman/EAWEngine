#pragma once

// An 8-bit RGBA image (top row first, four bytes per pixel): what PNG reading and writing
// (formats/png_image.hpp) and the engine's screen captures hold. Image regression compares them
// (image_compare/image_compare.hpp).

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

} // namespace eaw
