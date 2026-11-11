#include "fmod_audio_device.hpp"

#include "audio/spatial_audio.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include <fmod.hpp>
#include <fmod_errors.h>

namespace eaw {
namespace {

std::string fmod_text(const FMOD_RESULT result)
{
    return FMOD_ErrorString(result);
}

// The pitch range voices are clamped to, as for the XAudio2 backend.
constexpr float max_pitch = 4.0F;

} // namespace

struct FmodAudioDevice::Impl
{
    struct Voice
    {
        VoiceId id{};
        FMOD::Sound* sound{};
        FMOD::Channel* channel{};
        std::shared_ptr<const Sound> samples;
        std::uint16_t channels{};
    };

    FMOD::System* system{};
    std::array<FMOD::ChannelGroup*, audio_category_count> groups{};
    std::vector<Voice> voices;
    VoiceId next_voice{1};

    ~Impl()
    {
        for (auto& voice : voices)
        {
            release(voice);
        }
        voices.clear();
        for (auto* group : groups)
        {
            if (group != nullptr)
            {
                group->release();
            }
        }
        if (system != nullptr)
        {
            system->release();
        }
    }

    static void release(Voice& voice)
    {
        if (voice.channel != nullptr)
        {
            voice.channel->stop();
        }
        if (voice.sound != nullptr)
        {
            voice.sound->release();
        }
    }

    // Volume and pan as mix levels into the front left and right speakers, by the shared pan law
    // (a stereo source is balanced as in the XAudio2 backend).
    static void apply(const Voice& voice, const float volume, const float pan)
    {
        const auto gains = stereo_gains(pan);
        if (voice.channels == 1)
        {
            voice.channel->setMixLevelsOutput(volume * gains.left, volume * gains.right, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F);
            return;
        }
        constexpr float centred = 0.70710678F;
        const std::array<float, 2> levels{volume * std::min(1.0F, gains.left / centred), volume * std::min(1.0F, gains.right / centred)};
        // Rows are output speakers (left, right), columns the source's channels.
        const std::array<float, 4> matrix{levels[0], 0.0F, 0.0F, levels[1]};
        voice.channel->setMixMatrix(const_cast<float*>(matrix.data()), 2, 2, 2);
    }
};

Result<std::unique_ptr<FmodAudioDevice>> FmodAudioDevice::create()
{
    auto impl = std::make_unique<Impl>();
    if (const auto result = FMOD::System_Create(&impl->system); result != FMOD_OK)
    {
        return make_error("FMOD System_Create failed (" + fmod_text(result) + ")");
    }
    if (const auto result = impl->system->init(static_cast<int>(max_audio_voices), FMOD_INIT_NORMAL, nullptr); result != FMOD_OK)
    {
        return make_error("FMOD could not open the audio output (" + fmod_text(result) + ")");
    }
    for (auto& group : impl->groups)
    {
        if (const auto result = impl->system->createChannelGroup(nullptr, &group); result != FMOD_OK)
        {
            return make_error("FMOD could not create a category mix (" + fmod_text(result) + ")");
        }
    }
    return std::unique_ptr<FmodAudioDevice>(new FmodAudioDevice(std::move(impl)));
}

FmodAudioDevice::FmodAudioDevice(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{}

FmodAudioDevice::~FmodAudioDevice() = default;

std::string_view FmodAudioDevice::name() const noexcept
{
    return "fmod";
}

VoiceId FmodAudioDevice::play(const SoundPlayback& playback)
{
    auto& impl = *impl_;
    const auto& samples = playback.sound;
    if (samples == nullptr || samples->channels == 0 || samples->sample_rate == 0 || samples->samples.empty() || impl.voices.size() >= max_audio_voices)
    {
        return 0;
    }
    FMOD_CREATESOUNDEXINFO info{};
    info.cbsize = sizeof(info);
    info.length = static_cast<unsigned int>(samples->samples.size() * sizeof(std::int16_t));
    info.numchannels = samples->channels;
    info.defaultfrequency = static_cast<int>(samples->sample_rate);
    info.format = FMOD_SOUND_FORMAT_PCM16;
    // The engine's PCM is used in place (FMOD_OPENMEMORY_POINT): the Sound outlives the FMOD sound.
    const FMOD_MODE mode = FMOD_OPENMEMORY_POINT | FMOD_OPENRAW | FMOD_CREATESAMPLE | (playback.loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);
    Impl::Voice voice{impl.next_voice, nullptr, nullptr, samples, samples->channels};
    if (const auto result = impl.system->createSound(reinterpret_cast<const char*>(samples->samples.data()), mode, &info, &voice.sound); result != FMOD_OK)
    {
        log_warning("audio", make_error("FMOD could not create a sound (" + fmod_text(result) + ")"));
        return 0;
    }
    if (const auto result = impl.system->playSound(voice.sound, impl.groups[static_cast<std::size_t>(playback.category)], true, &voice.channel); result != FMOD_OK)
    {
        Impl::release(voice);
        log_warning("audio", make_error("FMOD could not play a sound (" + fmod_text(result) + ")"));
        return 0;
    }
    voice.channel->setPitch(std::clamp(playback.pitch, 1.0F / max_pitch, max_pitch));
    Impl::apply(voice, playback.volume, playback.pan);
    voice.channel->setPaused(false);
    ++impl.next_voice;
    impl.voices.push_back(std::move(voice));
    return impl.voices.back().id;
}

void FmodAudioDevice::update_voice(const VoiceId voice, const float volume, const float pan)
{
    const auto found = std::ranges::find(impl_->voices, voice, &Impl::Voice::id);
    if (found != impl_->voices.end())
    {
        Impl::apply(*found, volume, pan);
    }
}

void FmodAudioDevice::stop(const VoiceId voice)
{
    auto& voices = impl_->voices;
    const auto found = std::ranges::find(voices, voice, &Impl::Voice::id);
    if (found != voices.end())
    {
        Impl::release(*found);
        voices.erase(found);
    }
}

bool FmodAudioDevice::playing(const VoiceId voice) const
{
    return std::ranges::find(impl_->voices, voice, &Impl::Voice::id) != impl_->voices.end();
}

void FmodAudioDevice::set_category_volume(const AudioCategory category, const float volume)
{
    impl_->groups[static_cast<std::size_t>(category)]->setVolume(std::max(volume, 0.0F));
}

void FmodAudioDevice::set_master_volume(const float volume)
{
    FMOD::ChannelGroup* master = nullptr;
    if (impl_->system->getMasterChannelGroup(&master) == FMOD_OK && master != nullptr)
    {
        master->setVolume(std::max(volume, 0.0F));
    }
}

void FmodAudioDevice::update()
{
    impl_->system->update();
    std::erase_if(impl_->voices,
                  [](Impl::Voice& voice)
                  {
                      bool is_playing = false;
                      if (voice.channel->isPlaying(&is_playing) == FMOD_OK && is_playing)
                      {
                          return false;
                      }
                      Impl::release(voice);
                      return true;
                  });
}

std::size_t FmodAudioDevice::active_voices() const noexcept
{
    return impl_->voices.size();
}

} // namespace eaw
