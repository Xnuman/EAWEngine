#include "core/random.hpp"

namespace eaw {

Pcg32::Pcg32(const std::uint64_t seed) noexcept
{
    increment_ = (seed << 1U) | 1U;
    (void)next();
    state_ += 0x853c49e6748fea9bULL ^ seed;
    (void)next();
}

std::uint32_t Pcg32::next() noexcept
{
    const auto old = state_;
    state_ = old * 6364136223846793005ULL + increment_;
    const auto xorshifted = static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
    const auto rotation = static_cast<std::uint32_t>(old >> 59U);
    return (xorshifted >> rotation) | (xorshifted << ((32U - rotation) & 31U));
}

float Pcg32::unit() noexcept
{
    return static_cast<float>(next() >> 8U) * (1.0F / 16777216.0F);
}

float Pcg32::range(const float low, const float high) noexcept
{
    return low + (high - low) * unit();
}

std::uint32_t Pcg32::below(const std::uint32_t count) noexcept
{
    return count == 0 ? 0U : static_cast<std::uint32_t>((static_cast<std::uint64_t>(next()) * count) >> 32U);
}

} // namespace eaw
