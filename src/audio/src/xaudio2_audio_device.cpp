#include "xaudio2_audio_device.hpp"

#include "core/log.hpp"
#include "audio/spatial_audio.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <xaudio2.h>

namespace eaw {
namespace {

std::string hresult_text(const HRESULT result)
{
    std::array<char, 16> text{};
    std::snprintf(text.data(), text.size(), "0x%08lX", static_cast<unsigned long>(result));
    return text.data();
}

// The pitch range a source voice is created for (XAudio2 needs the maximum up front).
constexpr float max_pitch = 4.0F;

} // namespace

struct XAudio2AudioDevice::Impl
{
    struct Voice
    {
        VoiceId id{};
        IXAudio2SourceVoice* source{};
        std::shared_ptr<const Sound> sound;
        std::uint16_t channels{};
        AudioCategory category{};
    };

    IXAudio2* engine{};
    IXAudio2MasteringVoice* master{};
    std::array<IXAudio2SubmixVoice*, audio_category_count> submixes{};
    std::uint32_t output_channels{};
    std::vector<Voice> voices;
    VoiceId next_voice{1};

    ~Impl()
    {
        for (auto& voice : voices)
        {
            voice.source->DestroyVoice();
        }
        voices.clear();
        for (auto* submix : submixes)
        {
            if (submix != nullptr)
            {
                submix->DestroyVoice();
            }
        }
        if (master != nullptr)
        {
            master->DestroyVoice();
        }
        if (engine != nullptr)
        {
            engine->Release();
        }
    }

    // Volume and pan as an output matrix into the category's submix: a mono source goes left and
    // right by the pan law; a stereo source keeps its channels, balanced by the pan. Outputs past
    // the first two (surround speakers) stay silent.
    void apply(const Voice& voice, const float volume, const float pan) const
    {
        const auto gains = stereo_gains(pan);
        std::vector<float> matrix(static_cast<std::size_t>(voice.channels) * output_channels, 0.0F);
        const auto at = [&](const std::uint32_t output, const std::uint32_t input) -> float&
        {
            return matrix[static_cast<std::size_t>(output) * voice.channels + input];
        };
        if (output_channels == 1)
        {
            for (std::uint32_t input = 0; input < voice.channels; ++input)
            {
                at(0, input) = volume / static_cast<float>(voice.channels);
            }
        }
        else if (voice.channels == 1)
        {
            at(0, 0) = volume * gains.left;
            at(1, 0) = volume * gains.right;
        }
        else
        {
            // Balance: the far side is attenuated, the near side kept at its recorded level (the
            // pan law normalized to the centre, never above 1).
            constexpr float centred = 0.70710678F;
            at(0, 0) = volume * std::min(1.0F, gains.left / centred);
            at(1, 1) = volume * std::min(1.0F, gains.right / centred);
        }
        voice.source->SetOutputMatrix(submixes[static_cast<std::size_t>(voice.category)], voice.channels, output_channels, matrix.data());
    }

    [[nodiscard]] Voice* find(const VoiceId id)
    {
        const auto found = std::ranges::find(voices, id, &Voice::id);
        return found == voices.end() ? nullptr : &*found;
    }
};

Result<std::unique_ptr<XAudio2AudioDevice>> XAudio2AudioDevice::create()
{
    auto impl = std::make_unique<Impl>();
    if (const auto result = XAudio2Create(&impl->engine, 0, XAUDIO2_DEFAULT_PROCESSOR); FAILED(result))
    {
        return make_error("XAudio2Create failed (" + hresult_text(result) + ")");
    }
    if (const auto result = impl->engine->CreateMasteringVoice(&impl->master); FAILED(result))
    {
        return make_error("XAudio2 could not open the audio output (" + hresult_text(result) + "; no output device?)");
    }
    XAUDIO2_VOICE_DETAILS details{};
    impl->master->GetVoiceDetails(&details);
    impl->output_channels = std::max<std::uint32_t>(details.InputChannels, 1U);
    for (auto& submix : impl->submixes)
    {
        if (const auto result = impl->engine->CreateSubmixVoice(&submix, impl->output_channels, details.InputSampleRate); FAILED(result))
        {
            return make_error("XAudio2 could not create a category mix (" + hresult_text(result) + ")");
        }
    }
    return std::unique_ptr<XAudio2AudioDevice>(new XAudio2AudioDevice(std::move(impl)));
}

XAudio2AudioDevice::XAudio2AudioDevice(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{}

XAudio2AudioDevice::~XAudio2AudioDevice() = default;

std::string_view XAudio2AudioDevice::name() const noexcept
{
    return "xaudio2";
}

VoiceId XAudio2AudioDevice::play(const SoundPlayback& playback)
{
    auto& impl = *impl_;
    const auto& sound = playback.sound;
    if (sound == nullptr || sound->channels == 0 || sound->sample_rate == 0 || sound->samples.empty() || impl.voices.size() >= max_audio_voices)
    {
        return 0;
    }
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = sound->channels;
    format.nSamplesPerSec = sound->sample_rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(sound->channels * sizeof(std::int16_t));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    XAUDIO2_SEND_DESCRIPTOR send{0, impl.submixes[static_cast<std::size_t>(playback.category)]};
    const XAUDIO2_VOICE_SENDS sends{1, &send};
    IXAudio2SourceVoice* source = nullptr;
    if (const auto result = impl.engine->CreateSourceVoice(&source, &format, 0, max_pitch, nullptr, &sends, nullptr); FAILED(result))
    {
        log_warning("audio", make_error("XAudio2 could not create a voice (" + hresult_text(result) + ")"));
        return 0;
    }
    XAUDIO2_BUFFER buffer{};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = static_cast<UINT32>(sound->samples.size() * sizeof(std::int16_t));
    buffer.pAudioData = reinterpret_cast<const BYTE*>(sound->samples.data());
    buffer.LoopCount = playback.loop ? XAUDIO2_LOOP_INFINITE : 0U;
    if (const auto result = source->SubmitSourceBuffer(&buffer); FAILED(result))
    {
        source->DestroyVoice();
        log_warning("audio", make_error("XAudio2 could not queue a sound (" + hresult_text(result) + ")"));
        return 0;
    }
    source->SetFrequencyRatio(std::clamp(playback.pitch, 1.0F / max_pitch, max_pitch));
    Impl::Voice voice{impl.next_voice++, source, sound, sound->channels, playback.category};
    impl.apply(voice, playback.volume, playback.pan);
    source->Start();
    impl.voices.push_back(std::move(voice));
    return impl.voices.back().id;
}

void XAudio2AudioDevice::update_voice(const VoiceId voice, const float volume, const float pan)
{
    if (const auto* found = impl_->find(voice))
    {
        impl_->apply(*found, volume, pan);
    }
}

void XAudio2AudioDevice::stop(const VoiceId voice)
{
    auto& voices = impl_->voices;
    const auto found = std::ranges::find(voices, voice, &Impl::Voice::id);
    if (found != voices.end())
    {
        // DestroyVoice waits for the audio thread to let go of the voice, so its buffer (the Sound)
        // may be released afterwards.
        found->source->DestroyVoice();
        voices.erase(found);
    }
}

bool XAudio2AudioDevice::playing(const VoiceId voice) const
{
    return std::ranges::find(impl_->voices, voice, &Impl::Voice::id) != impl_->voices.end();
}

void XAudio2AudioDevice::set_category_volume(const AudioCategory category, const float volume)
{
    impl_->submixes[static_cast<std::size_t>(category)]->SetVolume(std::max(volume, 0.0F));
}

void XAudio2AudioDevice::set_master_volume(const float volume)
{
    impl_->master->SetVolume(std::max(volume, 0.0F));
}

void XAudio2AudioDevice::update()
{
    std::erase_if(impl_->voices,
                  [](const Impl::Voice& voice)
                  {
                      XAUDIO2_VOICE_STATE state{};
                      voice.source->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
                      if (state.BuffersQueued != 0)
                      {
                          return false;
                      }
                      voice.source->DestroyVoice();
                      return true;
                  });
}

std::size_t XAudio2AudioDevice::active_voices() const noexcept
{
    return impl_->voices.size();
}

} // namespace eaw
