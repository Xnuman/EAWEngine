#pragma once

#include "core/status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace eaw {

struct MtdAtlasEntry
{
    std::string name;
    std::uint32_t name_crc{};
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
    bool has_alpha{};
};

struct MtdAtlasData
{
    std::vector<MtdAtlasEntry> entries;
    std::uint32_t atlas_width{};
    std::uint32_t atlas_height{};
    std::size_t alpha_entries{};
};

[[nodiscard]] Result<MtdAtlasData> read_mtd(std::span<const std::byte> bytes);

} // namespace eaw
