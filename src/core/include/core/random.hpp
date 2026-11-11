#pragma once

// PCG32 (O'Neill): a small, fast, fully specified random stream, so a seed replays the same
// sequence everywhere. Presentation (particles, sound choices) uses it; the simulation and scripts
// keep their own streams, which are part of their state.

#include <cstdint>

namespace eaw {

class Pcg32 final
{
public:
    Pcg32() noexcept : Pcg32(0)
    {}
    explicit Pcg32(std::uint64_t seed) noexcept;

    std::uint32_t next() noexcept;
    // Uniform in [0, 1).
    float unit() noexcept;
    float range(float low, float high) noexcept;
    // Uniform in [0, count); 0 when count is 0.
    std::uint32_t below(std::uint32_t count) noexcept;
    // The stream position, for state hashes.
    [[nodiscard]] std::uint64_t state() const noexcept
    {
        return state_;
    }

private:
    std::uint64_t state_{};
    std::uint64_t increment_{};
};

} // namespace eaw
