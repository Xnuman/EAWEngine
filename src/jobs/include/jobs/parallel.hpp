#pragma once

// Deterministic parallel loops over the job system (docs/ecs-design.md, 6.9 and 7.3): chunk
// boundaries come from the item count and a constant grain only, never the number of workers, so a
// loop whose chunks write only their own outputs, merged in chunk order, gives the same result on
// any machine and with any worker count.

#include "core/fatal_error.hpp"
#include "jobs/job_system.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace eaw {

// The chunks `count` items make in `grain`-sized pieces (the last one may be shorter).
[[nodiscard]] constexpr std::size_t chunk_count(const std::size_t count, const std::size_t grain) noexcept
{
    return grain == 0 ? 0 : (count + grain - 1) / grain;
}

// The most helper jobs one parallel_for queues (more threads than this help no single loop; an
// engine choice).
inline constexpr std::size_t max_parallel_helpers = 64;

// Calls function(begin, end, chunk) for every chunk of [0, count) in `grain`-sized pieces, chunk k
// covering [k * grain, min((k + 1) * grain, count)), on the job system, and returns when all are
// done. Give each chunk its own output (indexed by `chunk`) and merge the outputs in chunk order
// afterwards.
//
// How it runs (which thread runs a chunk never decides a result): one chunk runs on the caller;
// otherwise a helper job per worker (at most one per chunk beyond the caller's first) and the caller
// itself take chunks in turn from a shared index until none is left, and the caller waits for the
// helpers still finishing theirs. Queuing a job per chunk cost a queue operation and a wake-up per
// chunk, and left the caller parked instead of working (the jobs-loops benchmark).
template <typename Function> void parallel_for(JobSystem& jobs, const std::size_t count, const std::size_t grain, Function&& function, const JobPriority priority = JobPriority::simulation)
{
    EAW_ASSERT_MSG(grain > 0, "parallel_for needs a grain of at least one item");
    const auto chunks = chunk_count(count, grain);
    if (chunks == 0)
    {
        return;
    }
    EAW_ASSERT_MSG(chunks <= std::numeric_limits<std::uint32_t>::max(), "parallel_for has more chunks than a job index holds");
    if (chunks == 1)
    {
        function(std::size_t{0}, count, std::size_t{0});
        return;
    }
    struct Loop
    {
        std::remove_reference_t<Function>* function;
        std::size_t count;
        std::size_t grain;
        std::size_t chunks;
        std::atomic<std::size_t> next{0};

        void take_chunks()
        {
            for (auto chunk = next.fetch_add(1, std::memory_order_relaxed); chunk < chunks; chunk = next.fetch_add(1, std::memory_order_relaxed))
            {
                const auto begin = chunk * grain;
                (*function)(begin, std::min(begin + grain, count), chunk);
            }
        }
    };
    Loop loop{&function, count, grain, chunks};
    const auto helpers = std::min({chunks - 1, static_cast<std::size_t>(jobs.worker_count()), max_parallel_helpers});
    std::array<Job, max_parallel_helpers> list{};
    for (std::size_t helper = 0; helper < helpers; ++helper)
    {
        list[helper] = {[](void* argument, std::uint32_t)
                        {
                            static_cast<Loop*>(argument)->take_chunks();
                        },
                        &loop, static_cast<std::uint32_t>(helper)};
    }
    JobCounter counter;
    jobs.run(std::span<const Job>(list.data(), helpers), priority, counter);
    loop.take_chunks();
    jobs.wait(counter);
}

} // namespace eaw
