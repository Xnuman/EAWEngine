#pragma once

// Sound files to engine sounds. A file's format is recognized from its first bytes, not its name
// (the stock XML names speech .wav that is stored as .mp3), and each format has a reader in
// eaw_formats and a conversion here, so adding one (MP3, Ogg) means one reader and one case below.
// Every format ends as the same engine-native Sound (16-bit PCM).

#include "core/status.hpp"
#include "formats/wav_audio.hpp"
#include "audio/sound.hpp"

#include <cstddef>
#include <span>

namespace eaw {

enum class SoundFileFormat
{
    wav,
    mp3,
    ogg,
    unknown,
};

[[nodiscard]] SoundFileFormat sound_file_format(std::span<const std::byte> bytes) noexcept;

// PCM of 8, 16, 24, or 32 bits and 32-bit float (plain or extensible) become 16-bit PCM; other WAV
// codings (ADPCM, ...) are an error naming the format code.
[[nodiscard]] Result<Sound> load_sound(const WavData& wav);

// Reads and converts a sound file of any supported format; an unsupported one is an error naming it.
[[nodiscard]] Result<Sound> decode_sound(std::span<const std::byte> bytes);

} // namespace eaw
