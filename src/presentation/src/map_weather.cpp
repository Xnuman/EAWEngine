#include "presentation/map_weather.hpp"

#include "math/matrix.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

void MapWeather::reset(std::shared_ptr<const WeatherSource> source, const std::uint64_t seed)
{
    source_ = std::move(source);
    effect_ = {};
    clock_ = 0.0;
    state_ = {};
    flash_ = 0.0F;
    random_.seed(static_cast<std::minstd_rand::result_type>(seed % std::minstd_rand::modulus) + 1U);
}

void MapWeather::update(ParticleWorld& particles, const float seconds, const Vector3& eye, const Vector3& forward)
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

    if (source_->effect == nullptr)
    {
        return;
    }
    float distance = 0.0F;
    for (const auto& emitter : source_->effect->emitters)
    {
        distance = std::max(distance, emitter.weather_distance);
    }
    auto transform = Matrix4::identity();
    set_translation(transform, eye + forward * (distance > 0.0F ? distance : default_weather_distance));
    if (!particles.playing(effect_))
    {
        effect_ = particles.play(source_->effect, transform, random_());
    }
    particles.set_transform(effect_, transform);
    particles.set_emission_rate(effect_, state_.emitter_intensity);
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
