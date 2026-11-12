#pragma once

// Petroglyph's chunk container, shared by ALO models and particle systems, ALA animations, and TED
// maps. A chunk is a little-endian u32 type and a u32 size whose high bit marks a container of
// chunks, followed by its payload; a mini-chunk is a u8 type and a u8 size. The readers validate every
// header against its parent before returning ranges, so the decoders above them only index inside
// ranges that exist.

#include "math/vector.hpp"
#include "core/status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

struct ChunkRange
{
    std::uint32_t type{};
    // The size's high bit: the payload is itself a list of chunks.
    bool container{};
    std::size_t payload{};
    std::size_t end{};

    [[nodiscard]] std::size_t size() const noexcept
    {
        return end - payload;
    }
};

struct MiniChunkRange
{
    std::uint8_t type{};
    std::size_t payload{};
    std::size_t end{};

    [[nodiscard]] std::size_t size() const noexcept
    {
        return end - payload;
    }
};

// The chunks of a range up to the first that cannot be read, and why reading stopped there (empty
// when every chunk was read): what tools list for a damaged file.
struct ChunkListing
{
    std::vector<ChunkRange> chunks;
    std::string problem;
};

namespace chunk {

// Lists the chunks in bytes[begin, end) as far as they can be read (read_chunks fails instead).
[[nodiscard]] ChunkListing list_chunks(std::span<const std::byte> bytes, std::size_t begin, std::size_t end, std::string_view format, std::string_view where = {});
// The chunks in bytes[begin, end). `format` names the file kind in diagnostics ("ALO", "TED") and
// `where` the part being read (empty for none); errors give the offset and chunk type.
[[nodiscard]] Result<std::vector<ChunkRange>> read_chunks(std::span<const std::byte> bytes, std::size_t begin, std::size_t end, std::string_view format, std::string_view where = {});
// The mini-chunks filling `chunk`'s payload.
[[nodiscard]] Result<std::vector<MiniChunkRange>> read_minis(std::span<const std::byte> bytes, const ChunkRange& chunk, std::string_view format, std::string_view where = {});

[[nodiscard]] const ChunkRange* find_chunk(std::span<const ChunkRange> chunks, std::uint32_t type) noexcept;
[[nodiscard]] const MiniChunkRange* find_mini(std::span<const MiniChunkRange> chunks, std::uint8_t type) noexcept;

// Little-endian values. Callers check the enclosing range first, so reading past the end is a
// programming error (asserted).
[[nodiscard]] std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t offset);
[[nodiscard]] std::int32_t read_i32(std::span<const std::byte> bytes, std::size_t offset);
[[nodiscard]] std::uint16_t read_u16(std::span<const std::byte> bytes, std::size_t offset);
[[nodiscard]] std::int16_t read_i16(std::span<const std::byte> bytes, std::size_t offset);
[[nodiscard]] float read_float(std::span<const std::byte> bytes, std::size_t offset);
// Three floats (x, y, z).
[[nodiscard]] Vector3 read_vector3(std::span<const std::byte> bytes, std::size_t offset);
// The narrow string in bytes[begin, end), ending at the first NUL.
[[nodiscard]] std::string read_string(std::span<const std::byte> bytes, std::size_t begin, std::size_t end);

} // namespace chunk
} // namespace eaw
