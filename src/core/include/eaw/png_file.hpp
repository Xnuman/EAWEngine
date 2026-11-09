#pragma once

// PNG files of RGBA images, written uncompressed (stored deflate blocks, filter 0) so the engine needs
// no compression library. The reader accepts what the writer produces (8-bit RGBA, not interlaced,
// stored blocks, filter 0) and reports anything else as unsupported; it is for the engine's own
// captures, not for arbitrary PNG files.

#include "eaw/rgba_image.hpp"
#include "eaw/status.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace eaw {

[[nodiscard]] Result<std::vector<std::byte>> encode_png(const RgbaImage& image);
// Errors name the malformed or unsupported part (signature, chunk CRC, header, compression, filter).
[[nodiscard]] Result<RgbaImage> decode_png(std::span<const std::byte> bytes);

// Writes or reads a file; `overwrite` false refuses to replace an existing file.
[[nodiscard]] Status write_png(const std::filesystem::path& path, const RgbaImage& image, bool overwrite = false);
[[nodiscard]] Result<RgbaImage> read_png(const std::filesystem::path& path);

} // namespace eaw
