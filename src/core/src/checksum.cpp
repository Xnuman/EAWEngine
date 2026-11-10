#include "core/checksum.hpp"

#include <array>

namespace eaw {
namespace {

constexpr std::uint32_t polynomial = 0xedb88320U;

constexpr std::array<std::uint32_t, 256> make_table() noexcept
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0; index < table.size(); ++index)
    {
        auto value = index;
        for (unsigned bit = 0; bit < 8; ++bit)
        {
            value = (value & 1U) != 0 ? polynomial ^ (value >> 1U) : value >> 1U;
        }
        table[index] = value;
    }
    return table;
}

constexpr auto table = make_table();

} // namespace

std::uint32_t crc32_update(std::uint32_t crc, const std::span<const std::byte> bytes) noexcept
{
    for (const auto byte : bytes)
    {
        crc = table[(crc ^ std::to_integer<std::uint32_t>(byte)) & 0xffU] ^ (crc >> 8U);
    }
    return crc;
}

std::uint32_t crc32(const std::span<const std::byte> bytes) noexcept
{
    return crc32_finish(crc32_update(crc32_initial, bytes));
}

std::uint32_t crc32(const std::string_view text) noexcept
{
    return crc32(std::as_bytes(std::span(text.data(), text.size())));
}

} // namespace eaw
