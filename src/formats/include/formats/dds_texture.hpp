#pragma once

#include "core/status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace eaw {

enum class DdsFormat
{
    unknown,
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
    rgbx8_unorm,
    bgrx8_unorm,
    bgrx8_srgb,
    rgb8_unorm,
    bgr8_unorm,
    r8_unorm,
    a8_unorm,
    rg8_unorm,
};

struct DdsSubresource
{
    std::uint32_t array_layer{};
    std::uint32_t face{};
    std::uint32_t mip_level{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t depth{};
    std::size_t offset{};
    std::size_t row_pitch{};
    std::size_t slice_pitch{};
    std::size_t size{};
};

struct DdsTextureData
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t depth{1};
    std::uint32_t mip_levels{1};
    std::uint32_t array_size{1};
    std::uint32_t faces{1};
    DdsFormat format{DdsFormat::unknown};
    std::uint32_t dxgi_format{};
    std::size_t data_offset{};
    std::size_t data_size{};
    bool cube_map{};
    bool volume{};
    bool has_alpha{};
    std::vector<DdsSubresource> subresources;
};

[[nodiscard]] std::string_view to_string(DdsFormat format) noexcept;
[[nodiscard]] bool is_block_compressed(DdsFormat format) noexcept;

[[nodiscard]] Result<DdsTextureData> read_dds(std::span<const std::byte> bytes);

} // namespace eaw
