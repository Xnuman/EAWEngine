#include "core/byte_order.hpp"

#include "core/status.hpp"

namespace eaw {
namespace {

template <class Value> Value little_endian(const std::span<const std::byte> bytes, const std::size_t offset)
{
    EAW_ASSERT_MSG(offset <= bytes.size() && bytes.size() - offset >= sizeof(Value), "a buffer ended while reading a little-endian value");
    Value value = 0;
    for (std::size_t index = 0; index < sizeof(Value); ++index)
    {
        value = static_cast<Value>(value | static_cast<Value>(static_cast<Value>(std::to_integer<std::uint8_t>(bytes[offset + index])) << (index * 8U)));
    }
    return value;
}

} // namespace

std::uint16_t little_endian_u16(const std::span<const std::byte> bytes, const std::size_t offset)
{
    return little_endian<std::uint16_t>(bytes, offset);
}

std::uint32_t little_endian_u32(const std::span<const std::byte> bytes, const std::size_t offset)
{
    return little_endian<std::uint32_t>(bytes, offset);
}

} // namespace eaw
