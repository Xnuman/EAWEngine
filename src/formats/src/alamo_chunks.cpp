#include "formats/alamo_chunks.hpp"

#include "core/byte_order.hpp"
#include "core/fatal_error.hpp"

#include <algorithm>
#include <bit>
#include <sstream>

namespace eaw::chunk {
namespace {

std::string hex(const std::uint32_t value)
{
    std::ostringstream text;
    text << "0x" << std::hex << value;
    return text.str();
}

// " in <where>" or nothing.
std::string in(const std::string_view where)
{
    return where.empty() ? std::string{} : " in " + std::string(where);
}

} // namespace

Result<std::vector<ChunkRange>> read_chunks(const std::span<const std::byte> bytes, const std::size_t begin, const std::size_t end, const std::string_view format, const std::string_view where)
{
    if (begin > end || end > bytes.size())
    {
        return make_error(std::string(format) + " chunk range" + in(where) + " is invalid");
    }
    std::vector<ChunkRange> chunks;
    auto cursor = begin;
    while (cursor < end)
    {
        if (end - cursor < 8)
        {
            return make_error(std::string(format) + in(where) + " ends in a truncated chunk header at offset " + std::to_string(cursor));
        }
        const auto type = read_u32(bytes, cursor);
        const auto encoded_size = read_u32(bytes, cursor + 4);
        const auto size = static_cast<std::size_t>(encoded_size & 0x7fffffffU);
        const auto payload = cursor + 8;
        if (size > end - payload)
        {
            return make_error(std::string(format) + " chunk " + hex(type) + in(where) + " at offset " + std::to_string(cursor) + " extends past its parent");
        }
        chunks.push_back({type, (encoded_size & 0x80000000U) != 0, payload, payload + size});
        cursor = payload + size;
    }
    return chunks;
}

Result<std::vector<MiniChunkRange>> read_minis(const std::span<const std::byte> bytes, const ChunkRange& chunk, const std::string_view format, const std::string_view where)
{
    if (chunk.payload > chunk.end || chunk.end > bytes.size())
    {
        return make_error(std::string(format) + " mini-chunk range" + in(where) + " is invalid");
    }
    std::vector<MiniChunkRange> minis;
    auto cursor = chunk.payload;
    while (cursor < chunk.end)
    {
        if (chunk.end - cursor < 2)
        {
            return make_error(std::string(format) + in(where) + " ends in a truncated mini-chunk header at offset " + std::to_string(cursor));
        }
        const auto type = std::to_integer<std::uint8_t>(bytes[cursor]);
        const auto size = std::to_integer<std::uint8_t>(bytes[cursor + 1]);
        const auto payload = cursor + 2;
        if (size > chunk.end - payload)
        {
            return make_error(std::string(format) + " mini-chunk " + hex(type) + in(where) + " at offset " + std::to_string(cursor) + " extends past its parent");
        }
        minis.push_back({type, payload, payload + size});
        cursor = payload + size;
    }
    return minis;
}

const ChunkRange* find_chunk(const std::span<const ChunkRange> chunks, const std::uint32_t type) noexcept
{
    const auto found = std::ranges::find(chunks, type, &ChunkRange::type);
    return found == chunks.end() ? nullptr : &*found;
}

const MiniChunkRange* find_mini(const std::span<const MiniChunkRange> chunks, const std::uint8_t type) noexcept
{
    const auto found = std::ranges::find(chunks, type, &MiniChunkRange::type);
    return found == chunks.end() ? nullptr : &*found;
}

std::uint32_t read_u32(const std::span<const std::byte> bytes, const std::size_t offset)
{
    return little_endian_u32(bytes, offset);
}

std::int32_t read_i32(const std::span<const std::byte> bytes, const std::size_t offset)
{
    return std::bit_cast<std::int32_t>(read_u32(bytes, offset));
}

std::uint16_t read_u16(const std::span<const std::byte> bytes, const std::size_t offset)
{
    return little_endian_u16(bytes, offset);
}

std::int16_t read_i16(const std::span<const std::byte> bytes, const std::size_t offset)
{
    return std::bit_cast<std::int16_t>(read_u16(bytes, offset));
}

float read_float(const std::span<const std::byte> bytes, const std::size_t offset)
{
    return std::bit_cast<float>(read_u32(bytes, offset));
}

Vector3 read_vector3(const std::span<const std::byte> bytes, const std::size_t offset)
{
    return {read_float(bytes, offset), read_float(bytes, offset + 4), read_float(bytes, offset + 8)};
}

std::string read_string(const std::span<const std::byte> bytes, const std::size_t begin, const std::size_t end)
{
    EAW_ASSERT_MSG(begin <= end && end <= bytes.size(), "chunk file string range is invalid");
    auto finish = begin;
    while (finish < end && bytes[finish] != std::byte{})
    {
        ++finish;
    }
    return {reinterpret_cast<const char*>(bytes.data() + begin), finish - begin};
}

} // namespace eaw::chunk
