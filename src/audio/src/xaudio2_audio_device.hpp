#pragma once

// The XAudio2 backend (private to eaw_audio_devices): a mastering voice, a submix voice per
// category, and one source voice per playing sound, its buffer pointing into the Sound kept alive
// until the voice is destroyed.

#include "core/status.hpp"
#include "audio/audio_device.hpp"

#include <memory>

namespace eaw {

class XAudio2AudioDevice final : public AudioDevice
{
public:
    [[nodiscard]] static Result<std::unique_ptr<XAudio2AudioDevice>> create();
    ~XAudio2AudioDevice() override;
    XAudio2AudioDevice(const XAudio2AudioDevice&) = delete;
    XAudio2AudioDevice& operator=(const XAudio2AudioDevice&) = delete;

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
    explicit XAudio2AudioDevice(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace eaw
