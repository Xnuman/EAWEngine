#include <cstdint>
#include <compare>

struct EntityId
{
    std::uint32_t index{};
    std::uint32_t generation{};

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return generation != 0;
    }
    friend constexpr bool operator==(const EntityId&, const EntityId&) = default;
    // By index, then generation: the order deterministic sorts of ids use.
    friend constexpr auto operator<=>(const EntityId&, const EntityId&) = default;
};