#include "assets/animation_events.hpp"

#include "core/string_utils.hpp"

#include <algorithm>
#include <charconv>

namespace eaw {
namespace {

// The lower-case file stem of a name or path.
std::string animation_key(const std::string_view animation)
{
    auto name = animation;
    if (const auto slash = name.find_last_of("/\\"); slash != std::string_view::npos)
    {
        name.remove_prefix(slash + 1U);
    }
    if (const auto dot = name.find_last_of('.'); dot != std::string_view::npos)
    {
        name = name.substr(0, dot);
    }
    return ascii_lower(name);
}

} // namespace

AnimationEventTable AnimationEventTable::build(const AnimationSfxMapData& data, std::vector<std::string>* warnings)
{
    AnimationEventTable table;
    const auto warn = [&](const AnimationSfxMapRowData& row, const std::string& message)
    {
        if (warnings != nullptr)
        {
            warnings->push_back(std::string(animation_events_path) + " line " + std::to_string(row.line) + ": " + message);
        }
    };
    for (const auto& row : data.rows)
    {
        const auto& kind = row.fields.front();
        const bool sound = ascii_equal_ignoring_case(kind, "SOUND");
        const bool surface = ascii_equal_ignoring_case(kind, "SURFACE");
        if (!sound && !surface)
        {
            warn(row, "unknown row kind '" + kind + "'");
            continue;
        }
        if (row.fields.size() < (sound ? 4U : 5U))
        {
            warn(row, "a " + kind + " row needs " + (sound ? "an animation, a time, and a sound event" : "an animation, a time, a trigger, and a bone"));
            continue;
        }
        const auto& time = row.fields[2];
        std::uint32_t hundredths = 0;
        if (const auto [end, error] = std::from_chars(time.data(), time.data() + time.size(), hundredths); error != std::errc{} || end != time.data() + time.size())
        {
            warn(row, "time '" + time + "' is not a whole number");
            continue;
        }
        // NONE names nothing (the data's placeholder, as in SURFACEFX.XML).
        if (ascii_equal_ignoring_case(row.fields[3], none_name))
        {
            continue;
        }
        AnimationEvent event;
        event.kind = sound ? AnimationEventKind::sound : AnimationEventKind::surface;
        event.frame = static_cast<float>(hundredths) / animation_event_time_per_frame;
        event.name = row.fields[3];
        if (surface)
        {
            event.bone = row.fields[4];
        }
        table.events_[animation_key(row.fields[1])].push_back(std::move(event));
        ++table.size_;
    }
    for (auto& [name, events] : table.events_)
    {
        std::ranges::stable_sort(events, {}, &AnimationEvent::frame);
    }
    return table;
}

std::span<const AnimationEvent> AnimationEventTable::find(const std::string_view animation) const
{
    const auto found = events_.find(animation_key(animation));
    return found != events_.end() ? std::span<const AnimationEvent>(found->second) : std::span<const AnimationEvent>{};
}

std::size_t AnimationEventTable::size() const noexcept
{
    return size_;
}

AnimationEventTable load_animation_events(const AssetReader& reader, std::vector<std::string>* warnings)
{
    const auto bytes = reader.read(animation_events_path);
    const auto data = bytes ? read_animation_sfx_map(*bytes) : Result<AnimationSfxMapData>(bytes.error());
    if (!data)
    {
        if (warnings != nullptr)
        {
            warnings->push_back(std::string(animation_events_path) + ": " + data.error().message + "; animations play no sounds");
        }
        return {};
    }
    return AnimationEventTable::build(*data, warnings);
}

} // namespace eaw
