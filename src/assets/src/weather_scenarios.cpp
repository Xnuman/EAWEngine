#include "assets/weather_scenarios.hpp"

#include "core/string_utils.hpp"
#include "assets/type_fields.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace eaw {

namespace {

constexpr std::array<std::pair<std::string_view, WeatherKind>, 5> kind_names{{
    {"CLEAR", WeatherKind::clear},
    {"ASH", WeatherKind::ash},
    {"RAIN", WeatherKind::rain},
    {"SANDSTORM", WeatherKind::sandstorm},
    {"SNOW", WeatherKind::snow},
}};

WeatherKind weather_kind(const std::string_view name)
{
    const auto trimmed = ascii_trim(name);
    for (const auto& [text, kind] : kind_names)
    {
        if (ascii_equal_ignoring_case(trimmed, text))
        {
            return kind;
        }
    }
    return WeatherKind::clear;
}

std::string_view attribute(const std::vector<XmlAttribute>& attributes, const std::string_view name)
{
    for (const auto& entry : attributes)
    {
        if (ascii_equal_ignoring_case(entry.name, name))
        {
            return ascii_trim(entry.value);
        }
    }
    return {};
}

float lerp(const float a, const float b, const float t) noexcept
{
    return a + (b - a) * t;
}

} // namespace

WeatherScenarioCatalog WeatherScenarioCatalog::build(const XmlAssetDatabase& database, std::vector<std::string>* warnings)
{
    WeatherScenarioCatalog catalog;
    for (const auto& definition : database.definitions())
    {
        if (!ascii_equal_ignoring_case(definition.type, "Weather_Scenario") || catalog.scenarios_.contains(ascii_lower(definition.name)))
        {
            continue;
        }
        WeatherScenario scenario;
        scenario.name = definition.name;
        scenario.emitter = std::string(attribute(definition.attributes, "emitter_name"));
        for (const auto& node : definition.fields)
        {
            if (!ascii_equal_ignoring_case(node.name, "Phase"))
            {
                continue;
            }
            WeatherPhase phase;
            phase.kind = weather_kind(attribute(node.attributes, "name"));
            for (const auto& child : node.children)
            {
                float* target = ascii_equal_ignoring_case(child.name, "Emitter_Intensity")               ? &phase.emitter_intensity
                                : ascii_equal_ignoring_case(child.name, "Lightning_Intensity")           ? &phase.lightning_intensity
                                : ascii_equal_ignoring_case(child.name, "Duration")                      ? &phase.duration
                                : ascii_equal_ignoring_case(child.name, "Ease_Out_Duration")             ? &phase.ease_out
                                : ascii_equal_ignoring_case(child.name, "Fog_Plane_Interpolation_Alpha") ? &phase.fog_plane_alpha
                                                                                                         : nullptr;
                if (target == nullptr)
                {
                    continue;
                }
                const auto parsed = parse_xml_number(ascii_trim(child.text));
                if (!parsed || !std::isfinite(*parsed) || *parsed < 0.0F)
                {
                    if (warnings != nullptr)
                    {
                        warnings->push_back("weather scenario '" + scenario.name + "': " + child.name + " '" + child.text + "' is not usable");
                    }
                    continue;
                }
                *target = *parsed;
            }
            if (!(phase.duration > 0.0F))
            {
                if (warnings != nullptr)
                {
                    warnings->push_back("weather scenario '" + scenario.name + "': a phase without a positive duration is left out");
                }
                continue;
            }
            phase.ease_out = std::min(phase.ease_out, phase.duration);
            scenario.phases.push_back(phase);
        }
        catalog.scenarios_.emplace(ascii_lower(definition.name), std::move(scenario));
    }
    return catalog;
}

const WeatherScenario* WeatherScenarioCatalog::find(const std::string_view name) const
{
    const auto found = scenarios_.find(ascii_lower(ascii_trim(name)));
    return found != scenarios_.end() ? &found->second : nullptr;
}

std::size_t WeatherScenarioCatalog::size() const noexcept
{
    return scenarios_.size();
}

WeatherState sample_weather(const WeatherScenario& scenario, const double seconds) noexcept
{
    const auto& phases = scenario.phases;
    if (phases.empty())
    {
        return {};
    }
    double cycle = 0.0;
    for (const auto& phase : phases)
    {
        cycle += phase.duration;
    }
    auto time = std::fmod(std::max(seconds, 0.0), cycle);
    std::size_t index = 0;
    while (index + 1 < phases.size() && time >= phases[index].duration)
    {
        time -= phases[index].duration;
        ++index;
    }
    const auto& phase = phases[index];
    const auto& next = phases[(index + 1) % phases.size()];
    const auto ease_start = phase.duration - phase.ease_out;
    const auto t = phase.ease_out > 0.0F && time > ease_start ? std::clamp(static_cast<float>((time - ease_start) / phase.ease_out), 0.0F, 1.0F) : 0.0F;
    return {phase.kind, lerp(phase.emitter_intensity, next.emitter_intensity, t), lerp(phase.lightning_intensity, next.lightning_intensity, t), lerp(phase.fog_plane_alpha, next.fog_plane_alpha, t)};
}

const char* to_string(const WeatherKind kind) noexcept
{
    for (const auto& [text, value] : kind_names)
    {
        if (value == kind)
        {
            return text.data();
        }
    }
    return "CLEAR";
}

} // namespace eaw
