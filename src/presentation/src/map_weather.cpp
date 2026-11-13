#include "presentation/map_weather.hpp"

#include "math/matrix.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

namespace {

// The side of a spawn shape's footprint (0 when it has none).
float shape_width(const AloParticleGroup& shape)
{
    switch (shape.type)
    {
    case 1:
        return std::max(shape.maximum.x - shape.minimum.x, shape.maximum.y - shape.minimum.y);
    case 2:
        return shape.side_length;
    case 3:
        return 2.0F * shape.sphere_radius;
    case 4:
        return 2.0F * shape.cylinder_radius;
    default:
        return 0.0F;
    }
}

// The downward speed a spawn shape gives (positive down).
float falling_speed(const AloParticleGroup& shape)
{
    return shape.type == 1 ? -(shape.minimum.z + shape.maximum.z) * 0.5F : shape.type == 0 ? -shape.value.z : 0.0F;
}

std::uint64_t tile_key(const std::int32_t x, const std::int32_t y)
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) | static_cast<std::uint32_t>(y);
}

bool usable(const float value)
{
    return std::isfinite(value) && value > 0.0F;
}

} // namespace

void MapWeather::reset(std::shared_ptr<const WeatherSource> source, const std::uint64_t seed)
{
    source_ = std::move(source);
    tiles_.clear();
    frame_ = 0;
    clock_ = 0.0;
    state_ = {};
    flash_ = 0.0F;
    random_.seed(static_cast<std::minstd_rand::result_type>(seed % std::minstd_rand::modulus) + 1U);
    tile_size_ = 0.0F;
    tile_height_ = 0.0F;
    tile_particles_ = 0.0F;
    if (source_ == nullptr || source_->effect == nullptr)
    {
        return;
    }
    // A tile spans the widest spawn volume and hangs so that its particles fall to the ground within
    // their life (half the volume above the ground at least).
    float fall = 0.0F;
    for (const auto& emitter : source_->effect->emitters)
    {
        if (!emitter.drawn || emitter.interval <= 0.0F)
        {
            continue;
        }
        const auto width = shape_width(emitter.position_shape);
        tile_size_ = std::max(tile_size_, usable(width) ? width : 0.0F);
        const auto speed = falling_speed(emitter.velocity_shape);
        const auto drop = speed * emitter.lifetime + 0.5F * emitter.gravity * emitter.lifetime * emitter.lifetime;
        fall = std::max(fall, usable(drop) ? drop : 0.0F);
        tile_particles_ += static_cast<float>(emitter.per_spawn) / emitter.interval * emitter.lifetime;
    }
    if (!usable(tile_size_))
    {
        tile_size_ = default_weather_tile;
    }
    tile_height_ = std::max(fall - tile_size_ * 0.5F, tile_size_ * 0.5F);
}

void MapWeather::update(ParticleWorld& particles, const float seconds, const Vector3& eye, const Vector3& focus)
{
    if (source_ == nullptr)
    {
        return;
    }
    const auto step = std::isfinite(seconds) ? std::max(seconds, 0.0F) : 0.0F;
    clock_ += step;
    state_ = sample_weather(source_->scenario, clock_);

    // Lightning: a flash fades out; a new one strikes with a chance that grows with the intensity.
    flash_ = std::max(flash_ - step / lightning_flash_seconds, 0.0F);
    if (state_.lightning_intensity > 0.0F && step > 0.0F)
    {
        const auto chance = 1.0F - std::exp(-step * state_.lightning_intensity / lightning_mean_interval);
        if (std::uniform_real_distribution<float>(0.0F, 1.0F)(random_) < chance)
        {
            flash_ = 1.0F;
        }
    }

    if (source_->effect == nullptr || !is_finite(eye) || !is_finite(focus))
    {
        return;
    }
    // The tiles over the ground in view, at most max_weather_tiles_per_side along each side.
    ++frame_;
    const auto reach = std::min(length(eye - focus) * weather_view_reach + tile_size_ * 0.5F, tile_size_ * static_cast<float>(max_weather_tiles_per_side) * 0.5F);
    const auto span = [&](const float center, std::int32_t& first, std::int32_t& last)
    {
        first = static_cast<std::int32_t>(std::floor((center - reach) / tile_size_));
        last = std::min(static_cast<std::int32_t>(std::floor((center + reach) / tile_size_)), first + max_weather_tiles_per_side - 1);
    };
    std::int32_t first_x = 0;
    std::int32_t last_x = 0;
    std::int32_t first_y = 0;
    std::int32_t last_y = 0;
    span(focus.x, first_x, last_x);
    span(focus.y, first_y, last_y);
    const auto count = static_cast<float>(last_x - first_x + 1) * static_cast<float>(last_y - first_y + 1);
    const auto thinning = tile_particles_ * count > weather_particle_budget ? weather_particle_budget / (tile_particles_ * count) : 1.0F;
    const auto rate = state_.emitter_intensity * thinning;
    for (auto x = first_x; x <= last_x; ++x)
    {
        for (auto y = first_y; y <= last_y; ++y)
        {
            auto& tile = tiles_[tile_key(x, y)];
            tile.seen = frame_;
            if (!particles.playing(tile.effect))
            {
                auto transform = Matrix4::identity();
                set_translation(transform, Vector3{(static_cast<float>(x) + 0.5F) * tile_size_, (static_cast<float>(y) + 0.5F) * tile_size_, focus.z + tile_height_});
                tile.effect = particles.play(source_->effect, transform, random_());
            }
            particles.set_emission_rate(tile.effect, rate);
        }
    }
    // Tiles out of view stop; their particles live out.
    std::erase_if(tiles_,
                  [&](const auto& entry)
                  {
                      if (entry.second.seen == frame_)
                      {
                          return false;
                      }
                      particles.stop(entry.second.effect);
                      return true;
                  });
}

VisualLightRig lightning_rig(const VisualLightRig& base, const float flash) noexcept
{
    auto rig = base;
    const auto amount = std::clamp(flash, 0.0F, 1.0F);
    for (std::size_t channel = 0; channel < 3; ++channel)
    {
        rig.ambient[channel] += amount * lightning_ambient_gain;
        rig.key.color[channel] += amount * lightning_key_gain;
    }
    return rig;
}

} // namespace eaw
