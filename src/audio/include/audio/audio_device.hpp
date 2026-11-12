#pragma once

// The engine's audio output, implemented by a backend (XAudio2 by default, FMOD optionally, or the
// null device). Backends only play PCM the engine hands them: decoding and spatial placement are
// the engine's (audio/spatial_audio.hpp), so every backend plays the same samples at the same
// volumes and pans. Calls come from one thread (presentation).

#include "audio/sound.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace eaw {

// Mixer categories, each with its own volume (options and ducking will use them).
enum class AudioCategory : std::uint8_t
{
    effects,
    voice,
    music,
    interface,
    count,
};

inline constexpr std::size_t audio_category_count = static_cast<std::size_t>(AudioCategory::count);

// The most voices a backend plays at once (an engine choice; the original's Miles setup is
// unknown). A play beyond it is refused, and the caller decides what to drop.
inline constexpr std::size_t max_audio_voices = 64;

struct SoundPlayback
{
    // Kept alive by the device until the voice ends.
    std::shared_ptr<const Sound> sound;
    AudioCategory category{AudioCategory::effects};
    float volume{1.0F};
    // Playback rate: 1 plays as recorded, 2 an octave up.
    float pitch{1.0F};
    // -1 left, 0 centre, 1 right.
    float pan{};
    bool loop{};
};

// A playing voice; 0 is none.
using VoiceId = std::uint64_t;

class AudioDevice
{
public:
    virtual ~AudioDevice() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    // Starts a voice; 0 when the voice budget is spent or the backend failed (with a warning).
    virtual VoiceId play(const SoundPlayback& playback) = 0;
    // A playing voice's volume and pan (moving sources, a moving listener).
    virtual void update_voice(VoiceId voice, float volume, float pan) = 0;
    virtual void stop(VoiceId voice) = 0;
    [[nodiscard]] virtual bool playing(VoiceId voice) const = 0;
    // Volume factors (0 silent, 1 as recorded) of a category and of everything.
    virtual void set_category_volume(AudioCategory category, float volume) = 0;
    virtual void set_master_volume(float volume) = 0;
    // Releases voices that finished; once per frame.
    virtual void update() = 0;
    [[nodiscard]] virtual std::size_t active_voices() const noexcept = 0;
};

} // namespace eaw
