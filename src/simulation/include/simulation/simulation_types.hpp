#pragma once

// Basic value types shared by the simulation's headers (positions are eaw_math vectors).

#include "math/vector.hpp"

#include <cstdint>

namespace eaw {

// Generation-checked entity reference. Generation 0 is never issued, so a default id is invalid and a
// destroyed entity's id never aliases a later entity in the same slot.
struct EntityId
{
    std::uint32_t index{};
    std::uint32_t generation{};

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return generation != 0;
    }
    friend constexpr bool operator==(const EntityId&, const EntityId&) = default;
};

} // namespace eaw
