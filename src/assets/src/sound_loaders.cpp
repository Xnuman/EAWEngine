#include "assets/sound_loaders.hpp"

#include "core/byte_order.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <string>

namespace eaw {
namespace {

// The subformat GUID of WAVE_FORMAT_EXTENSIBLE starts with the plain format code.
constexpr std::size_t extensible_subformat_offset = 8;

std::int16_t to_pcm16(const float value)
{
    return static_cast<std::int16_t>(std::lround(std::clamp(value, -1.0F, 1.0F) * 32767.0F));
}

} // namespace

SoundFileFormat sound_file_format(const std::span<const std::byte> bytes) noexcept
{
    const auto starts = [&](const std::string_view tag)
    {
        return bytes.size() >= tag.size() && std::equal(tag.begin(), tag.end(), bytes.begin(),
                                                        [](const char expected, const std::byte actual)
                                                        {
                                                            return std::to_integer<char>(actual) == expected;
                                                        });
    };
    if (bytes.size() >= 12 && starts("RIFF") && std::to_integer<char>(bytes[8]) == 'W' && std::to_integer<char>(bytes[9]) == 'A')
    {
        return SoundFileFormat::wav;
    }
    if (starts("OggS"))
    {
        return SoundFileFormat::ogg;
    }
    // An ID3 tag, or an MPEG audio frame sync (eleven set bits).
    if (starts("ID3") || (bytes.size() >= 2 && std::to_integer<std::uint8_t>(bytes[0]) == 0xFF && (std::to_integer<std::uint8_t>(bytes[1]) & 0xE0U) == 0xE0U))
    {
        return SoundFileFormat::mp3;
    }
    return SoundFileFormat::unknown;
}

Result<Sound> load_sound(const WavData& wav)
{
    auto format = wav.format;
    if (format == wav_format::extensible)
    {
        if (wav.format_extension.size() < extensible_subformat_offset + 2)
        {
            return make_error("extensible WAV format without a subformat");
        }
        format = little_endian_u16(wav.format_extension, extensible_subformat_offset);
    }
    const auto bits = wav.bits_per_sample;
    const bool pcm = format == wav_format::pcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool floating = format == wav_format::ieee_float && bits == 32;
    if (!pcm && !floating)
    {
        return make_error("WAV coding " + std::to_string(format) + " with " + std::to_string(bits) + "-bit samples is not supported (PCM of 8 to 32 bits and 32-bit float are)");
    }
    const std::size_t bytes_per_sample = bits / 8U;
    if (wav.block_align != wav.channels * bytes_per_sample)
    {
        return make_error("WAV block size " + std::to_string(wav.block_align) + " does not match " + std::to_string(wav.channels) + " channels of " + std::to_string(bits) + "-bit samples");
    }
    Sound sound;
    sound.channels = wav.channels;
    sound.sample_rate = wav.sample_rate;
    const auto count = wav.samples.size() / bytes_per_sample / wav.channels * wav.channels;
    sound.samples.resize(count);
    const std::span<const std::byte> data(wav.samples);
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto at = index * bytes_per_sample;
        std::int16_t value = 0;
        if (floating)
        {
            value = to_pcm16(std::bit_cast<float>(little_endian_u32(data, at)));
        }
        else if (bits == 8)
        {
            // 8-bit WAV is unsigned, centred on 128.
            value = static_cast<std::int16_t>((std::to_integer<int>(data[at]) - 128) * 256);
        }
        else
        {
            // The two most significant bytes of a little-endian sample of 16 bits or more.
            value = static_cast<std::int16_t>(little_endian_u16(data, at + bytes_per_sample - 2U));
        }
        sound.samples[index] = value;
    }
    return sound;
}

Result<Sound> decode_sound(const std::span<const std::byte> bytes)
{
    switch (sound_file_format(bytes))
    {
    case SoundFileFormat::wav:
    {
        EAW_TRY_ASSIGN(const auto wav, read_wav(bytes));
        return load_sound(wav);
    }
    case SoundFileFormat::mp3:
        return make_error("MP3 sound files are not supported yet (music and speech come with Audio stage 3)");
    case SoundFileFormat::ogg:
        return make_error("Ogg sound files are not supported yet");
    case SoundFileFormat::unknown:
        break;
    }
    return make_error("not a sound file the engine knows (WAV, MP3, or Ogg)");
}

} // namespace eaw
