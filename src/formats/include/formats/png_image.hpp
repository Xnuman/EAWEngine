#pragma once

// PNG files of RGBA images, written uncompressed (stored deflate blocks, filter 0) so the engine needs
// no compression library. The reader accepts what the writer produces (8-bit RGBA, not interlaced,
// stored blocks, filter 0) and reports anything else as unsupported; it is for the engine's own
// captures, not for arbitrary PNG files.

#include "formats/rgba_image.hpp"
#include "core/status.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace eaw {

[[nodiscard]] Result<std::vector<std::byte>> encode_png(const RgbaImage& image);
// Errors name the malformed or unsupported part (signature, chunk CRC, header, compression, filter).
[[nodiscard]] Result<RgbaImage> decode_png(std::span<const std::byte> bytes);

} // namespace eaw
