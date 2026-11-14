#pragma once

// The engine's job system (docs/ecs-design.md, section 7). Jobs run on fibers over worker threads
// plus the thread that created the system. Counters are the only synchronization: running jobs
// adds to a counter, each finished job takes one off, and waiting on a counter parks the waiting
// fiber while its thread goes on running other jobs; when the counter reaches zero, the parked
// fiber resumes, possibly on another thread. No OS mutexes or condition variables: lock-free
// queues, atomics, and idle workers sleeping on an atomic (`std::atomic::wait`).
//
// Rules for code that runs in jobs (AGENTS.md): after a wait the job may be on another thread, so
// hold no lock across a wait and keep no thread_local state across one. Results must not depend on
// which thread ran what (parallel.hpp: chunk boundaries from the count, merges in chunk order).

#include "core/status.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace eaw {

// Queues in this order: the simulation's tick first, then presentation's frame work, then
// background work (asset decoding, content builds).
enum class JobPriority : std::uint8_t
{
    simulation,
    frame,
    background,
};
inline constexpr std::size_t job_priority_count = 3;

// A job: a function with its argument and an index (a chunk's or a job's number).
using JobFunction = void (*)(void* argument, std::uint32_t index);
struct Job
{
    JobFunction function{};
    void* argument{};
    std::uint32_t index{};
};

// The most fibers that may wait on one counter at a time; more is a fatal error naming the limit.
inline constexpr std::size_t max_counter_waiters = 4;

// Counts unfinished jobs. Owned by the code that runs and waits; it must outlive its jobs.
class JobCounter final
{
public:
    JobCounter() = default;
    JobCounter(const JobCounter&) = delete;
    JobCounter& operator=(const JobCounter&) = delete;

    // Jobs run on this counter that have not finished yet.
    [[nodiscard]] std::uint32_t pending() const noexcept
    {
        return pending_.load(std::memory_order_acquire);
    }

private:
    friend class JobSystem;
    std::atomic<std::uint32_t> pending_{0};
    // Jobs past their decrement that still touch the counter (resuming its waiters); wait returns
    // only once none is, so the counter may be destroyed as soon as wait returns.
    std::atomic<std::uint32_t> finishing_{0};
    // Parked fibers (their id plus one; 0 for a free slot).
    std::array<std::atomic<std::uint32_t>, max_counter_waiters> waiters_{};
};

// The hardware threads less one (the creating thread works too); at least zero.
[[nodiscard]] std::uint32_t default_job_workers() noexcept;

struct JobSystemOptions
{
    // Worker threads besides the creating thread; 0 runs every job on the creating thread (for
    // stepping through in a debugger).
    std::uint32_t workers{default_job_workers()};
    // Fibers in the pool: one per job that is waiting at the same time, plus one per thread.
    // Running out is a fatal error naming this option.
    std::uint32_t fibers{128};
    // Each fiber's reserved stack (committed as it grows, with a guard page).
    std::size_t fiber_stack_bytes{512U * 1024U};
    // Jobs each priority's queue holds before `run` runs further jobs on the calling fiber.
    std::uint32_t queue_capacity{4096};
};

class JobSystem final
{
public:
    // Turns the calling thread into a fiber (unless it is one) and starts the workers. Destroy the
    // system on the same thread, with no job left to run.
    [[nodiscard]] static Result<std::unique_ptr<JobSystem>> create(const JobSystemOptions& options = {});
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Queues the jobs and adds them to `counter`. A full queue runs the rest on the calling fiber.
    // Call from the creating thread or from a job.
    void run(std::span<const Job> jobs, JobPriority priority, JobCounter& counter);
    // Returns once `counter` is zero; the calling fiber is parked meanwhile and its thread runs
    // other jobs. Call from the creating thread or from a job.
    void wait(JobCounter& counter);

    [[nodiscard]] std::uint32_t worker_count() const noexcept;

    struct State;

private:
    explicit JobSystem(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace eaw
