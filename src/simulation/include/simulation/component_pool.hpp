#pragma once

// A pool of one optional component (docs/ecs-design.md, 5 option B and migration step 3): the
// component's columns for the entities that have it, packed, and a sparse index from an entity's
// slot (EntityId::index, stable for the entity's life) to its packed row. Queries walk the dense
// core table in its order and look an entity's row up by slot, so the packed order (it changes with
// swap-removal, in a way that depends only on the command history) never decides an iteration
// order.
//
// `Columns` is a component's column group: std::vector columns, all one row per member, and a
// for_each_column(visit) that lists them (as the core table's groups do).

#include "core/fatal_error.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace eaw {

template <class Columns> class ComponentPool final
{
public:
    // The member's packed row, or nothing when the entity of `slot` lacks the component.
    [[nodiscard]] std::optional<std::size_t> find(const std::uint32_t slot) const noexcept
    {
        if (slot >= rows_.size() || rows_[slot] == no_row)
        {
            return std::nullopt;
        }
        return rows_[slot];
    }

    // The member's value in `column` (a column of Columns), or nullptr when the entity of `slot`
    // lacks the component.
    template <class Value> [[nodiscard]] const Value* find(const std::uint32_t slot, std::vector<Value> Columns::* column) const noexcept
    {
        const auto row = find(slot);
        return row ? &(columns.*column)[*row] : nullptr;
    }
    template <class Value> [[nodiscard]] Value* find(const std::uint32_t slot, std::vector<Value> Columns::* column) noexcept
    {
        const auto row = find(slot);
        return row ? &(columns.*column)[*row] : nullptr;
    }
    // The same for a member of the pool (asserts that the entity has the component).
    template <class Value> [[nodiscard]] const Value& get(const std::uint32_t slot, std::vector<Value> Columns::* column) const
    {
        const auto* value = find(slot, column);
        EAW_ASSERT_MSG(value != nullptr, "the entity lacks the pooled component it is read for");
        return *value;
    }
    template <class Value> [[nodiscard]] Value& get(const std::uint32_t slot, std::vector<Value> Columns::* column)
    {
        auto* value = find(slot, column);
        EAW_ASSERT_MSG(value != nullptr, "the entity lacks the pooled component it is read for");
        return *value;
    }

    // Gives the entity of `slot` the component with its columns' default values; returns its row.
    std::size_t add(const std::uint32_t slot)
    {
        EAW_ASSERT_MSG(!find(slot), "an entity can have a component once");
        if (slot >= rows_.size())
        {
            rows_.resize(static_cast<std::size_t>(slot) + 1U, no_row);
        }
        const auto row = slots_.size();
        EAW_ASSERT_MSG(row < no_row, "component pool is full");
        rows_[slot] = static_cast<std::uint32_t>(row);
        slots_.push_back(slot);
        columns.for_each_column(
            [](auto& column)
            {
                column.emplace_back();
            });
        return row;
    }

    // Takes the component from the entity of `slot`, if it has it: the last row fills its place.
    void remove(const std::uint32_t slot)
    {
        const auto found = find(slot);
        if (!found)
        {
            return;
        }
        const auto row = *found;
        const auto last = slots_.size() - 1U;
        if (row != last)
        {
            columns.for_each_column(
                [&](auto& column)
                {
                    column[row] = std::move(column[last]);
                });
            slots_[row] = slots_[last];
            rows_[slots_[row]] = static_cast<std::uint32_t>(row);
        }
        columns.for_each_column(
            [](auto& column)
            {
                column.pop_back();
            });
        slots_.pop_back();
        rows_[slot] = no_row;
    }

    // Members in packed order: the slot of each row.
    [[nodiscard]] std::span<const std::uint32_t> slots() const noexcept
    {
        return slots_;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return slots_.size();
    }

    bool operator==(const ComponentPool&) const = default;

    // The packed columns, one row per member (rows from find or add).
    Columns columns;

private:
    static constexpr std::uint32_t no_row = std::numeric_limits<std::uint32_t>::max();
    // Packed row by slot (no_row: the entity lacks the component), and slot by packed row.
    std::vector<std::uint32_t> rows_;
    std::vector<std::uint32_t> slots_;
};

} // namespace eaw
