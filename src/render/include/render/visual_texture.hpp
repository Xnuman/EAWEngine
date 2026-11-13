#pragma once

// Textures as the renderer takes them: decoded mips in a GPU format, named for diagnostics and
// identified for reuse across scenes.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace eaw {

enum class VisualTextureFormat
{
    bc1_unorm,
    bc1_srgb,
    bc2_unorm,
    bc2_srgb,
    bc3_unorm,
    bc3_srgb,
    bc4_unorm,
    bc4_snorm,
    bc5_unorm,
    bc5_snorm,
    rgba8_unorm,
    rgba8_srgb,
    bgra8_unorm,
    bgra8_srgb,
    bgrx8_unorm,
    bgrx8_srgb,
    r8_unorm,
    a8_unorm,
    rg8_unorm,
};

struct VisualTextureMip
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t row_pitch{};
    std::size_t slice_pitch{};
    std::vector<std::byte> data;
};

struct VisualTexture
{
    std::string name;
    // Identity of the decoded texels. Two textures with the same non-empty identity are identical,
    // so a renderer may keep an uploaded texture when a replacement scene names it again. Empty
    // means "never reuse".
    std::string identity;
    VisualTextureFormat format{VisualTextureFormat::rgba8_unorm};
    std::vector<VisualTextureMip> mips;
};

inline constexpr std::uint32_t invalid_visual_texture = std::numeric_limits<std::uint32_t>::max();

} // namespace eaw
