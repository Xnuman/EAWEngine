#include "formats/png_image.hpp"

#include "core/checksum.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace eaw {
namespace {

constexpr std::array<std::uint8_t, 8> signature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
// Larger images are refused rather than allocated (a capture is a window's back buffer).
constexpr std::uint32_t maximum_dimension = 16384;
// Stored deflate blocks hold at most this many bytes.
constexpr std::size_t maximum_stored_block = 65535;

std::uint32_t adler32(const std::span<const std::byte> bytes) noexcept
{
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const auto byte : bytes)
    {
        a = (a + std::to_integer<std::uint32_t>(byte)) % 65521U;
        b = (b + a) % 65521U;
    }
    return (b << 16U) | a;
}

void append_be32(std::vector<std::byte>& out, const std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

std::uint32_t read_be32(const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index)
    {
        value = (value << 8U) | std::to_integer<std::uint32_t>(bytes[offset + index]);
    }
    return value;
}

void append_chunk(std::vector<std::byte>& out, const char (&type)[5], const std::span<const std::byte> data)
{
    append_be32(out, static_cast<std::uint32_t>(data.size()));
    const auto start = out.size();
    for (int index = 0; index < 4; ++index)
    {
        out.push_back(static_cast<std::byte>(type[index]));
    }
    out.insert(out.end(), data.begin(), data.end());
    append_be32(out, crc32(std::span(out).subspan(start)));
}

} // namespace

Result<std::vector<std::byte>> write_png(const RgbaImage& image)
{
    if (!image.valid() || image.width > maximum_dimension || image.height > maximum_dimension)
    {
        return make_error("PNG output needs a complete image of at most " + std::to_string(maximum_dimension) + " pixels a side");
    }
    // Rows, each with filter byte 0.
    const auto row_bytes = static_cast<std::size_t>(image.width) * 4U;
    std::vector<std::byte> raw;
    raw.reserve((row_bytes + 1U) * image.height);
    for (std::uint32_t row = 0; row < image.height; ++row)
    {
        raw.push_back(std::byte{0});
        const auto* first = reinterpret_cast<const std::byte*>(image.pixels.data() + row * row_bytes);
        raw.insert(raw.end(), first, first + row_bytes);
    }
    // A zlib stream of stored deflate blocks.
    std::vector<std::byte> zlib{std::byte{0x78}, std::byte{0x01}};
    zlib.reserve(raw.size() + raw.size() / maximum_stored_block * 5U + 16U);
    for (std::size_t offset = 0; offset < raw.size(); offset += maximum_stored_block)
    {
        const auto length = static_cast<std::uint16_t>(std::min(maximum_stored_block, raw.size() - offset));
        const bool last = offset + length >= raw.size();
        zlib.push_back(std::byte{last ? std::uint8_t{1} : std::uint8_t{0}});
        zlib.push_back(static_cast<std::byte>(length & 0xFFU));
        zlib.push_back(static_cast<std::byte>(length >> 8U));
        zlib.push_back(static_cast<std::byte>(~length & 0xFFU));
        zlib.push_back(static_cast<std::byte>((~length >> 8U) & 0xFFU));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
    }
    append_be32(zlib, adler32(raw));

    std::vector<std::byte> png;
    for (const auto byte : signature)
    {
        png.push_back(static_cast<std::byte>(byte));
    }
    std::vector<std::byte> header;
    append_be32(header, image.width);
    append_be32(header, image.height);
    // 8 bits per channel, colour type 6 (RGBA), deflate, adaptive filtering, no interlace.
    for (const auto value : {std::byte{8}, std::byte{6}, std::byte{0}, std::byte{0}, std::byte{0}})
    {
        header.push_back(value);
    }
    append_chunk(png, "IHDR", header);
    append_chunk(png, "IDAT", zlib);
    append_chunk(png, "IEND", {});
    return png;
}

Result<RgbaImage> read_png(const std::span<const std::byte> bytes)
{
    if (bytes.size() < signature.size() || !std::equal(signature.begin(), signature.end(), bytes.begin(),
                                                       [](const std::uint8_t expected, const std::byte actual)
                                                       {
                                                           return std::to_integer<std::uint8_t>(actual) == expected;
                                                       }))
    {
        return make_error("PNG signature is missing");
    }
    RgbaImage image;
    std::vector<std::byte> zlib;
    bool ended = false;
    auto cursor = signature.size();
    while (!ended)
    {
        if (bytes.size() - cursor < 12)
        {
            return make_error("PNG ends in a truncated chunk at offset " + std::to_string(cursor));
        }
        const auto length = read_be32(bytes, cursor);
        if (length > bytes.size() - cursor - 12)
        {
            return make_error("PNG chunk at offset " + std::to_string(cursor) + " extends past the file");
        }
        const auto type = std::string(reinterpret_cast<const char*>(bytes.data() + cursor + 4), 4);
        const auto data = bytes.subspan(cursor + 8, length);
        if (crc32(bytes.subspan(cursor + 4, length + 4)) != read_be32(bytes, cursor + 8 + length))
        {
            return make_error("PNG chunk " + type + " has a wrong CRC");
        }
        if (type == "IHDR")
        {
            if (length != 13)
            {
                return make_error("PNG header has the wrong size");
            }
            image.width = read_be32(data, 0);
            image.height = read_be32(data, 4);
            const auto value = [&](const std::size_t index)
            {
                return std::to_integer<std::uint8_t>(data[index]);
            };
            if (image.width == 0 || image.height == 0 || image.width > maximum_dimension || image.height > maximum_dimension)
            {
                return make_error("PNG size " + std::to_string(image.width) + "x" + std::to_string(image.height) + " is not supported");
            }
            if (value(8) != 8 || value(9) != 6 || value(10) != 0 || value(11) != 0 || value(12) != 0)
            {
                return make_error("PNG is not 8-bit RGBA without interlacing (only EAWEngine's own captures are read)");
            }
        }
        else if (type == "IDAT")
        {
            zlib.insert(zlib.end(), data.begin(), data.end());
        }
        else if (type == "IEND")
        {
            ended = true;
        }
        cursor += 12U + length;
    }
    if (image.width == 0)
    {
        return make_error("PNG has no header");
    }
    if (zlib.size() < 6 || (std::to_integer<std::uint8_t>(zlib[0]) & 0x0FU) != 8)
    {
        return make_error("PNG image data is not a zlib deflate stream");
    }
    const auto row_bytes = static_cast<std::size_t>(image.width) * 4U;
    const auto expected = (row_bytes + 1U) * image.height;
    std::vector<std::byte> raw;
    raw.reserve(expected);
    std::size_t position = 2;
    bool last = false;
    while (!last)
    {
        if (zlib.size() - position < 5)
        {
            return make_error("PNG image data ends in a truncated block");
        }
        const auto header = std::to_integer<std::uint8_t>(zlib[position]);
        last = (header & 1U) != 0;
        if (((header >> 1U) & 3U) != 0)
        {
            return make_error("compressed PNG data is not supported (only EAWEngine's own uncompressed captures are read)");
        }
        const auto length = static_cast<std::size_t>(std::to_integer<std::uint32_t>(zlib[position + 1]) | (std::to_integer<std::uint32_t>(zlib[position + 2]) << 8U));
        const auto complement = static_cast<std::size_t>(std::to_integer<std::uint32_t>(zlib[position + 3]) | (std::to_integer<std::uint32_t>(zlib[position + 4]) << 8U));
        if ((length ^ 0xFFFFU) != complement || length > zlib.size() - position - 5 || length > expected - raw.size())
        {
            return make_error("PNG image data has a malformed stored block");
        }
        raw.insert(raw.end(), zlib.begin() + static_cast<std::ptrdiff_t>(position + 5), zlib.begin() + static_cast<std::ptrdiff_t>(position + 5 + length));
        position += 5 + length;
    }
    if (raw.size() != expected || zlib.size() - position < 4 || read_be32(zlib, position) != adler32(raw))
    {
        return make_error("PNG image data has the wrong size or checksum");
    }
    image.pixels.resize(row_bytes * image.height);
    for (std::uint32_t row = 0; row < image.height; ++row)
    {
        const auto* source = raw.data() + row * (row_bytes + 1U);
        if (std::to_integer<std::uint8_t>(source[0]) != 0)
        {
            return make_error("PNG row filters are not supported (only EAWEngine's own captures are read)");
        }
        std::memcpy(image.pixels.data() + row * row_bytes, source + 1, row_bytes);
    }
    return image;
}

} // namespace eaw
