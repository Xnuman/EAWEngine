#pragma once

// Deterministic parallel loops over the job system (docs/ecs-design.md, 6.9 and 7.3): chunk
// boundaries come from the item count and a constant grain only, never the number of workers, so a
// loop whose chunks write only their own outputs, merged in chunk order, gives the same result on
// any machine and with any worker count.

#include "core/fatal_error.hpp"
#include "jobs/job_system.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace eaw {

// The chunks `count` items make in `grain`-sized pieces (the last one may be shorter).
[[nodiscard]] constexpr std::size_t chunk_count(const std::size_t count, const std::size_t grain) noexcept
{
    return grain == 0 ? 0 : (count + grain - 1) / grain;
}

// Calls function(begin, end, chunk) for every chunk of [0, count) in `grain`-sized pieces, chunk k
// covering [k * grain, min((k + 1) * grain, count)), on the job system, and returns when all are
// done (the calling fiber runs jobs meanwhile). Give each chunk its own output (indexed by `chunk`)
// and merge the outputs in chunk order afterwards.
template <typename Function> void parallel_for(JobSystem& jobs, const std::size_t count, const std::size_t grain, Function&& function, const JobPriority priority = JobPriority::simulation)
{
    EAW_ASSERT_MSG(grain > 0, "parallel_for needs a grain of at least one item");
    const auto chunks = chunk_count(count, grain);
    if (chunks == 0)
    {
        return;
    }
    EAW_ASSERT_MSG(chunks <= std::numeric_limits<std::uint32_t>::max(), "parallel_for has more chunks than a job index holds");
    struct Context
    {
        std::remove_reference_t<Function>* function;
        std::size_t count;
        std::size_t grain;
    };
    Context context{&function, count, grain};
    std::vector<Job> list(chunks);
    for (std::size_t chunk = 0; chunk < chunks; ++chunk)
    {
        list[chunk] = {[](void* argument, const std::uint32_t index)
                       {
                           const auto& loop = *static_cast<const Context*>(argument);
                           const auto begin = static_cast<std::size_t>(index) * loop.grain;
                           (*loop.function)(begin, std::min(begin + loop.grain, loop.count), static_cast<std::size_t>(index));
                       },
                       &context, static_cast<std::uint32_t>(chunk)};
    }
    JobCounter counter;
    jobs.run(list, priority, counter);
    jobs.wait(counter);
}

} // namespace eaw
