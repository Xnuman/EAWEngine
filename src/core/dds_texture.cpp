#include "eaw/dds_texture.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace eaw {
namespace {

constexpr std::uint32_t fourcc(const char a, const char b, const char c, const char d) noexcept
{
    return static_cast<std::uint32_t>(a) | (static_cast<std::uint32_t>(b) << 8U) | (static_cast<std::uint32_t>(c) << 16U) | (static_cast<std::uint32_t>(d) << 24U);
}

std::uint32_t read_u32(const std::span<const std::byte> bytes, const std::size_t offset)
{
    if (offset + 4 > bytes.size())
    {
        throw std::runtime_error("truncated DDS header");
    }
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + shift / 8])) << shift;
    }
    return value;
}

std::size_t checked_multiply(const std::size_t left, const std::size_t right)
{
    if (right != 0 && left > std::numeric_limits<std::size_t>::max() / right)
    {
        throw std::runtime_error("DDS dimensions overflow the address space");
    }
    return left * right;
}

DdsFormat dxgi_to_format(const std::uint32_t format) noexcept
{
    switch (format)
    {
    case 28:
        return DdsFormat::rgba8_unorm;
    case 29:
        return DdsFormat::rgba8_srgb;
    case 49:
        return DdsFormat::rg8_unorm;
    case 61:
        return DdsFormat::r8_unorm;
    case 65:
        return DdsFormat::a8_unorm;
    case 71:
        return DdsFormat::bc1_unorm;
    case 72:
        return DdsFormat::bc1_srgb;
    case 74:
        return DdsFormat::bc2_unorm;
    case 75:
        return DdsFormat::bc2_srgb;
    case 77:
        return DdsFormat::bc3_unorm;
    case 78:
        return DdsFormat::bc3_srgb;
    case 80:
        return DdsFormat::bc4_unorm;
    case 81:
        return DdsFormat::bc4_snorm;
    case 83:
        return DdsFormat::bc5_unorm;
    case 84:
        return DdsFormat::bc5_snorm;
    case 87:
        return DdsFormat::bgra8_unorm;
    case 88:
        return DdsFormat::bgrx8_unorm;
    case 91:
        return DdsFormat::bgra8_srgb;
    case 93:
        return DdsFormat::bgrx8_srgb;
    default:
        return DdsFormat::unknown;
    }
}

std::uint32_t bits_per_pixel(const DdsFormat format) noexcept
{
    switch (format)
    {
    case DdsFormat::rgba8_unorm:
    case DdsFormat::rgba8_srgb:
    case DdsFormat::bgra8_unorm:
    case DdsFormat::bgra8_srgb:
    case DdsFormat::rgbx8_unorm:
    case DdsFormat::bgrx8_unorm:
    case DdsFormat::bgrx8_srgb:
        return 32;
    case DdsFormat::rgb8_unorm:
    case DdsFormat::bgr8_unorm:
        return 24;
    case DdsFormat::rg8_unorm:
        return 16;
    case DdsFormat::r8_unorm:
    case DdsFormat::a8_unorm:
        return 8;
    default:
        return 0;
    }
}

std::uint32_t block_bytes(const DdsFormat format) noexcept
{
    switch (format)
    {
    case DdsFormat::bc1_unorm:
    case DdsFormat::bc1_srgb:
    case DdsFormat::bc4_unorm:
    case DdsFormat::bc4_snorm:
        return 8;
    case DdsFormat::bc2_unorm:
    case DdsFormat::bc2_srgb:
    case DdsFormat::bc3_unorm:
    case DdsFormat::bc3_srgb:
    case DdsFormat::bc5_unorm:
    case DdsFormat::bc5_snorm:
        return 16;
    default:
        return 0;
    }
}

bool alpha_format(const DdsFormat format) noexcept
{
    switch (format)
    {
    case DdsFormat::bc2_unorm:
    case DdsFormat::bc2_srgb:
    case DdsFormat::bc3_unorm:
    case DdsFormat::bc3_srgb:
    case DdsFormat::rgba8_unorm:
    case DdsFormat::rgba8_srgb:
    case DdsFormat::bgra8_unorm:
    case DdsFormat::bgra8_srgb:
    case DdsFormat::a8_unorm:
        return true;
    default:
        return false;
    }
}

} // namespace

DdsTexture DdsTexture::parse(const std::span<const std::byte> bytes)
{
    if (bytes.size() < 128 || read_u32(bytes, 0) != fourcc('D', 'D', 'S', ' ') || read_u32(bytes, 4) != 124 || read_u32(bytes, 76) != 32)
    {
        throw std::runtime_error("invalid DDS header");
    }

    DdsTexture result;
    result.height = read_u32(bytes, 12);
    result.width = read_u32(bytes, 16);
    result.depth = std::max(1U, read_u32(bytes, 24));
    result.mip_levels = std::max(1U, read_u32(bytes, 28));
    const auto pixel_flags = read_u32(bytes, 80);
    const auto legacy_fourcc = read_u32(bytes, 84);
    const auto rgb_bits = read_u32(bytes, 88);
    const auto red_mask = read_u32(bytes, 92);
    const auto green_mask = read_u32(bytes, 96);
    const auto blue_mask = read_u32(bytes, 100);
    const auto alpha_mask = read_u32(bytes, 104);
    const auto caps2 = read_u32(bytes, 112);
    result.data_offset = 128;

    if (result.width == 0 || result.height == 0 || result.mip_levels > 32)
    {
        throw std::runtime_error("invalid DDS dimensions or mip count");
    }

    if ((pixel_flags & 0x4U) != 0)
    {
        switch (legacy_fourcc)
        {
        case fourcc('D', 'X', 'T', '1'):
            result.format = DdsFormat::bc1_unorm;
            break;
        case fourcc('D', 'X', 'T', '3'):
            result.format = DdsFormat::bc2_unorm;
            break;
        case fourcc('D', 'X', 'T', '5'):
            result.format = DdsFormat::bc3_unorm;
            break;
        case fourcc('A', 'T', 'I', '1'):
        case fourcc('B', 'C', '4', 'U'):
            result.format = DdsFormat::bc4_unorm;
            break;
        case fourcc('B', 'C', '4', 'S'):
            result.format = DdsFormat::bc4_snorm;
            break;
        case fourcc('A', 'T', 'I', '2'):
        case fourcc('B', 'C', '5', 'U'):
            result.format = DdsFormat::bc5_unorm;
            break;
        case fourcc('B', 'C', '5', 'S'):
            result.format = DdsFormat::bc5_snorm;
            break;
        case fourcc('D', 'X', '1', '0'):
        {
            if (bytes.size() < 148)
            {
                throw std::runtime_error("truncated DDS DX10 header");
            }
            result.dxgi_format = read_u32(bytes, 128);
            result.format = dxgi_to_format(result.dxgi_format);
            const auto dimension = read_u32(bytes, 132);
            const auto misc_flags = read_u32(bytes, 136);
            result.array_size = read_u32(bytes, 140);
            result.data_offset = 148;
            result.cube_map = (misc_flags & 0x4U) != 0;
            result.faces = result.cube_map ? 6U : 1U;
            result.volume = dimension == 4;
            if (dimension < 2 || dimension > 4 || result.array_size == 0 || (result.volume && result.array_size != 1))
            {
                throw std::runtime_error("invalid DDS DX10 resource description");
            }
            break;
        }
        default:
            break;
        }
    }
    else if ((pixel_flags & 0x40U) != 0)
    {
        if (rgb_bits == 32 && red_mask == 0x000000ffU && green_mask == 0x0000ff00U && blue_mask == 0x00ff0000U)
        {
            result.format = alpha_mask == 0xff000000U ? DdsFormat::rgba8_unorm : DdsFormat::rgbx8_unorm;
        }
        else if (rgb_bits == 32 && red_mask == 0x00ff0000U && green_mask == 0x0000ff00U && blue_mask == 0x000000ffU)
        {
            result.format = alpha_mask == 0xff000000U ? DdsFormat::bgra8_unorm : DdsFormat::bgrx8_unorm;
        }
        else if (rgb_bits == 24 && red_mask == 0x000000ffU && green_mask == 0x0000ff00U && blue_mask == 0x00ff0000U)
        {
            result.format = DdsFormat::rgb8_unorm;
        }
        else if (rgb_bits == 24 && red_mask == 0x00ff0000U && green_mask == 0x0000ff00U && blue_mask == 0x000000ffU)
        {
            result.format = DdsFormat::bgr8_unorm;
        }
    }
    else if ((pixel_flags & 0x20000U) != 0)
    {
        if (rgb_bits == 8)
        {
            result.format = DdsFormat::r8_unorm;
        }
        else if (rgb_bits == 16)
        {
            result.format = DdsFormat::rg8_unorm;
        }
    }
    else if ((pixel_flags & 0x2U) != 0 && rgb_bits == 8)
    {
        result.format = DdsFormat::a8_unorm;
    }

    if (legacy_fourcc != fourcc('D', 'X', '1', '0'))
    {
        result.cube_map = (caps2 & 0x200U) != 0;
        result.faces = result.cube_map ? 6U : 1U;
        result.volume = (caps2 & 0x200000U) != 0;
    }
    if (result.format == DdsFormat::unknown)
    {
        throw std::runtime_error("unsupported DDS pixel format");
    }

    result.has_alpha = (pixel_flags & 0x1U) != 0 || alpha_mask != 0 || alpha_format(result.format);
    std::size_t cursor = result.data_offset;
    for (std::uint32_t layer = 0; layer < result.array_size; ++layer)
    {
        for (std::uint32_t face = 0; face < result.faces; ++face)
        {
            auto width = result.width;
            auto height = result.height;
            auto depth = result.volume ? result.depth : 1U;
            for (std::uint32_t mip = 0; mip < result.mip_levels; ++mip)
            {
                std::size_t row_pitch = 0;
                std::size_t slice_pitch = 0;
                if (const auto bytes_per_block = block_bytes(result.format); bytes_per_block != 0)
                {
                    row_pitch = checked_multiply(std::max(1U, (width + 3U) / 4U), bytes_per_block);
                    slice_pitch = checked_multiply(row_pitch, std::max(1U, (height + 3U) / 4U));
                }
                else
                {
                    row_pitch = (checked_multiply(width, bits_per_pixel(result.format)) + 7U) / 8U;
                    slice_pitch = checked_multiply(row_pitch, height);
                }
                const auto size = checked_multiply(slice_pitch, depth);
                if (cursor > bytes.size() || size > bytes.size() - cursor)
                {
                    throw std::runtime_error("truncated DDS image data");
                }
                result.subresources.push_back({layer, face, mip, width, height, depth, cursor, row_pitch, slice_pitch, size});
                cursor += size;
                width = std::max(1U, width / 2U);
                height = std::max(1U, height / 2U);
                depth = std::max(1U, depth / 2U);
            }
        }
    }
    result.data_size = cursor - result.data_offset;
    return result;
}

std::string_view to_string(const DdsFormat format) noexcept
{
    switch (format)
    {
    case DdsFormat::unknown:
        return "unknown";
    case DdsFormat::bc1_unorm:
        return "BC1_UNORM";
    case DdsFormat::bc1_srgb:
        return "BC1_SRGB";
    case DdsFormat::bc2_unorm:
        return "BC2_UNORM";
    case DdsFormat::bc2_srgb:
        return "BC2_SRGB";
    case DdsFormat::bc3_unorm:
        return "BC3_UNORM";
    case DdsFormat::bc3_srgb:
        return "BC3_SRGB";
    case DdsFormat::bc4_unorm:
        return "BC4_UNORM";
    case DdsFormat::bc4_snorm:
        return "BC4_SNORM";
    case DdsFormat::bc5_unorm:
        return "BC5_UNORM";
    case DdsFormat::bc5_snorm:
        return "BC5_SNORM";
    case DdsFormat::rgba8_unorm:
        return "RGBA8_UNORM";
    case DdsFormat::rgba8_srgb:
        return "RGBA8_SRGB";
    case DdsFormat::bgra8_unorm:
        return "BGRA8_UNORM";
    case DdsFormat::bgra8_srgb:
        return "BGRA8_SRGB";
    case DdsFormat::rgbx8_unorm:
        return "RGBX8_UNORM";
    case DdsFormat::bgrx8_unorm:
        return "BGRX8_UNORM";
    case DdsFormat::bgrx8_srgb:
        return "BGRX8_SRGB";
    case DdsFormat::rgb8_unorm:
        return "RGB8_UNORM";
    case DdsFormat::bgr8_unorm:
        return "BGR8_UNORM";
    case DdsFormat::r8_unorm:
        return "R8_UNORM";
    case DdsFormat::a8_unorm:
        return "A8_UNORM";
    case DdsFormat::rg8_unorm:
        return "RG8_UNORM";
    }
    return "unknown";
}

bool is_block_compressed(const DdsFormat format) noexcept
{
    return block_bytes(format) != 0;
}

} // namespace eaw
