#pragma once

// Graphics-API-independent building blocks for GPU resource lifetime: generation-checked handles,
// a descriptor-slot allocator whose frees are deferred by fence value, and a fence-keyed
// retirement queue. They contain no Direct3D types so the rules are unit-testable without a device.
//
// Fence values are those of one queue timeline; callers must not mix timelines in one instance.

#include "eaw/fatal_error.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eaw {

// A reference to a pool entry. Generation 0 is never issued, so a default handle is invalid, and a
// handle whose entry was erased stops resolving instead of aliasing the slot's next occupant.
template <class Tag> struct ResourceHandle
{
    std::uint32_t index{};
    std::uint32_t generation{};

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return generation != 0;
    }
    friend constexpr bool operator==(const ResourceHandle&, const ResourceHandle&) = default;
};

template <class Tag, class T> class HandlePool final
{
public:
    using Handle = ResourceHandle<Tag>;

    [[nodiscard]] Handle insert(T value)
    {
        std::uint32_t index{};
        if (!free_.empty())
        {
            index = free_.back();
            free_.pop_back();
        }
        else
        {
            EAW_ASSERT_MSG(slots_.size() < std::numeric_limits<std::uint32_t>::max(), "resource handle pool is full");
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
        }
        auto& slot = slots_[index];
        slot.value.emplace(std::move(value));
        ++live_;
        return {index, slot.generation};
    }

    [[nodiscard]] T* get(const Handle handle) noexcept
    {
        return contains(handle) ? &*slots_[handle.index].value : nullptr;
    }

    [[nodiscard]] const T* get(const Handle handle) const noexcept
    {
        return contains(handle) ? &*slots_[handle.index].value : nullptr;
    }

    [[nodiscard]] bool contains(const Handle handle) const noexcept
    {
        return handle.valid() && handle.index < slots_.size() && slots_[handle.index].generation == handle.generation && slots_[handle.index].value.has_value();
    }

    // Removes and returns the entry; stale or invalid handles return nothing. The slot's generation
    // advances so outstanding copies of the handle stop resolving. A slot whose generation would
    // wrap is retired permanently rather than reissued.
    std::optional<T> erase(const Handle handle)
    {
        if (!contains(handle))
        {
            return std::nullopt;
        }
        auto& slot = slots_[handle.index];
        std::optional<T> result(std::move(slot.value));
        slot.value.reset();
        --live_;
        if (slot.generation != std::numeric_limits<std::uint32_t>::max())
        {
            ++slot.generation;
            free_.push_back(handle.index);
        }
        return result;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return live_;
    }

private:
    struct Slot
    {
        std::uint32_t generation{1};
        std::optional<T> value;
    };
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
    std::size_t live_{};
};

// Hands out descriptor slots in [first, first + count). A released slot stays reserved until the
// fence value passed to release() has completed, because in-flight command lists may still read
// the descriptor; reclaim() returns slots to the free list and reports them so the caller can
// overwrite them with a null view.
class DescriptorSlotAllocator final
{
public:
    DescriptorSlotAllocator() = default;
    DescriptorSlotAllocator(const std::uint32_t first, const std::uint32_t count) : first_(first), count_(count), state_(count, SlotState::free)
    {
        EAW_ASSERT_MSG(static_cast<std::uint64_t>(first) + count <= std::numeric_limits<std::uint32_t>::max(), "descriptor slot range overflows");
        free_.reserve(count);
        for (std::uint32_t offset = count; offset > 0; --offset)
        {
            free_.push_back(first + offset - 1U);
        }
    }

    // Lowest-numbered free slot first; nothing when every slot is allocated or awaiting its fence.
    [[nodiscard]] std::optional<std::uint32_t> allocate()
    {
        if (free_.empty())
        {
            return std::nullopt;
        }
        const auto slot = free_.back();
        free_.pop_back();
        state_[slot - first_] = SlotState::allocated;
        return slot;
    }

    // Whether release(slot, fence_value) is allowed: the slot is allocated and the fence value does
    // not precede an earlier release.
    [[nodiscard]] bool can_release(const std::uint32_t slot, const std::uint64_t fence_value) const noexcept
    {
        return slot >= first_ && slot - first_ < count_ && state_[slot - first_] == SlotState::allocated && (pending_.empty() || fence_value >= pending_.back().fence_value);
    }

    // Releasing a slot that is not allocated, or out of fence order, is a programming error.
    void release(const std::uint32_t slot, const std::uint64_t fence_value)
    {
        EAW_ASSERT_MSG(slot >= first_ && slot - first_ < count_ && state_[slot - first_] == SlotState::allocated, "descriptor slot " + std::to_string(slot) + " released while not allocated");
        EAW_ASSERT_MSG(pending_.empty() || fence_value >= pending_.back().fence_value, "descriptor slot releases must use non-decreasing fence values");
        state_[slot - first_] = SlotState::pending;
        pending_.push_back({slot, fence_value});
    }

    // Returns the slots whose release fence has completed; they are free again on return.
    std::vector<std::uint32_t> reclaim(const std::uint64_t completed_fence_value)
    {
        std::vector<std::uint32_t> reclaimed;
        while (!pending_.empty() && pending_.front().fence_value <= completed_fence_value)
        {
            reclaimed.push_back(pending_.front().slot);
            pending_.pop_front();
        }
        for (const auto slot : reclaimed)
        {
            state_[slot - first_] = SlotState::free;
            free_.push_back(slot);
        }
        // Keep handing out low slots first so the shader table's used range stays compact.
        std::ranges::sort(free_, std::greater<>{});
        return reclaimed;
    }

    [[nodiscard]] std::uint32_t capacity() const noexcept
    {
        return count_;
    }
    [[nodiscard]] std::size_t free_count() const noexcept
    {
        return free_.size();
    }
    [[nodiscard]] std::size_t pending_count() const noexcept
    {
        return pending_.size();
    }

private:
    enum class SlotState : std::uint8_t
    {
        free,
        allocated,
        pending,
    };
    struct PendingSlot
    {
        std::uint32_t slot{};
        std::uint64_t fence_value{};
    };
    std::uint32_t first_{};
    std::uint32_t count_{};
    std::vector<SlotState> state_;
    std::vector<std::uint32_t> free_;
    std::deque<PendingSlot> pending_;
};

// Keeps objects alive until a fence value completes. Values must be pushed in non-decreasing fence
// order (one queue timeline), which keeps retirement a pop from the front.
template <class T> class FenceRetirementQueue final
{
public:
    // Whether push(value, fence_value) keeps the queue in fence order.
    [[nodiscard]] bool can_push(const std::uint64_t fence_value) const noexcept
    {
        return entries_.empty() || fence_value >= entries_.back().fence_value;
    }

    // Pushing out of fence order is a programming error.
    void push(T value, const std::uint64_t fence_value)
    {
        EAW_ASSERT_MSG(can_push(fence_value), "retirement fence values must be non-decreasing");
        entries_.push_back({std::move(value), fence_value});
    }

    // Destroys every entry whose fence has completed and returns how many were released.
    std::size_t retire(const std::uint64_t completed_fence_value)
    {
        std::size_t released = 0;
        while (!entries_.empty() && entries_.front().fence_value <= completed_fence_value)
        {
            entries_.pop_front();
            ++released;
        }
        return released;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return entries_.size();
    }

private:
    struct Entry
    {
        T value;
        std::uint64_t fence_value{};
    };
    std::deque<Entry> entries_;
};

} // namespace eaw
