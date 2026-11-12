#include "formats/dat_string_table.hpp"

#include "core/byte_order.hpp"
#include "core/checksum.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <unordered_map>

namespace eaw {
namespace {

struct IndexRecord
{
    std::uint32_t crc{};
    std::uint32_t value_characters{};
    std::uint32_t key_characters{};
};

std::optional<std::uint32_t> read_u32(const std::span<const std::byte> bytes, std::size_t& cursor)
{
    if (cursor + 4 > bytes.size())
    {
        return std::nullopt;
    }
    const auto value = little_endian_u32(bytes, cursor);
    cursor += 4;
    return value;
}

void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

} // namespace

std::uint32_t dat_key_crc(const std::string_view key) noexcept
{
    return crc32(key);
}

Result<DatStringTableData> read_dat(const std::span<const std::byte> bytes)
{
    std::size_t cursor = 0;
    const auto stored_count = read_u32(bytes, cursor);
    if (!stored_count)
    {
        return make_error("truncated DAT file");
    }
    const auto count = *stored_count;
    constexpr std::uint32_t maximum_reasonable_entries = 5'000'000;
    if (count > maximum_reasonable_entries || static_cast<std::uint64_t>(count) * 12U + 4U > bytes.size())
    {
        return make_error("invalid DAT entry count");
    }

    std::vector<IndexRecord> index;
    index.reserve(count);
    std::uint64_t total_value_characters = 0;
    std::uint64_t total_key_characters = 0;
    for (std::uint32_t entry = 0; entry < count; ++entry)
    {
        const auto crc = read_u32(bytes, cursor);
        const auto value_characters = read_u32(bytes, cursor);
        const auto key_characters = read_u32(bytes, cursor);
        if (!crc || !value_characters || !key_characters)
        {
            return make_error("truncated DAT file");
        }
        IndexRecord record{*crc, *value_characters, *key_characters};
        total_value_characters += record.value_characters;
        total_key_characters += record.key_characters;
        index.push_back(record);
    }

    const std::uint64_t expected_size = cursor + total_value_characters * 2U + total_key_characters;
    if (expected_size != bytes.size())
    {
        return make_error(expected_size > bytes.size() ? "truncated DAT string data" : "unexpected trailing data in DAT file");
    }

    DatStringTableData result;
    result.entries.resize(count);
    for (std::size_t entry = 0; entry < count; ++entry)
    {
        result.entries[entry].crc = index[entry].crc;
        auto& value = result.entries[entry].value;
        value.reserve(index[entry].value_characters);
        for (std::uint32_t character = 0; character < index[entry].value_characters; ++character)
        {
            if (cursor + 2 > bytes.size())
            {
                return make_error("truncated UTF-16 DAT value");
            }
            const auto low = std::to_integer<unsigned char>(bytes[cursor++]);
            const auto high = std::to_integer<unsigned char>(bytes[cursor++]);
            value.push_back(static_cast<char16_t>(low | (high << 8U)));
        }
    }
    for (std::size_t entry = 0; entry < count; ++entry)
    {
        const auto length = index[entry].key_characters;
        auto& key = result.entries[entry].key;
        key.reserve(length);
        for (std::uint32_t character = 0; character < length; ++character)
        {
            key.push_back(static_cast<char>(std::to_integer<unsigned char>(bytes[cursor++])));
        }
    }

    result.crc_sorted = std::ranges::is_sorted(result.entries, {}, &DatEntry::crc);
    return result;
}

Result<std::vector<std::byte>> write_dat(const DatStringTableData& table)
{
    if (table.entries.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return make_error("too many DAT entries");
    }
    std::uint64_t byte_count = 4U + table.entries.size() * 12U;
    for (const auto& entry : table.entries)
    {
        byte_count += entry.value.size() * 2U + entry.key.size();
        if (entry.value.size() > std::numeric_limits<std::uint32_t>::max() || entry.key.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return make_error("DAT string is too long");
        }
    }
    if (byte_count > std::numeric_limits<std::size_t>::max())
    {
        return make_error("DAT output is too large");
    }

    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(byte_count));
    append_u32(bytes, static_cast<std::uint32_t>(table.entries.size()));
    for (const auto& entry : table.entries)
    {
        append_u32(bytes, dat_key_crc(entry.key));
        append_u32(bytes, static_cast<std::uint32_t>(entry.value.size()));
        append_u32(bytes, static_cast<std::uint32_t>(entry.key.size()));
    }
    for (const auto& entry : table.entries)
    {
        for (const char16_t character : entry.value)
        {
            bytes.push_back(static_cast<std::byte>(character & 0xffU));
            bytes.push_back(static_cast<std::byte>((character >> 8U) & 0xffU));
        }
    }
    for (const auto& entry : table.entries)
    {
        for (const unsigned char character : entry.key)
        {
            bytes.push_back(static_cast<std::byte>(character));
        }
    }
    return bytes;
}

} // namespace eaw
