#include "audio/audio_devices.hpp"

#include "audio/null_audio_device.hpp"

#ifdef _WIN32
#include "xaudio2_audio_device.hpp"
#endif
#ifdef EAW_AUDIO_FMOD
#include "fmod_audio_device.hpp"
#endif

#include <string>

namespace eaw {

std::vector<std::string_view> audio_backends()
{
    std::vector<std::string_view> backends;
#ifdef _WIN32
    backends.emplace_back("xaudio2");
#endif
#ifdef EAW_AUDIO_FMOD
    backends.emplace_back("fmod");
#endif
    backends.emplace_back("none");
    return backends;
}

Result<std::unique_ptr<AudioDevice>> create_audio_device(const std::string_view backend)
{
    if (backend == "none")
    {
        return std::unique_ptr<AudioDevice>(std::make_unique<NullAudioDevice>());
    }
#ifdef _WIN32
    if (backend == "xaudio2")
    {
        EAW_TRY_ASSIGN(auto device, XAudio2AudioDevice::create());
        return std::unique_ptr<AudioDevice>(std::move(device));
    }
#endif
#ifdef EAW_AUDIO_FMOD
    if (backend == "fmod")
    {
        EAW_TRY_ASSIGN(auto device, FmodAudioDevice::create());
        return std::unique_ptr<AudioDevice>(std::move(device));
    }
#endif
    std::string available;
    for (const auto name : audio_backends())
    {
        available += (available.empty() ? "" : ", ") + std::string(name);
    }
    if (backend == "fmod")
    {
        return make_error("this build has no FMOD backend (configure with EAW_AUDIO_FMOD=ON and FMOD_ROOT pointing at a local FMOD SDK); available: " + available);
    }
    return make_error("unknown audio backend '" + std::string(backend) + "'; available: " + available);
}

} // namespace eaw
