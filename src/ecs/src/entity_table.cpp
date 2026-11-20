#include "ecs/entity_table.hpp"

#include "core/fatal_error.hpp"

#include <algorithm>

namespace eaw::ecs {

Entity EntityTable::create()
{
    std::uint32_t slot = 0;
    if (!free_.empty())
    {
        slot = free_.back();
        free_.pop_back();
    }
    else
    {
        EAW_ASSERT_MSG(generations_.size() < std::numeric_limits<std::uint32_t>::max(), "the entity table is full");
        slot = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(1);
        locations_.emplace_back();
        used_.push_back(0);
    }
    used_[slot] = 1;
    locations_[slot] = {};
    return {slot, generations_[slot]};
}

void EntityTable::release(const Entity entity)
{
    EAW_ASSERT_MSG(alive(entity), "the entity table releases an entity it holds");
    used_[entity.index] = 0;
    locations_[entity.index] = {};
    // The next generation, never 0 (no entity).
    generations_[entity.index] = generations_[entity.index] == std::numeric_limits<std::uint32_t>::max() ? 1 : generations_[entity.index] + 1;
    free_.push_back(entity.index);
}

bool EntityTable::alive(const Entity entity) const noexcept
{
    return entity.index < generations_.size() && used_[entity.index] != 0 && generations_[entity.index] == entity.generation;
}

const Location* EntityTable::location(const Entity entity) const noexcept
{
    return alive(entity) ? &locations_[entity.index] : nullptr;
}

void EntityTable::place(const Entity entity, const Location location) noexcept
{
    locations_[entity.index] = location;
}

std::optional<Entity> EntityTable::entity_in(const std::size_t slot) const noexcept
{
    return slot < generations_.size() && used_[slot] != 0 ? std::optional(Entity{static_cast<std::uint32_t>(slot), generations_[slot]}) : std::nullopt;
}

std::optional<std::size_t> EntityTable::find_archetype(const ComponentMask& mask) const noexcept
{
    const auto found = std::ranges::find_if(archetypes_,
                                            [&](const std::unique_ptr<Archetype>& archetype)
                                            {
                                                return archetype->mask() == mask;
                                            });
    return found != archetypes_.end() ? std::optional(static_cast<std::size_t>(found - archetypes_.begin())) : std::nullopt;
}

std::size_t EntityTable::add_archetype(const ComponentMask& mask)
{
    EAW_ASSERT_MSG(!find_archetype(mask), "an archetype is made once per mask");
    archetypes_.push_back(std::make_unique<Archetype>(mask));
    return archetypes_.size() - 1;
}

} // namespace eaw::ecs
