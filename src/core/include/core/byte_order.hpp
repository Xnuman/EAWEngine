#pragma once

// Little-endian integers in byte buffers, as every format the original uses stores them. Callers
// check the enclosing range first, so reading past the end is a programming error (asserted).

#include <cstddef>
#include <cstdint>
#include <span>

namespace eaw {

[[nodiscard]] std::uint16_t little_endian_u16(std::span<const std::byte> bytes, std::size_t offset);
[[nodiscard]] std::uint32_t little_endian_u32(std::span<const std::byte> bytes, std::size_t offset);

} // namespace eaw
