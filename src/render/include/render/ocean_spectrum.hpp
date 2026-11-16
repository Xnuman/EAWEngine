#pragma once

// The open sea's wave spectrum for the FFT ocean (docs/water-research.md, "The ocean plan"): the
// JONSWAP frequency spectrum limited by depth (TMA) and spread around the wind by Hasselmann's
// directional function with an optional swell term, as Horvath's "Empirical directional wave
// spectra for computer graphics" (DigiPro 2015) gives them. A cascade is one periodic patch of
// size x size cells; each keeps only the wavelengths of its band, so the cascades add up without
// counting a wave twice. Units are the world's (world units, seconds); the sea state's values are
// engine choices for the owner to tune. Graphics-free: the renderer uploads the result.

#include <cstdint>
#include <vector>

namespace eaw {

// The resolution of a cascade's patch (cells per side), a power of two for the FFT.
inline constexpr std::uint32_t ocean_cascade_size = 256;

struct OceanSeaState
{
    // Wind speed (world units a second) and the direction it blows toward (radians,
    // counter-clockwise from +X).
    float wind_speed{32.0F};
    float wind_angle{0.0F};
    // Open water upwind (world units): longer fetch, longer and higher waves. With this wind and
    // gravity the peak wavelength is about 300 units and the significant wave height about 7, close
    // to the Gerstner waves it replaces.
    float fetch{600000.0F};
    // Water depth (world units) for TMA's limit on long waves in shallow water.
    float depth{2000.0F};
    // 0..1: long waves gather into parallel trains (Horvath's swell parameter).
    float swell{0.0F};
    // The engine's gravity for water waves (world units a second squared; the ocean's Gerstner waves
    // used the same).
    float gravity{30.0F};
};

// The wavelengths one cascade keeps: at least `min_wavelength`, below `max_wavelength`, on a patch of
// `patch_size` world units.
struct OceanCascadeBand
{
    float patch_size{};
    float min_wavelength{};
    float max_wavelength{};
};

// One cell of a cascade's starting spectrum: the complex amplitude h0(k) and the conjugate of
// h0(-k), so the GPU's time evolution h0(k) e^(i w t) + conj(h0(-k)) e^(-i w t) gives a real height.
struct OceanSpectrumCell
{
    float real{};
    float imaginary{};
    float minus_real{};
    float minus_imaginary{};
};

// The JONSWAP spectrum S(omega) of the sea state (variance per unit of angular frequency), limited by
// depth (TMA); 0 for omega <= 0.
[[nodiscard]] float ocean_frequency_spectrum(float omega, const OceanSeaState& state) noexcept;
// The sea state's peak angular frequency.
[[nodiscard]] float ocean_peak_frequency(const OceanSeaState& state) noexcept;
// Hasselmann's directional spreading D(omega, theta), theta measured from the wind; it integrates to 1
// over theta in [-pi, pi].
[[nodiscard]] float ocean_directional_spread(float omega, float theta, const OceanSeaState& state) noexcept;
// A cascade's starting spectrum, row-major from wave number -size/2 to size/2 - 1 on each axis
// (k = 2 pi n / patch_size); the same seed gives the same sea.
[[nodiscard]] std::vector<OceanSpectrumCell> ocean_initial_spectrum(const OceanSeaState& state, const OceanCascadeBand& band, std::uint32_t size, std::uint64_t seed);

} // namespace eaw
