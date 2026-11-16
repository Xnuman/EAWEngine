#include "render/ocean_spectrum.hpp"

#include "core/random.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace eaw {

namespace {

// JONSWAP's peak enhancement and its widths below and above the peak (Hasselmann et al. 1973, as
// Horvath gives them).
constexpr float jonswap_gamma = 3.3F;
constexpr float jonswap_sigma_below = 0.07F;
constexpr float jonswap_sigma_above = 0.09F;
// The spreading exponent's bounds (keeps the normalization finite and the spread from collapsing to a
// line); an engine choice.
constexpr float spread_exponent_min = 0.1F;
constexpr float spread_exponent_max = 200.0F;

float dimensionless_fetch(const OceanSeaState& state) noexcept
{
    const auto wind = std::max(state.wind_speed, 0.01F);
    return std::max(state.fetch, 1.0F) * state.gravity / (wind * wind);
}

} // namespace

float ocean_peak_frequency(const OceanSeaState& state) noexcept
{
    // omega_p = 22 (g^2 / (U F))^(1/3), written through the dimensionless fetch g F / U^2.
    const auto wind = std::max(state.wind_speed, 0.01F);
    return 22.0F * state.gravity / wind * std::pow(dimensionless_fetch(state), -1.0F / 3.0F);
}

float ocean_frequency_spectrum(const float omega, const OceanSeaState& state) noexcept
{
    if (!(omega > 0.0F) || !(state.gravity > 0.0F))
    {
        return 0.0F;
    }
    const auto peak = ocean_peak_frequency(state);
    const auto alpha = 0.076F * std::pow(dimensionless_fetch(state), -0.22F);
    const auto sigma = omega <= peak ? jonswap_sigma_below : jonswap_sigma_above;
    const auto offset = omega - peak;
    const auto enhancement = std::pow(jonswap_gamma, std::exp(-offset * offset / (2.0F * sigma * sigma * peak * peak)));
    const auto ratio = peak / omega;
    const auto spectrum = alpha * state.gravity * state.gravity / std::pow(omega, 5.0F) * std::exp(-1.25F * ratio * ratio * ratio * ratio) * enhancement;
    // TMA: Kitaigorodskii's depth attenuation by omega_h = omega sqrt(depth / g).
    const auto omega_h = omega * std::sqrt(std::max(state.depth, 0.0F) / state.gravity);
    const auto depth_factor = omega_h <= 1.0F ? 0.5F * omega_h * omega_h : omega_h < 2.0F ? 1.0F - 0.5F * (2.0F - omega_h) * (2.0F - omega_h) : 1.0F;
    return spectrum * depth_factor;
}

float ocean_directional_spread(const float omega, const float theta, const OceanSeaState& state) noexcept
{
    const auto peak = ocean_peak_frequency(state);
    if (!(omega > 0.0F) || !(peak > 0.0F))
    {
        return 0.0F;
    }
    const auto ratio = omega / peak;
    const auto wind = std::max(state.wind_speed, 0.01F);
    auto exponent = ratio <= 1.0F ? 6.97F * std::pow(ratio, 4.06F) : 9.77F * std::pow(ratio, -2.33F - 1.45F * (wind * peak / state.gravity - 1.17F));
    // Horvath's swell: long waves (below the peak frequency's ratio) narrow toward parallel trains.
    exponent += 16.0F * std::tanh(peak / omega) * state.swell * state.swell;
    exponent = std::clamp(exponent, spread_exponent_min, spread_exponent_max);
    // Q(s) = 2^(2s - 1) / pi * Gamma(s + 1)^2 / Gamma(2s + 1), through logarithms (large s overflows).
    const auto log_normalization =
        (2.0F * exponent - 1.0F) * std::numbers::ln2_v<float> - std::log(std::numbers::pi_v<float>) + 2.0F * std::lgamma(exponent + 1.0F) - std::lgamma(2.0F * exponent + 1.0F);
    return std::exp(log_normalization) * std::pow(std::abs(std::cos(theta * 0.5F)), 2.0F * exponent);
}

std::vector<OceanSpectrumCell> ocean_initial_spectrum(const OceanSeaState& state, const OceanCascadeBand& band, const std::uint32_t size, const std::uint64_t seed)
{
    std::vector<OceanSpectrumCell> cells(static_cast<std::size_t>(size) * size);
    if (size == 0 || !(band.patch_size > 0.0F) || !(state.gravity > 0.0F))
    {
        return cells;
    }
    const auto step = 2.0F * std::numbers::pi_v<float> / band.patch_size;
    const auto half = static_cast<int>(size / 2U);
    // One draw per cell, so the amplitudes of k and -k are independent and both known.
    Pcg32 random(seed);
    std::vector<std::array<float, 2>> draws(cells.size());
    for (auto& draw : draws)
    {
        draw = {random.gaussian(), random.gaussian()};
    }
    // |h0(k)|: sqrt(2 S(omega) D(omega, theta) (d omega / d k) / k) dk, deep-water dispersion.
    const auto amplitude = [&](const int column, const int row)
    {
        const auto kx = static_cast<float>(column) * step;
        const auto ky = static_cast<float>(row) * step;
        const auto k = std::sqrt(kx * kx + ky * ky);
        if (!(k > 0.0F))
        {
            return 0.0F;
        }
        const auto wavelength = 2.0F * std::numbers::pi_v<float> / k;
        if (wavelength < band.min_wavelength || wavelength >= band.max_wavelength)
        {
            return 0.0F;
        }
        const auto omega = std::sqrt(state.gravity * k);
        const auto slope = state.gravity / (2.0F * omega);
        const auto theta = std::atan2(ky, kx) - state.wind_angle;
        const auto density = ocean_frequency_spectrum(omega, state) * ocean_directional_spread(omega, theta, state) * slope / k;
        return std::sqrt(2.0F * density) * step;
    };
    // E|h0|^2 = S_k dk^2 / 2 with complex unit draws (E|draw|^2 = 2), so the real field's variance,
    // the sum of |h0(k)|^2 + |h0(-k)|^2 over the patch, is the spectrum's integral.
    const auto amplitude_scale = 0.5F / std::numbers::sqrt2_v<float>;
    const auto index_of = [&](const int column, const int row)
    {
        return static_cast<std::size_t>(row + half) * size + static_cast<std::size_t>(column + half);
    };
    for (int row = -half; row < half; ++row)
    {
        for (int column = -half; column < half; ++column)
        {
            auto& cell = cells[index_of(column, row)];
            const auto& draw = draws[index_of(column, row)];
            const auto here = amplitude(column, row) * amplitude_scale;
            cell.real = draw[0] * here;
            cell.imaginary = draw[1] * here;
            // -k wraps on the periodic patch (the most negative wave number is its own opposite).
            const auto mirror_column = column == -half ? -half : -column;
            const auto mirror_row = row == -half ? -half : -row;
            const auto& mirror_draw = draws[index_of(mirror_column, mirror_row)];
            const auto there = amplitude(mirror_column, mirror_row) * amplitude_scale;
            cell.minus_real = mirror_draw[0] * there;
            cell.minus_imaginary = -mirror_draw[1] * there;
        }
    }
    return cells;
}

} // namespace eaw
