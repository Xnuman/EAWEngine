#include "formats/wav_audio.hpp"

#include "core/byte_order.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace eaw {
namespace {

constexpr std::size_t riff_header_size = 12;
constexpr std::size_t chunk_header_size = 8;
constexpr std::size_t format_header_size = 16;

bool tag_is(const std::span<const std::byte> bytes, const std::size_t offset, const std::string_view tag)
{
    for (std::size_t index = 0; index < tag.size(); ++index)
    {
        if (std::to_integer<char>(bytes[offset + index]) != tag[index])
        {
            return false;
        }
    }
    return true;
}

} // namespace

Result<WavData> read_wav(const std::span<const std::byte> bytes)
{
    if (bytes.size() < riff_header_size || !tag_is(bytes, 0, "RIFF") || !tag_is(bytes, 8, "WAVE"))
    {
        return make_error("not a RIFF WAVE file");
    }
    WavData wav;
    bool has_format = false;
    bool has_samples = false;
    std::size_t offset = riff_header_size;
    while (offset + chunk_header_size <= bytes.size())
    {
        const auto size = static_cast<std::size_t>(little_endian_u32(bytes, offset + 4));
        const auto payload = offset + chunk_header_size;
        if (size > bytes.size() - payload)
        {
            return make_error("WAV chunk '" + std::string(reinterpret_cast<const char*>(bytes.data() + offset), 4) + "' at offset " + std::to_string(offset) + " runs past the end of the file");
        }
        const auto content = bytes.subspan(payload, size);
        if (tag_is(bytes, offset, "fmt "))
        {
            if (size < format_header_size)
            {
                return make_error("WAV fmt chunk is " + std::to_string(size) + " bytes, shorter than 16");
            }
            wav.format = little_endian_u16(content, 0);
            wav.channels = little_endian_u16(content, 2);
            wav.sample_rate = little_endian_u32(content, 4);
            wav.byte_rate = little_endian_u32(content, 8);
            wav.block_align = little_endian_u16(content, 12);
            wav.bits_per_sample = little_endian_u16(content, 14);
            wav.format_extension.assign(content.begin() + format_header_size, content.end());
            has_format = true;
        }
        else if (tag_is(bytes, offset, "data"))
        {
            wav.samples.assign(content.begin(), content.end());
            has_samples = true;
        }
        else
        {
            WavChunkData chunk;
            std::copy_n(reinterpret_cast<const char*>(bytes.data() + offset), 4, chunk.id.begin());
            chunk.bytes.assign(content.begin(), content.end());
            wav.other_chunks.push_back(std::move(chunk));
        }
        // Chunks are padded to an even size; a missing pad byte at the very end is tolerated.
        offset = payload + size + (size & 1U);
    }
    if (!has_format || !has_samples)
    {
        return make_error(has_format ? "WAV file has no data chunk" : "WAV file has no fmt chunk");
    }
    if (wav.channels == 0 || wav.sample_rate == 0 || wav.block_align == 0)
    {
        return make_error("WAV format has " + std::to_string(wav.channels) + " channels, a sample rate of " + std::to_string(wav.sample_rate) + ", and a block size of " +
                          std::to_string(wav.block_align));
    }
    return wav;
}

} // namespace eaw
