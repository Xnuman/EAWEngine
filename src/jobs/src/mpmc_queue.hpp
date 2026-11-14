#pragma once

// A bounded queue many threads push to and pop from without locks (Dmitry Vyukov's bounded MPMC
// queue): each cell carries a sequence number telling whether it is free for the producer of a
// given turn or full for its consumer, so a push or pop is one compare-and-swap on the shared
// position plus a store, with no ABA problem. Values are copied in and out; the capacity is a power
// of two fixed at construction, and a full queue refuses the push (the caller decides what to do).

#include "core/fatal_error.hpp"

#include <atomic>
#include <cstddef>
#include <memory>

namespace eaw::jobs_detail {

// The producers' and consumers' positions are kept a cache line apart (no false sharing).
inline constexpr std::size_t cache_line_bytes = 64;

template <typename T> class MpmcQueue final
{
public:
    explicit MpmcQueue(const std::size_t capacity) : mask_(round_up(capacity) - 1), cells_(std::make_unique<Cell[]>(mask_ + 1))
    {
        for (std::size_t index = 0; index <= mask_; ++index)
        {
            cells_[index].sequence.store(index, std::memory_order_relaxed);
        }
    }
    MpmcQueue(const MpmcQueue&) = delete;
    MpmcQueue& operator=(const MpmcQueue&) = delete;

    [[nodiscard]] bool push(const T& value) noexcept
    {
        auto position = enqueue_.load(std::memory_order_relaxed);
        for (;;)
        {
            auto& cell = cells_[position & mask_];
            const auto sequence = cell.sequence.load(std::memory_order_acquire);
            const auto difference = static_cast<std::ptrdiff_t>(sequence) - static_cast<std::ptrdiff_t>(position);
            if (difference == 0)
            {
                if (enqueue_.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
                {
                    cell.value = value;
                    cell.sequence.store(position + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (difference < 0)
            {
                return false;
            }
            else
            {
                position = enqueue_.load(std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] bool pop(T& value) noexcept
    {
        auto position = dequeue_.load(std::memory_order_relaxed);
        for (;;)
        {
            auto& cell = cells_[position & mask_];
            const auto sequence = cell.sequence.load(std::memory_order_acquire);
            const auto difference = static_cast<std::ptrdiff_t>(sequence) - static_cast<std::ptrdiff_t>(position + 1);
            if (difference == 0)
            {
                if (dequeue_.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
                {
                    value = cell.value;
                    cell.sequence.store(position + mask_ + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (difference < 0)
            {
                return false;
            }
            else
            {
                position = dequeue_.load(std::memory_order_relaxed);
            }
        }
    }

    // Whether the queue looks empty; a hint for idle workers, exact only when nothing runs.
    [[nodiscard]] bool looks_empty() const noexcept
    {
        return enqueue_.load(std::memory_order_acquire) == dequeue_.load(std::memory_order_acquire);
    }

private:
    struct Cell
    {
        std::atomic<std::size_t> sequence{};
        T value{};
    };

    static std::size_t round_up(const std::size_t capacity) noexcept
    {
        EAW_ASSERT_MSG(capacity > 0 && capacity <= (std::size_t{1} << 30U), "a job queue needs a capacity between 1 and 2^30");
        std::size_t size = 1;
        while (size < capacity)
        {
            size <<= 1U;
        }
        return size;
    }

    const std::size_t mask_;
    std::unique_ptr<Cell[]> cells_;
    // A cache line between the producers' and the consumers' positions (padding, not alignas, which
    // would make MSVC warn about the padded class).
    std::byte before_enqueue_[cache_line_bytes]{};
    std::atomic<std::size_t> enqueue_{0};
    std::byte between_[cache_line_bytes]{};
    std::atomic<std::size_t> dequeue_{0};
    std::byte after_dequeue_[cache_line_bytes]{};
};

} // namespace eaw::jobs_detail
