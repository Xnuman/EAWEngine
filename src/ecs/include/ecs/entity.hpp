#pragma once
// An entity (Singe's ecs::Entity, as a generational handle instead of a clock-based UUID, so a run
// makes the same entities every time): the slot it holds in its entity table, and that slot's
// generation when the entity was made. A slot is reused with the next generation, so a handle to a
// removed entity never names a later one. Generation 0 is no entity.
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace eaw::ecs {

struct Entity
{
    std::uint32_t index{};
    std::uint32_t generation{};

    [[nodiscard]] bool valid() const noexcept
    {
        return generation != 0;
    }
    friend bool operator==(Entity, Entity) noexcept = default;
    friend auto operator<=>(Entity, Entity) noexcept = default;
};

} // namespace eaw::ecs

template <> struct std::hash<eaw::ecs::Entity>
{
    std::size_t operator()(const eaw::ecs::Entity entity) const noexcept
    {
        return (static_cast<std::size_t>(entity.generation) << 32U) | entity.index;
    }
};
