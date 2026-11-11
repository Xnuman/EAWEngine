#pragma once

// RIFF WAVE audio, as the original's sound effects and unit responses are stored (16-bit PCM in all
// stock files). The reader keeps the file's facts: the format chunk's fields and any extra bytes,
// the sample data as stored, and every other chunk in order (LIST, fact, cue, ...). Interpreting
// the samples is the asset layer's job (assets/sound_loaders).

#include "core/status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace eaw {

// Format codes of the fmt chunk the engine knows by name; others are kept as numbers.
namespace wav_format {
inline constexpr std::uint16_t pcm = 1;
inline constexpr std::uint16_t ms_adpcm = 2;
inline constexpr std::uint16_t ieee_float = 3;
inline constexpr std::uint16_t extensible = 0xFFFE;
} // namespace wav_format

struct WavChunkData
{
    std::array<char, 4> id{};
    std::vector<std::byte> bytes;
};

struct WavData
{
    std::uint16_t format{};
    std::uint16_t channels{};
    std::uint32_t sample_rate{};
    std::uint32_t byte_rate{};
    std::uint16_t block_align{};
    std::uint16_t bits_per_sample{};
    // The fmt chunk beyond its 16-byte header (extensible formats keep their subformat here).
    std::vector<std::byte> format_extension;
    // The data chunk, as stored.
    std::vector<std::byte> samples;
    // Every chunk besides fmt and data, in file order.
    std::vector<WavChunkData> other_chunks;
};

// Reads a RIFF WAVE file. Errors: not RIFF/WAVE, a chunk that runs past the end, no fmt or data
// chunk, a fmt chunk shorter than 16 bytes, no channels, or a zero sample rate or block size. A
// RIFF size that disagrees with the file is tolerated (some tools write it wrong); the chunks
// decide.
[[nodiscard]] Result<WavData> read_wav(std::span<const std::byte> bytes);

} // namespace eaw
