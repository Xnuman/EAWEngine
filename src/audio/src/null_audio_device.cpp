#include "audio/null_audio_device.hpp"

#include <algorithm>

namespace eaw {

std::string_view NullAudioDevice::name() const noexcept
{
    return "none";
}

VoiceId NullAudioDevice::play(const SoundPlayback& playback)
{
    if (playback.sound == nullptr || active_.size() >= max_audio_voices)
    {
        return 0;
    }
    const auto voice = next_voice_++;
    if (played_.size() < max_recorded_plays)
    {
        played_.push_back({voice, playback});
    }
    active_.push_back({voice, playback.loop});
    return voice;
}

void NullAudioDevice::update_voice(const VoiceId voice, const float volume, const float pan)
{
    const auto found = std::ranges::find(played_, voice, &Played::voice);
    if (found != played_.end() && playing(voice))
    {
        found->playback.volume = volume;
        found->playback.pan = pan;
    }
}

void NullAudioDevice::stop(const VoiceId voice)
{
    std::erase_if(active_,
                  [&](const Active& active)
                  {
                      return active.voice == voice;
                  });
}

bool NullAudioDevice::playing(const VoiceId voice) const
{
    return std::ranges::find(active_, voice, &Active::voice) != active_.end();
}

void NullAudioDevice::set_category_volume(const AudioCategory category, const float volume)
{
    category_volumes_[static_cast<std::size_t>(category)] = volume;
}

void NullAudioDevice::set_master_volume(const float volume)
{
    master_volume_ = volume;
}

float NullAudioDevice::master_volume() const noexcept
{
    return master_volume_;
}

void NullAudioDevice::update()
{
    std::erase_if(active_,
                  [](const Active& active)
                  {
                      return !active.loop;
                  });
}

std::size_t NullAudioDevice::active_voices() const noexcept
{
    return active_.size();
}

const std::vector<NullAudioDevice::Played>& NullAudioDevice::played() const noexcept
{
    return played_;
}

float NullAudioDevice::category_volume(const AudioCategory category) const noexcept
{
    return category_volumes_[static_cast<std::size_t>(category)];
}

} // namespace eaw
