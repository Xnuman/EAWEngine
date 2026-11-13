#pragma once

// A land map's weather, presented: its scenario's timeline on the world clock, its particle effect
// laid over the ground the camera sees, emitting as strongly as the playing phase says, and lightning
// flashes as often as the phase's lightning intensity says. Presentation only: the weather kind is
// not yet read by gameplay (WeatherModifiers.xml); when it is, the timeline moves to the battle's
// ticks.
//
// Engine choices (how the original covers the view is not known): the effect plays once per tile of
// a world-aligned grid, a tile as wide as the effect's spawn volume, over the ground within
// weather_view_reach x the camera's distance of its focus; each tile hangs high enough that its
// particles reach the ground. When the tiles would hold more than weather_particle_budget particles,
// every tile emits less. Tiles stay where they are as the camera moves; those that leave the view
// stop emitting and their particles live out. A flash comes on average every
// lightning_mean_interval / intensity seconds and fades over lightning_flash_seconds.

#include "math/vector.hpp"
#include "render/visual_frame.hpp"
#include "assets/particle_rules.hpp"
#include "assets/weather_scenarios.hpp"
#include "presentation/particle_effects.hpp"

#include <cstdint>
#include <memory>
#include <random>
#include <unordered_map>

namespace eaw {

// What a map's weather plays: its scenario and the effect that draws it (null without one).
struct WeatherSource
{
    WeatherScenario scenario;
    std::shared_ptr<const ParticleEffect> effect;
};

// A tile's side when the effect's spawn volume has no size.
inline constexpr float default_weather_tile = 250.0F;
inline constexpr float weather_view_reach = 1.2F;
// At most this many tiles along each side of the covered square.
inline constexpr int max_weather_tiles_per_side = 16;
inline constexpr float weather_particle_budget = 40000.0F;
inline constexpr float lightning_mean_interval = 2.0F;
inline constexpr float lightning_flash_seconds = 0.35F;
// How much a full flash adds to the ambient light and the key light (engine choice).
inline constexpr float lightning_ambient_gain = 0.8F;
inline constexpr float lightning_key_gain = 0.6F;

class MapWeather
{
public:
    // Starts `source`'s scenario from its beginning (null: no weather). The particle world's old
    // weather effects, if any, are the caller's to clear.
    void reset(std::shared_ptr<const WeatherSource> source, std::uint64_t seed = 1);

    // Advances the weather by `seconds` and covers the ground around `focus` (the point the camera
    // looks at) for a camera at `eye`.
    void update(ParticleWorld& particles, float seconds, const Vector3& eye, const Vector3& focus);

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
    [[nodiscard]] std::size_t tiles() const noexcept
    {
        return tiles_.size();
    }

private:
    struct Tile
    {
        ParticleInstanceId effect;
        std::uint64_t seen{};
    };

    std::shared_ptr<const WeatherSource> source_;
    std::unordered_map<std::uint64_t, Tile> tiles_;
    std::uint64_t frame_{};
    // From the effect: a tile's side, how high a tile hangs, and the particles one tile holds at full
    // strength.
    float tile_size_{default_weather_tile};
    float tile_height_{};
    float tile_particles_{};
    double clock_{};
    WeatherState state_;
    float flash_{};
    std::minstd_rand random_;
};

// `base` lit by a lightning flash of strength `flash` (0..1).
[[nodiscard]] VisualLightRig lightning_rig(const VisualLightRig& base, float flash) noexcept;

} // namespace eaw
