#include "jobs/job_system.hpp"

#include "mpmc_queue.hpp"

#include "core/fatal_error.hpp"
#include "core/profile.hpp"

#include <windows.h>
#include <immintrin.h>

#include <algorithm>
#include <string>
#include <thread>
#include <vector>

namespace eaw {

using jobs_detail::MpmcQueue;

namespace {

// An idle worker checks for work this many times, pausing in between, before it sleeps on the
// work epoch (an engine choice: a few microseconds, so bursts of short jobs do not pay for a sleep).
constexpr int idle_spins = 64;
// Committed stack of a new fiber; the rest of options.fiber_stack_bytes is reserved and committed
// as it grows.
constexpr std::size_t fiber_stack_commit = 64U * 1024U;

// What the fiber switched to must do for the fiber switched from, once that one is off the CPU:
// a fiber cannot publish itself as waiting (or as free) while it still runs, or another thread
// could resume it before the switch completes.
enum class Handoff : std::uint8_t
{
    none,
    // Park it on a counter (it called wait).
    park,
    // Return it to the pool (a scheduling fiber that resumed a parked one, or went home).
    release,
};

} // namespace

std::uint32_t default_job_workers() noexcept
{
    const auto hardware = std::thread::hardware_concurrency();
    return hardware > 1 ? hardware - 1 : 0;
}

struct JobSystem::State
{
    struct QueuedJob
    {
        Job job;
        JobCounter* counter{};
    };
    // One per thread: index 0 is the creating thread.
    struct Worker
    {
        std::uint32_t thread{};
        // This thread's own fiber (the converted thread), pinned to it.
        std::uint32_t home{};
        // The fiber running on this thread now.
        std::uint32_t current{};
        Handoff handoff{Handoff::none};
        std::uint32_t handoff_fiber{};
        JobCounter* handoff_counter{};
    };

    explicit State(const JobSystemOptions& settings) : options(settings), pool_size(settings.fibers), free_fibers(settings.fibers), ready(settings.fibers + settings.workers + 1)
    {
        for (auto& queue : jobs)
        {
            queue = std::make_unique<MpmcQueue<QueuedJob>>(settings.queue_capacity);
        }
    }

    JobSystemOptions options;
    DWORD tls{TLS_OUT_OF_INDEXES};
    std::uint32_t pool_size{};
    // Fiber handles by id: the pool first (ids 0..pool_size-1), then each thread's home fiber.
    std::vector<void*> fibers;
#if defined(EAW_PROFILE)
    // The pool fibers' and worker threads' names in the profiler (lasting_name).
    std::vector<const char*> fiber_names;
    std::vector<const char*> thread_names;
#endif
    MpmcQueue<std::uint32_t> free_fibers;
    // Parked fibers whose counter reached zero: pool fibers (any thread) and home fibers (their
    // own thread only).
    MpmcQueue<std::uint32_t> ready;
    std::vector<std::unique_ptr<MpmcQueue<std::uint32_t>>> pinned;
    std::array<std::unique_ptr<MpmcQueue<QueuedJob>>, job_priority_count> jobs;
    std::vector<std::unique_ptr<Worker>> workers;
    std::vector<std::thread> threads;
    // Bumped (and waited on) whenever work arrives, so idle workers sleep without a lock.
    std::atomic<std::uint32_t> epoch{0};
    std::atomic<std::uint32_t> started{0};
    std::atomic<bool> stopping{false};
    bool converted_creator{};

    [[nodiscard]] Worker& current_worker() const noexcept
    {
        auto* worker = static_cast<Worker*>(TlsGetValue(tls));
        EAW_ASSERT_MSG(worker != nullptr, "the job system was used from a thread it does not run on");
        return *worker;
    }

    void wake() noexcept
    {
        epoch.fetch_add(1, std::memory_order_release);
        epoch.notify_all();
    }

    // Moves a parked fiber to the queue it may run from.
    void make_ready(const std::uint32_t fiber) noexcept
    {
        const bool pushed = fiber >= pool_size ? pinned[fiber - pool_size]->push(fiber) : ready.push(fiber);
        EAW_ASSERT_MSG(pushed, "a ready queue overflowed (it holds every fiber)");
        wake();
    }

    // Switches this thread to `target`, telling it what to do for the fiber switched from; returns
    // when this fiber is resumed, possibly on another thread.
    void switch_to(const std::uint32_t target, const Handoff handoff, JobCounter* counter = nullptr) noexcept
    {
        auto& worker = current_worker();
        worker.handoff = handoff;
        worker.handoff_fiber = worker.current;
        worker.handoff_counter = counter;
        worker.current = target;
        SwitchToFiber(fibers[target]);
        after_switch();
    }

    // Done first thing on the fiber switched to.
    void after_switch() noexcept
    {
        auto& worker = current_worker();
        // The profiler follows zones by fiber: a pool fiber by its name, a thread's home fiber as
        // the thread itself.
#if defined(EAW_PROFILE)
        if (worker.current < pool_size)
        {
            EAW_PROFILE_FIBER_ENTER(fiber_names[worker.current]);
        }
        else
        {
            EAW_PROFILE_FIBER_LEAVE();
        }
#endif
        const auto handoff = worker.handoff;
        worker.handoff = Handoff::none;
        if (handoff == Handoff::release)
        {
            const bool pushed = free_fibers.push(worker.handoff_fiber);
            EAW_ASSERT_MSG(pushed, "the free fiber queue overflowed (it holds the whole pool)");
        }
        else if (handoff == Handoff::park)
        {
            park(worker.handoff_fiber, *worker.handoff_counter);
        }
    }

    void park(const std::uint32_t fiber, JobCounter& counter) noexcept
    {
        // Counted as finishing: once registered, the fiber may be resumed elsewhere and return from
        // wait while this still reads the counter.
        counter.finishing_.fetch_add(1, std::memory_order_acq_rel);
        const auto value = fiber + 1;
        std::size_t slot = 0;
        for (; slot < counter.waiters_.size(); ++slot)
        {
            std::uint32_t empty = 0;
            if (counter.waiters_[slot].compare_exchange_strong(empty, value, std::memory_order_acq_rel))
            {
                break;
            }
        }
        if (slot == counter.waiters_.size())
        {
            fatal_error("more than " + std::to_string(max_counter_waiters) + " fibers wait on one job counter at once (max_counter_waiters)");
        }
        // The counter may have reached zero before this fiber was registered: then whoever takes
        // the registration back resumes it, exactly once.
        if (counter.pending_.load(std::memory_order_acquire) == 0)
        {
            auto mine = value;
            if (counter.waiters_[slot].compare_exchange_strong(mine, 0, std::memory_order_acq_rel))
            {
                make_ready(fiber);
            }
        }
        // The last access to the counter.
        counter.finishing_.fetch_sub(1, std::memory_order_release);
    }

    void finish(JobCounter& counter) noexcept
    {
        // Marked as finishing before the decrement: once pending reaches zero the waiter may
        // resume at any moment, and it must not return (and destroy the counter) while this job
        // still reads the counter's waiter slots.
        counter.finishing_.fetch_add(1, std::memory_order_acq_rel);
        if (counter.pending_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            for (auto& waiter : counter.waiters_)
            {
                if (const auto value = waiter.exchange(0, std::memory_order_acq_rel); value != 0)
                {
                    make_ready(value - 1);
                }
            }
        }
        // The last access to the counter.
        counter.finishing_.fetch_sub(1, std::memory_order_release);
    }

    // Whether no job is left and none still touches the counter.
    [[nodiscard]] static bool done(const JobCounter& counter) noexcept
    {
        return counter.pending_.load(std::memory_order_acquire) == 0 && counter.finishing_.load(std::memory_order_acquire) == 0;
    }

    void execute(const QueuedJob& queued) noexcept
    {
        {
            EAW_PROFILE_ZONE("job");
            queued.job.function(queued.job.argument, queued.job.index);
        }
        finish(*queued.counter);
    }

    [[nodiscard]] bool pop_job(QueuedJob& queued) noexcept
    {
        return std::ranges::any_of(jobs,
                                   [&](const auto& queue)
                                   {
                                       return queue->pop(queued);
                                   });
    }

    [[nodiscard]] bool has_work(const Worker& worker) const noexcept
    {
        return !pinned[worker.thread]->looks_empty() || !ready.looks_empty() ||
               std::ranges::any_of(jobs,
                                   [](const auto& queue)
                                   {
                                       return !queue->looks_empty();
                                   });
    }

    void idle(const Worker& worker) noexcept
    {
        for (int spin = 0; spin < idle_spins; ++spin)
        {
            if (has_work(worker) || stopping.load(std::memory_order_acquire))
            {
                return;
            }
            _mm_pause();
        }
        const auto seen = epoch.load(std::memory_order_acquire);
        if (has_work(worker) || stopping.load(std::memory_order_acquire))
        {
            return;
        }
        epoch.wait(seen, std::memory_order_acquire);
    }

    // The loop every pool fiber runs: resume ready fibers, run jobs, sleep when there is nothing.
    void schedule() noexcept
    {
        for (;;)
        {
            auto& worker = current_worker();
            if (stopping.load(std::memory_order_acquire) && worker.thread != 0)
            {
                switch_to(worker.home, Handoff::release);
                continue;
            }
            std::uint32_t fiber = 0;
            if (pinned[worker.thread]->pop(fiber) || ready.pop(fiber))
            {
                switch_to(fiber, Handoff::release);
                continue;
            }
            QueuedJob queued;
            if (pop_job(queued))
            {
                execute(queued);
                continue;
            }
            idle(worker);
        }
    }

    [[nodiscard]] std::uint32_t take_free_fiber() noexcept
    {
        std::uint32_t fiber = 0;
        if (!free_fibers.pop(fiber))
        {
            fatal_error("the job system ran out of fibers (" + std::to_string(pool_size) + " in the pool, JobSystemOptions::fibers): more jobs wait at once than it holds");
        }
        return fiber;
    }

    void run_worker(const std::uint32_t thread)
    {
        auto* home = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
        if (home == nullptr)
        {
            fatal_error("a job worker thread could not become a fiber (ConvertThreadToFiberEx failed with " + std::to_string(GetLastError()) + ")");
        }
        auto& worker = *workers[thread];
        fibers[pool_size + thread] = home;
        TlsSetValue(tls, &worker);
        worker.current = worker.home;
        started.fetch_add(1, std::memory_order_release);
        started.notify_all();
        switch_to(take_free_fiber(), Handoff::none);
        // Back home: the system is stopping.
        ConvertFiberToThread();
    }
};

namespace {

#if defined(EAW_PROFILE)
// The profiler reads a fiber's or thread's name through its pointer whenever its viewer asks, which
// may be after the job system is gone: in profiling builds the names last for the rest of the
// process (a few bytes per fiber of every job system created).
const char* lasting_name(std::string name)
{
    return (new std::string(std::move(name)))->c_str();
}
#endif

void WINAPI fiber_main(void* parameter)
{
    auto& state = *static_cast<JobSystem::State*>(parameter);
    state.after_switch();
    state.schedule();
}

} // namespace

Result<std::unique_ptr<JobSystem>> JobSystem::create(const JobSystemOptions& options)
{
    if (options.fibers == 0 || options.fiber_stack_bytes < fiber_stack_commit || options.queue_capacity == 0)
    {
        return make_error("job system options need at least one fiber, a fiber stack of at least " + std::to_string(fiber_stack_commit) + " bytes, and a queue capacity");
    }
    auto state = std::make_unique<State>(options);
    state->tls = TlsAlloc();
    if (state->tls == TLS_OUT_OF_INDEXES)
    {
        return make_error("the job system could not allocate a thread-local slot (TlsAlloc failed with " + std::to_string(GetLastError()) + ")");
    }
    const auto threads = options.workers + 1;
    state->fibers.assign(static_cast<std::size_t>(state->pool_size) + threads, nullptr);
#if defined(EAW_PROFILE)
    for (std::uint32_t fiber = 0; fiber < state->pool_size; ++fiber)
    {
        state->fiber_names.push_back(lasting_name("job fiber " + std::to_string(fiber)));
    }
    for (std::uint32_t thread = 1; thread < threads; ++thread)
    {
        state->thread_names.push_back(lasting_name("job worker " + std::to_string(thread)));
    }
#endif
    for (std::uint32_t thread = 0; thread < threads; ++thread)
    {
        auto worker = std::make_unique<State::Worker>();
        worker->thread = thread;
        worker->home = state->pool_size + thread;
        state->workers.push_back(std::move(worker));
        state->pinned.push_back(std::make_unique<MpmcQueue<std::uint32_t>>(2));
    }
    // The creating thread is thread 0; it keeps its own fiber (as the home of thread 0).
    void* creator = nullptr;
    if (IsThreadAFiber())
    {
        creator = GetCurrentFiber();
    }
    else
    {
        creator = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
        if (creator == nullptr)
        {
            TlsFree(state->tls);
            return make_error("the job system's thread could not become a fiber (ConvertThreadToFiberEx failed with " + std::to_string(GetLastError()) + ")");
        }
        state->converted_creator = true;
    }
    state->fibers[state->pool_size] = creator;
    TlsSetValue(state->tls, state->workers[0].get());
    state->workers[0]->current = state->workers[0]->home;
    for (std::uint32_t fiber = 0; fiber < state->pool_size; ++fiber)
    {
        state->fibers[fiber] = CreateFiberEx(fiber_stack_commit, options.fiber_stack_bytes, FIBER_FLAG_FLOAT_SWITCH, &fiber_main, state.get());
        if (state->fibers[fiber] == nullptr)
        {
            const auto code = GetLastError();
            for (std::uint32_t created = 0; created < fiber; ++created)
            {
                DeleteFiber(state->fibers[created]);
            }
            TlsSetValue(state->tls, nullptr);
            TlsFree(state->tls);
            if (state->converted_creator)
            {
                ConvertFiberToThread();
            }
            return make_error("the job system could not create fiber " + std::to_string(fiber) + " of " + std::to_string(state->pool_size) + " (CreateFiberEx failed with " + std::to_string(code) +
                              ")");
        }
        const bool pushed = state->free_fibers.push(fiber);
        EAW_ASSERT_MSG(pushed, "the free fiber queue holds the whole pool");
    }
    for (std::uint32_t thread = 1; thread < threads; ++thread)
    {
        state->threads.emplace_back(
            [raw = state.get(), thread]
            {
                const auto name = L"EAW job worker " + std::to_wstring(thread);
                SetThreadDescription(GetCurrentThread(), name.c_str());
#if defined(EAW_PROFILE)
                EAW_PROFILE_THREAD_NAME(raw->thread_names[thread - 1]);
#endif
                raw->run_worker(thread);
            });
    }
    for (auto seen = state->started.load(std::memory_order_acquire); seen < options.workers; seen = state->started.load(std::memory_order_acquire))
    {
        state->started.wait(seen, std::memory_order_acquire);
    }
    return std::unique_ptr<JobSystem>(new JobSystem(std::move(state)));
}

JobSystem::JobSystem(std::unique_ptr<State> state) noexcept : state_(std::move(state))
{}

JobSystem::~JobSystem()
{
    auto& state = *state_;
    EAW_ASSERT_MSG(state.current_worker().thread == 0, "the job system must be destroyed on the thread that created it");
    State::QueuedJob left;
    EAW_ASSERT_MSG(!state.pop_job(left), "the job system was destroyed with jobs still queued");
    state.stopping.store(true, std::memory_order_release);
    state.wake();
    for (auto& thread : state.threads)
    {
        thread.join();
    }
    for (std::uint32_t fiber = 0; fiber < state.pool_size; ++fiber)
    {
        DeleteFiber(state.fibers[fiber]);
    }
    TlsSetValue(state.tls, nullptr);
    TlsFree(state.tls);
    if (state.converted_creator)
    {
        ConvertFiberToThread();
    }
}

void JobSystem::run(const std::span<const Job> jobs, const JobPriority priority, JobCounter& counter)
{
    if (jobs.empty())
    {
        return;
    }
    auto& state = *state_;
    counter.pending_.fetch_add(static_cast<std::uint32_t>(jobs.size()), std::memory_order_acq_rel);
    auto& queue = *state.jobs[static_cast<std::size_t>(priority)];
    for (const auto& job : jobs)
    {
        const State::QueuedJob queued{job, &counter};
        if (!queue.push(queued))
        {
            // A full queue: the calling fiber does the job itself (the order jobs run in never
            // decides a result).
            state.wake();
            state.execute(queued);
        }
    }
    state.wake();
}

void JobSystem::wait(JobCounter& counter)
{
    auto& state = *state_;
    if (counter.pending_.load(std::memory_order_acquire) != 0)
    {
        state.switch_to(state.take_free_fiber(), Handoff::park, &counter);
    }
    // The job that finished the counter may still be resuming its waiters: a few instructions.
    while (!State::done(counter))
    {
        _mm_pause();
    }
}

std::uint32_t JobSystem::worker_count() const noexcept
{
    return state_->options.workers;
}

} // namespace eaw
