#pragma once

// CRC-32 (IEEE 802.3: reflected polynomial 0xEDB88320, initial value and final XOR 0xFFFFFFFF), as
// DAT key hashes, game object name hashes, and PNG chunks use it.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace eaw {

[[nodiscard]] std::uint32_t crc32(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] std::uint32_t crc32(std::string_view text) noexcept;

// For data that arrives in pieces: start from crc32_initial, update with each piece, and finish.
inline constexpr std::uint32_t crc32_initial = 0xffffffffU;
[[nodiscard]] std::uint32_t crc32_update(std::uint32_t crc, std::span<const std::byte> bytes) noexcept;
[[nodiscard]] constexpr std::uint32_t crc32_finish(const std::uint32_t crc) noexcept
{
    return crc ^ 0xffffffffU;
}

} // namespace eaw
