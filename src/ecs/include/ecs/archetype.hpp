#pragma once
// The entities that have one exact set of components (Singe's ecs::Archetype): rows in chunks of
// about chunk_bytes, each chunk an array of entities and one array per component, so a view walks
// each component's values in a row. Row r lives in chunk r / capacity at element r % capacity.
// Removing a row moves the last row into its place, so row order depends only on the history of
// adds and removes.
#include "ecs/component.hpp"
#include "ecs/entity.hpp"
#include "rti/variant.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace eaw::ecs {

// Component values given for an entity before it is stored (the rest are default-constructed).
using PendingComponents = std::vector<std::pair<ComponentId, rti::Variant>>;

class Archetype final
{
public:
    // A chunk's size to aim for (Singe's; an engine choice): rows per chunk is this over a row's size.
    static constexpr std::size_t chunk_bytes = 16 * 1024;

    explicit Archetype(const ComponentMask& mask);
    ~Archetype();
    Archetype(const Archetype&) = delete;
    Archetype& operator=(const Archetype&) = delete;

    [[nodiscard]] const ComponentMask& mask() const noexcept
    {
        return mask_;
    }
    [[nodiscard]] std::size_t size() const noexcept
    {
        return size_;
    }
    [[nodiscard]] bool empty() const noexcept
    {
        return size_ == 0;
    }
    // The column holding a component, or none when this archetype lacks it.
    [[nodiscard]] std::optional<std::size_t> column(ComponentId id) const noexcept;
    [[nodiscard]] Entity entity(std::size_t row) const noexcept;
    [[nodiscard]] void* component(std::size_t column, std::size_t row) noexcept;
    template <class T> [[nodiscard]] T& get(const std::size_t column, const std::size_t row) noexcept
    {
        return *static_cast<T*>(component(column, row));
    }

    // Appends a row for `entity`, each component from `values` (taken by id) or default. Returns
    // the row.
    std::size_t push(Entity entity, PendingComponents& values);
    // Removes a row, destroying its components; the last row takes its place. Returns the entity
    // that moved into `row`, if one did.
    std::optional<Entity> erase(std::size_t row);
    // Moves the entity of `row` into a new row of `to`: components both archetypes have move, those
    // only `to` has come from `values` or default, those only `from` has are destroyed; the row then
    // leaves `from` as by erase (`moved`: the entity that took its place). Returns its row in `to`.
    static std::size_t transfer(Archetype& from, std::size_t row, Archetype& to, PendingComponents& values, std::optional<Entity>& moved);

private:
    struct Column
    {
        ComponentId id{};
        const ComponentType* type{};
        // Where the column's array starts in a chunk.
        std::size_t offset{};
    };
    struct ChunkDeleter
    {
        std::size_t alignment{};
        void operator()(std::byte* memory) const noexcept;
    };
    using Chunk = std::unique_ptr<std::byte[], ChunkDeleter>;

    [[nodiscard]] std::byte* chunk_of(std::size_t row) const noexcept;
    [[nodiscard]] Entity& entity_at(std::size_t row) const noexcept;
    [[nodiscard]] void* value_at(const Column& column, std::size_t row) const noexcept;
    // Makes room for one more row.
    void reserve_row();
    // Fills `row` (whose components are gone) with the last row, and drops the last row.
    std::optional<Entity> close_gap(std::size_t row);
    void construct(const Column& column, std::size_t row, PendingComponents& values);

    ComponentMask mask_;
    // By component id.
    std::vector<Column> columns_;
    std::size_t capacity_{};
    std::size_t chunk_size_{};
    std::size_t chunk_alignment_{};
    std::vector<Chunk> chunks_;
    std::size_t size_{};
};

} // namespace eaw::ecs
