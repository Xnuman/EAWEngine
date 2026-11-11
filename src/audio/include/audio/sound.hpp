#pragma once

// An engine-native sound: interleaved 16-bit PCM frames, whatever file it came from (the asset
// layer's loaders decode WAV now and other formats later into this). Short sounds are decoded
// whole; long music will stream through a decoder instead (Audio stage 3).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eaw {

struct Sound
{
    std::uint16_t channels{};
    std::uint32_t sample_rate{};
    // Interleaved samples: frame after frame, one value per channel.
    std::vector<std::int16_t> samples;

    [[nodiscard]] std::size_t frames() const noexcept
    {
        return channels == 0 ? 0 : samples.size() / channels;
    }
    [[nodiscard]] double seconds() const noexcept
    {
        return sample_rate == 0 ? 0.0 : static_cast<double>(frames()) / static_cast<double>(sample_rate);
    }
};

} // namespace eaw
