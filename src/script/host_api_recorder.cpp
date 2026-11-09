#include "eaw/host_api_recorder.hpp"

namespace eaw {

void HostApiRecorder::record(const std::string_view name, const std::string_view script, const std::string_view signature, const bool implemented)
{
    auto& entry = calls_[std::string(name)];
    entry.implemented = entry.implemented || implemented;
    ++entry.calls;
    entry.scripts.emplace(script);
    ++entry.signatures[std::string(signature)];
}

} // namespace eaw
