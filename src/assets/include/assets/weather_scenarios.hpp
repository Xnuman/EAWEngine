#pragma once

// Weather scenarios (XML WeatherScenarios.xml, `Weather_Scenario`): what a land map's environment
// names as its weather (TED environment field 0x29). A scenario names the particle effect that
// draws its weather (`emitter_name`) and plays its phases in order: each phase a weather kind (the
// phase's name, which also picks the gameplay modifiers in WeatherModifiers.xml), how strongly the
// emitter runs, how much lightning there is, how long it lasts, how long it takes to ease into the
// next phase, and a fog-plane value whose meaning is not known yet.
//
// Inferences, not evidence: the phases loop once the last has played; a phase's ease-out is the
// last part of its duration, blending its values toward the next phase's.

#include "assets/xml_asset_database.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eaw {

// The five kinds the game knows; any other phase name means clear (as the stock file's header says).
enum class WeatherKind : std::uint8_t
{
    clear,
    ash,
    rain,
    sandstorm,
    snow,
};

struct WeatherPhase
{
    WeatherKind kind{WeatherKind::clear};
    float emitter_intensity{};
    float lightning_intensity{};
    float duration{};
    float ease_out{};
    // Fog_Plane_Interpolation_Alpha, as written (0 when absent); not interpreted yet.
    float fog_plane_alpha{};
};

struct WeatherScenario
{
    std::string name;
    // The particle effect file (an .alo under Data/Art/Models), or empty.
    std::string emitter;
    std::vector<WeatherPhase> phases;
};

// The weather at a moment of a scenario: the playing phase's kind and the values eased between it
// and the next.
struct WeatherState
{
    WeatherKind kind{WeatherKind::clear};
    float emitter_intensity{};
    float lightning_intensity{};
    float fog_plane_alpha{};
};

class WeatherScenarioCatalog final
{
public:
    // Malformed numbers are reported to `warnings` when given and read as 0; a phase without a
    // positive duration is reported and left out.
    [[nodiscard]] static WeatherScenarioCatalog build(const XmlAssetDatabase& database, std::vector<std::string>* warnings = nullptr);

    // A scenario by name, ignoring case; null when unknown.
    [[nodiscard]] const WeatherScenario* find(std::string_view name) const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::unordered_map<std::string, WeatherScenario> scenarios_;
};

// The scenario's weather `seconds` after it started (clear without phases).
[[nodiscard]] WeatherState sample_weather(const WeatherScenario& scenario, double seconds) noexcept;

[[nodiscard]] const char* to_string(WeatherKind kind) noexcept;

} // namespace eaw
