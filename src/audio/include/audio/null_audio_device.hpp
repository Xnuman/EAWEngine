#pragma once

// A device that plays nothing: for --no-audio, machines without audio output, and tests, which
// read what would have played. A voice that does not loop ends at the next update().

#include "audio/audio_device.hpp"

#include <vector>

namespace eaw {

class NullAudioDevice final : public AudioDevice
{
public:
    static constexpr std::size_t max_recorded_plays = 4096;

    struct Played
    {
        VoiceId voice{};
        SoundPlayback playback;
    };

    [[nodiscard]] std::string_view name() const noexcept override;
    VoiceId play(const SoundPlayback& playback) override;
    void update_voice(VoiceId voice, float volume, float pan) override;
    void stop(VoiceId voice) override;
    [[nodiscard]] bool playing(VoiceId voice) const override;
    void set_category_volume(AudioCategory category, float volume) override;
    void set_master_volume(float volume) override;
    void update() override;
    [[nodiscard]] std::size_t active_voices() const noexcept override;

    // The first max_recorded_plays plays, in order, with the latest volume and pan of their voices
    // (a long session without audio would otherwise keep every shot it played).
    [[nodiscard]] const std::vector<Played>& played() const noexcept;
    [[nodiscard]] float category_volume(AudioCategory category) const noexcept;
    [[nodiscard]] float master_volume() const noexcept;

private:
    std::vector<Played> played_;
    struct Active
    {
        VoiceId voice{};
        bool loop{};
    };
    std::vector<Active> active_;
    std::array<float, audio_category_count> category_volumes_{1.0F, 1.0F, 1.0F, 1.0F};
    float master_volume_{1.0F};
    VoiceId next_voice_{1};
};

} // namespace eaw
