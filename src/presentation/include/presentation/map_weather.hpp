#pragma once

// A land map's weather, presented: its scenario's timeline on the world clock, its particle effect
// kept ahead of the camera and emitting as strongly as the playing phase says, and lightning flashes
// as often as the phase's lightning intensity says. Presentation only: the weather kind is not yet
// read by gameplay (WeatherModifiers.xml); when it is, the timeline moves to the battle's ticks.
//
// Engine choices (the original's placement and lightning are not known): the effect sits on the
// view ray at its emitters' weather cube distance (default_weather_distance without one), in world
// axes, its particles left where they fell; a flash comes on average every
// lightning_mean_interval / intensity seconds and fades over lightning_flash_seconds.

#include "math/vector.hpp"
#include "render/visual_frame.hpp"
#include "assets/particle_rules.hpp"
#include "assets/weather_scenarios.hpp"
#include "presentation/particle_effects.hpp"

#include <cstdint>
#include <memory>
#include <random>

namespace eaw {

// What a map's weather plays: its scenario and the effect that draws it (null without one).
struct WeatherSource
{
    WeatherScenario scenario;
    std::shared_ptr<const ParticleEffect> effect;
};

inline constexpr float default_weather_distance = 500.0F;
inline constexpr float lightning_mean_interval = 2.0F;
inline constexpr float lightning_flash_seconds = 0.35F;
// How much a full flash adds to the ambient light and the key light (engine choice).
inline constexpr float lightning_ambient_gain = 0.8F;
inline constexpr float lightning_key_gain = 0.6F;

class MapWeather
{
public:
    // Starts `source`'s scenario from its beginning (null: no weather). The particle world's old
    // weather effect, if any, is the caller's to clear.
    void reset(std::shared_ptr<const WeatherSource> source, std::uint64_t seed = 1);

    // Advances the weather by `seconds` and places its effect for a camera at `eye` looking along
    // `forward` (unit length).
    void update(ParticleWorld& particles, float seconds, const Vector3& eye, const Vector3& forward);

    [[nodiscard]] const WeatherState& state() const noexcept
    {
        return state_;
    }
    // The lightning flash now, 0..1.
    [[nodiscard]] float flash() const noexcept
    {
        return flash_;
    }
    [[nodiscard]] bool active() const noexcept
    {
        return source_ != nullptr;
    }

private:
    std::shared_ptr<const WeatherSource> source_;
    ParticleInstanceId effect_;
    double clock_{};
    WeatherState state_;
    float flash_{};
    std::minstd_rand random_;
};

// `base` lit by a lightning flash of strength `flash` (0..1).
[[nodiscard]] VisualLightRig lightning_rig(const VisualLightRig& base, float flash) noexcept;

} // namespace eaw
