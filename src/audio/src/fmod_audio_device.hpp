#pragma once

// The optional FMOD Core backend (private to eaw_audio_devices; compiled only with
// EAW_AUDIO_FMOD=ON and a local FMOD SDK at FMOD_ROOT). Each playing sound is an FMOD sound made
// from the engine's PCM in place (the Sound stays alive until the sound is released) on a channel
// in the category's channel group.

#include "core/status.hpp"
#include "audio/audio_device.hpp"

#include <memory>

namespace eaw {

class FmodAudioDevice final : public AudioDevice
{
public:
    [[nodiscard]] static Result<std::unique_ptr<FmodAudioDevice>> create();
    ~FmodAudioDevice() override;
    FmodAudioDevice(const FmodAudioDevice&) = delete;
    FmodAudioDevice& operator=(const FmodAudioDevice&) = delete;

    [[nodiscard]] std::string_view name() const noexcept override;
    VoiceId play(const SoundPlayback& playback) override;
    void update_voice(VoiceId voice, float volume, float pan) override;
    void stop(VoiceId voice) override;
    [[nodiscard]] bool playing(VoiceId voice) const override;
    void set_category_volume(AudioCategory category, float volume) override;
    void set_master_volume(float volume) override;
    void update() override;
    [[nodiscard]] std::size_t active_voices() const noexcept override;

    struct Impl;

private:
    explicit FmodAudioDevice(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace eaw
