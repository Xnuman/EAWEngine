#pragma once

// The audio backends and the choice between them. XAudio2 is the default on Windows (part of the
// system, nothing to install); FMOD is built only when the build is pointed at a local FMOD SDK
// (EAW_AUDIO_FMOD=ON with FMOD_ROOT), since its license does not let the SDK be committed or
// fetched; "none" is the null device.

#include "audio/audio_device.hpp"
#include "core/status.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace eaw {

// The backends this build can create, the default first.
[[nodiscard]] std::vector<std::string_view> audio_backends();

// Creates a backend by name ("xaudio2", "fmod", "none"); an unknown or unavailable one is an
// error naming the available ones, and a backend that fails to start reports why.
[[nodiscard]] Result<std::unique_ptr<AudioDevice>> create_audio_device(std::string_view backend);

} // namespace eaw
