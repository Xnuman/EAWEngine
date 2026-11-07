#pragma once

// Basic value types shared by the simulation's headers.

#include <cstdint>

namespace eaw {

struct SimVector3
{
    float x{};
    float y{};
    float z{};

    friend bool operator==(const SimVector3&, const SimVector3&) = default;
};

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
