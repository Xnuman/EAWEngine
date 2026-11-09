#pragma once

// What scripts ask of the engine: every call of a host function or of a method on an object a host
// function returned, by name, with how often, from which scripts, and with which argument types.
// The script host feeds it; a survey over the stock scripts turns it into the host API to build,
// ranked by use.

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace eaw {

struct HostCallRecord
{
    // A function the host implements (Create_Thread, Sleep, ...) or a recording stub.
    bool implemented{};
    std::uint64_t calls{};
    std::set<std::string> scripts;
    // Argument type lists ("string, number, object(Find_Player)") and how often each was seen.
    std::map<std::string, std::uint64_t> signatures;
};

class HostApiRecorder
{
public:
    // `name` is a global function ("Find_Player") or a method ("Find_Player().Get_Faction_Name").
    void record(std::string_view name, std::string_view script, std::string_view signature, bool implemented);

    [[nodiscard]] const std::map<std::string, HostCallRecord>& calls() const noexcept
    {
        return calls_;
    }

private:
    std::map<std::string, HostCallRecord> calls_;
};

} // namespace eaw
