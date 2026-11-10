#include "formats/mtd_atlas.hpp"

#include "formats/dat_string_table.hpp"

#include <algorithm>
#include <limits>

namespace eaw {
namespace {

// Callers check the table size before reading, so an out-of-range read is a bug.
std::uint32_t read_u32(const std::span<const std::byte> bytes, std::size_t& cursor)
{
    EAW_ASSERT(cursor + 4 <= bytes.size());
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[cursor++])) << shift;
    }
    return value;
}

} // namespace

Result<MtdDirectory> MtdDirectory::parse(const std::span<const std::byte> bytes)
{
    if (bytes.size() < 4)
    {
        return make_error("truncated MTD file");
    }
    std::size_t cursor = 0;
    const auto count = read_u32(bytes, cursor);
    constexpr std::size_t entry_size = 81;
    if (count > (std::numeric_limits<std::size_t>::max() - 4U) / entry_size || 4U + static_cast<std::size_t>(count) * entry_size != bytes.size())
    {
        return make_error("invalid MTD entry table size");
    }

    MtdDirectory result;
    result.entries.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto name_begin = cursor;
        const auto name_end = std::find(bytes.begin() + static_cast<std::ptrdiff_t>(cursor), bytes.begin() + static_cast<std::ptrdiff_t>(cursor + 64U), std::byte{});
        if (name_end == bytes.begin() + static_cast<std::ptrdiff_t>(cursor + 64U))
        {
            return make_error("MTD entry name is not null terminated");
        }
        std::string name;
        name.reserve(static_cast<std::size_t>(name_end - (bytes.begin() + static_cast<std::ptrdiff_t>(cursor))));
        for (auto position = bytes.begin() + static_cast<std::ptrdiff_t>(cursor); position != name_end; ++position)
        {
            const auto character = std::to_integer<unsigned char>(*position);
            if (character >= 0x80U)
            {
                return make_error("MTD entry name is not ASCII");
            }
            name.push_back(static_cast<char>(character));
        }
        if (name.empty())
        {
            return make_error("MTD entry name is empty");
        }
        cursor = name_begin + 64U;

        MtdEntry entry;
        entry.name = std::move(name);
        entry.name_crc = dat_key_crc(entry.name);
        entry.x = read_u32(bytes, cursor);
        entry.y = read_u32(bytes, cursor);
        entry.width = read_u32(bytes, cursor);
        entry.height = read_u32(bytes, cursor);
        entry.has_alpha = std::to_integer<unsigned char>(bytes[cursor++]) != 0;
        if (entry.x > std::numeric_limits<std::uint32_t>::max() - entry.width || entry.y > std::numeric_limits<std::uint32_t>::max() - entry.height)
        {
            return make_error("MTD entry rectangle overflows");
        }
        result.atlas_width = std::max(result.atlas_width, entry.x + entry.width);
        result.atlas_height = std::max(result.atlas_height, entry.y + entry.height);
        result.alpha_entries += entry.has_alpha ? 1U : 0U;
        result.entries.push_back(std::move(entry));
    }
    return result;
}

} // namespace eaw
