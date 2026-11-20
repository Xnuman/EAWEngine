#include "ecs/archetype.hpp"

#include "core/fatal_error.hpp"

#include <algorithm>
#include <new>

namespace eaw::ecs {

namespace {

constexpr std::size_t align_up(const std::size_t value, const std::size_t alignment) noexcept
{
    return (value + alignment - 1) / alignment * alignment;
}

// The given value of a component, taken out of `values`, or nullptr.
rti::Variant* given_value(PendingComponents& values, const ComponentId id)
{
    const auto found = std::ranges::find(values, id, &std::pair<ComponentId, rti::Variant>::first);
    return found != values.end() && found->second ? &found->second : nullptr;
}

} // namespace

void Archetype::ChunkDeleter::operator()(std::byte* memory) const noexcept
{
    ::operator delete(memory, std::align_val_t{alignment});
}

Archetype::Archetype(const ComponentMask& mask) : mask_(mask)
{
    auto row_size = sizeof(Entity);
    chunk_alignment_ = std::max(alignof(Entity), alignof(std::max_align_t));
    for (std::size_t id = 0; id < max_components; ++id)
    {
        if (mask.test(id))
        {
            const auto& type = component_type(static_cast<ComponentId>(id));
            columns_.push_back({static_cast<ComponentId>(id), &type, 0});
            row_size += type.size;
            chunk_alignment_ = std::max(chunk_alignment_, type.alignment);
        }
    }
    capacity_ = std::max<std::size_t>(1, chunk_bytes / row_size);
    auto offset = sizeof(Entity) * capacity_;
    for (auto& column : columns_)
    {
        offset = align_up(offset, column.type->alignment);
        column.offset = offset;
        offset += column.type->size * capacity_;
    }
    chunk_size_ = align_up(offset, chunk_alignment_);
}

Archetype::~Archetype()
{
    for (std::size_t row = 0; row < size_; ++row)
    {
        for (const auto& column : columns_)
        {
            column.type->destroy(value_at(column, row));
        }
    }
}

std::optional<std::size_t> Archetype::column(const ComponentId id) const noexcept
{
    const auto found = std::ranges::lower_bound(columns_, id, {}, &Column::id);
    return found != columns_.end() && found->id == id ? std::optional(static_cast<std::size_t>(found - columns_.begin())) : std::nullopt;
}

Entity Archetype::entity(const std::size_t row) const noexcept
{
    return entity_at(row);
}

void* Archetype::component(const std::size_t column, const std::size_t row) noexcept
{
    return value_at(columns_[column], row);
}

std::size_t Archetype::push(const Entity entity, PendingComponents& values)
{
    reserve_row();
    const auto row = size_;
    new (&entity_at(row)) Entity(entity);
    for (const auto& column : columns_)
    {
        construct(column, row, values);
    }
    ++size_;
    return row;
}

std::optional<Entity> Archetype::erase(const std::size_t row)
{
    EAW_ASSERT_MSG(row < size_, "an archetype erases a row it has");
    for (const auto& column : columns_)
    {
        column.type->destroy(value_at(column, row));
    }
    return close_gap(row);
}

std::size_t Archetype::transfer(Archetype& from, const std::size_t row, Archetype& to, PendingComponents& values, std::optional<Entity>& moved)
{
    EAW_ASSERT_MSG(&from != &to && row < from.size_, "an entity moves between two archetypes");
    to.reserve_row();
    const auto target = to.size_;
    new (&to.entity_at(target)) Entity(from.entity_at(row));
    for (const auto& column : to.columns_)
    {
        if (const auto source = from.column(column.id))
        {
            column.type->relocate(to.value_at(column, target), from.value_at(from.columns_[*source], row));
        }
        else
        {
            to.construct(column, target, values);
        }
    }
    for (const auto& column : from.columns_)
    {
        if (!to.column(column.id))
        {
            column.type->destroy(from.value_at(column, row));
        }
    }
    ++to.size_;
    moved = from.close_gap(row);
    return target;
}

std::byte* Archetype::chunk_of(const std::size_t row) const noexcept
{
    return chunks_[row / capacity_].get();
}

Entity& Archetype::entity_at(const std::size_t row) const noexcept
{
    return reinterpret_cast<Entity*>(chunk_of(row))[row % capacity_];
}

void* Archetype::value_at(const Column& column, const std::size_t row) const noexcept
{
    return chunk_of(row) + column.offset + column.type->size * (row % capacity_);
}

void Archetype::reserve_row()
{
    if (size_ == chunks_.size() * capacity_)
    {
        auto* memory = static_cast<std::byte*>(::operator new(chunk_size_, std::align_val_t{chunk_alignment_}));
        chunks_.emplace_back(memory, ChunkDeleter{chunk_alignment_});
    }
}

std::optional<Entity> Archetype::close_gap(const std::size_t row)
{
    const auto last = size_ - 1;
    std::optional<Entity> moved;
    if (row != last)
    {
        for (const auto& column : columns_)
        {
            column.type->relocate(value_at(column, row), value_at(column, last));
        }
        entity_at(row) = entity_at(last);
        moved = entity_at(row);
    }
    --size_;
    // shortcut: chunks stay allocated when rows leave; free trailing empty ones if memory shows.
    return moved;
}

void Archetype::construct(const Column& column, const std::size_t row, PendingComponents& values)
{
    if (auto* value = given_value(values, column.id))
    {
        column.type->construct_from(value_at(column, row), std::move(*value));
        *value = rti::Variant{};
    }
    else
    {
        column.type->construct(value_at(column, row));
    }
}

} // namespace eaw::ecs
