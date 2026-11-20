#pragma once
// Where entities live: a slot per entity (Entity::index) with its generation and its place (an
// archetype and a row), and the archetypes in the order they were made, which is the order views
// walk them. Slots are reused last freed first. Singe kept an entity's place in a hash map per
// archetype; a slot table keeps lookups and every order deterministic.
#include "ecs/archetype.hpp"
#include "ecs/component.hpp"
#include "ecs/entity.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace eaw::ecs {

struct Location
{
    static constexpr std::uint32_t nowhere = std::numeric_limits<std::uint32_t>::max();
    // The archetype's index, or nowhere for an entity made but not stored yet.
    std::uint32_t archetype{nowhere};
    std::uint32_t row{};

    [[nodiscard]] bool stored() const noexcept
    {
        return archetype != nowhere;
    }
};

class EntityTable final
{
public:
    // A new entity in a free slot (its next generation) or a new slot, not stored anywhere yet.
    [[nodiscard]] Entity create();
    // Frees the entity's slot; its handle names nothing afterwards.
    void release(Entity entity);
    [[nodiscard]] bool alive(Entity entity) const noexcept;
    // The entity's place, or nullptr when it is not alive.
    [[nodiscard]] const Location* location(Entity entity) const noexcept;
    void place(Entity entity, Location location) noexcept;
    // The slots there are (alive or free), for walking entities in slot order.
    [[nodiscard]] std::size_t slot_count() const noexcept
    {
        return generations_.size();
    }
    // The entity in a slot, or none when the slot is free.
    [[nodiscard]] std::optional<Entity> entity_in(std::size_t slot) const noexcept;

    [[nodiscard]] std::size_t archetype_count() const noexcept
    {
        return archetypes_.size();
    }
    [[nodiscard]] Archetype& archetype(std::size_t index) const noexcept
    {
        return *archetypes_[index];
    }
    // The index of the archetype with exactly this mask, or none.
    [[nodiscard]] std::optional<std::size_t> find_archetype(const ComponentMask& mask) const noexcept;
    // Makes the archetype of this mask (which must be new); returns its index.
    std::size_t add_archetype(const ComponentMask& mask);

private:
    // By slot: the generation of its entity (alive or the next one), its place, and whether it holds
    // an entity.
    std::vector<std::uint32_t> generations_;
    std::vector<Location> locations_;
    std::vector<std::uint8_t> used_;
    std::vector<std::uint32_t> free_;
    std::vector<std::unique_ptr<Archetype>> archetypes_;
};

} // namespace eaw::ecs
