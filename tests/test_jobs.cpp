#include "test_support.hpp"

#include "jobs/job_system.hpp"
#include "jobs/parallel.hpp"

#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

using namespace eaw_test;

std::unique_ptr<eaw::JobSystem> make_jobs(const std::uint32_t workers, const std::uint32_t queue_capacity = 4096)
{
    auto created = eaw::JobSystem::create({.workers = workers, .fibers = 64, .queue_capacity = queue_capacity});
    require(created.ok(), "the job system must start");
    return std::move(created).value();
}

// Every job runs exactly once, whatever the number of workers, and wait returns after the last.
void test_jobs_run_and_wait()
{
    for (const std::uint32_t workers : {0U, 1U, 3U})
    {
        auto jobs = make_jobs(workers);
        constexpr std::uint32_t count = 2000;
        std::vector<std::atomic<std::uint32_t>> runs(count);
        std::vector<eaw::Job> list;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            list.push_back({[](void* argument, const std::uint32_t at)
                            {
                                static_cast<std::atomic<std::uint32_t>*>(argument)[at].fetch_add(1, std::memory_order_relaxed);
                            },
                            runs.data(), index});
        }
        eaw::JobCounter counter;
        jobs->run(list, eaw::JobPriority::simulation, counter);
        jobs->wait(counter);
        bool once = counter.pending() == 0;
        for (const auto& run : runs)
        {
            once = once && run.load() == 1;
        }
        require(once, "every job must run exactly once before wait returns (" + std::to_string(workers) + " workers)");
    }
}

// A float sum folded per chunk and merged in chunk order is the same bit for bit with any number of
// workers: the chunks come from the count and the grain only.
void test_jobs_parallel_for_determinism()
{
    std::vector<float> values(100003);
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        values[index] = std::sin(static_cast<float>(index) * 0.37F) * 1000.0F + 1.0F / static_cast<float>(index + 1);
    }
    constexpr std::size_t grain = 997;
    std::vector<std::uint32_t> results;
    for (const std::uint32_t workers : {0U, 1U, 2U, 7U})
    {
        auto jobs = make_jobs(workers);
        std::vector<float> partials(eaw::chunk_count(values.size(), grain), 0.0F);
        std::vector<std::atomic<std::uint32_t>> covered(values.size());
        eaw::parallel_for(*jobs, values.size(), grain,
                          [&](const std::size_t begin, const std::size_t end, const std::size_t chunk)
                          {
                              float sum = 0.0F;
                              for (std::size_t index = begin; index < end; ++index)
                              {
                                  sum += values[index];
                                  covered[index].fetch_add(1, std::memory_order_relaxed);
                              }
                              partials[chunk] = sum;
                          });
        float total = 0.0F;
        for (const auto partial : partials)
        {
            total += partial;
        }
        bool once = true;
        for (const auto& item : covered)
        {
            once = once && item.load() == 1;
        }
        require(once, "parallel_for must cover every item exactly once");
        results.push_back(std::bit_cast<std::uint32_t>(total));
    }
    require(results[0] == results[1] && results[1] == results[2] && results[2] == results[3], "a parallel sum merged in chunk order must not depend on the number of workers");
}

// Jobs that run jobs and wait on them park their fiber; their threads go on with other jobs, also
// with no worker at all (the creating thread alone).
void test_jobs_nested_waits()
{
    struct Outer
    {
        eaw::JobSystem* jobs{};
        std::atomic<std::uint32_t>* inner_runs{};
        std::atomic<std::uint32_t>* finished{};
    };
    for (const std::uint32_t workers : {0U, 3U})
    {
        auto jobs = make_jobs(workers);
        std::atomic<std::uint32_t> inner_runs{0};
        std::atomic<std::uint32_t> finished{0};
        Outer outer{jobs.get(), &inner_runs, &finished};
        constexpr std::uint32_t outer_count = 48;
        constexpr std::uint32_t inner_count = 16;
        std::vector<eaw::Job> list;
        for (std::uint32_t index = 0; index < outer_count; ++index)
        {
            list.push_back({[](void* argument, std::uint32_t)
                            {
                                auto& context = *static_cast<Outer*>(argument);
                                std::vector<eaw::Job> inner(inner_count, eaw::Job{[](void* runs, std::uint32_t)
                                                                                  {
                                                                                      static_cast<std::atomic<std::uint32_t>*>(runs)->fetch_add(1, std::memory_order_relaxed);
                                                                                  },
                                                                                  context.inner_runs, 0});
                                eaw::JobCounter counter;
                                context.jobs->run(inner, eaw::JobPriority::frame, counter);
                                context.jobs->wait(counter);
                                // All of this job's inner jobs are done once wait returns.
                                if (counter.pending() == 0)
                                {
                                    context.finished->fetch_add(1, std::memory_order_relaxed);
                                }
                            },
                            &outer, index});
        }
        eaw::JobCounter counter;
        jobs->run(list, eaw::JobPriority::simulation, counter);
        jobs->wait(counter);
        require(finished.load() == outer_count && inner_runs.load() == outer_count * inner_count, "jobs waiting on their own jobs must park and resume (" + std::to_string(workers) + " workers)");
    }
}

// A queue too small for the jobs: the rest run on the calling fiber, none is lost.
void test_jobs_full_queue()
{
    auto jobs = make_jobs(2, 8);
    std::atomic<std::uint32_t> runs{0};
    std::vector<eaw::Job> list(300, eaw::Job{[](void* argument, std::uint32_t)
                                             {
                                                 static_cast<std::atomic<std::uint32_t>*>(argument)->fetch_add(1, std::memory_order_relaxed);
                                             },
                                             &runs, 0});
    eaw::JobCounter counter;
    jobs->run(list, eaw::JobPriority::background, counter);
    jobs->wait(counter);
    require(runs.load() == 300, "jobs a full queue cannot hold must still run");
    require(!eaw::JobSystem::create({.fibers = 0}).ok(), "a job system without fibers must be refused");
}

const ModuleRegistrar registrar("jobs", {
                                            {"test_jobs_run_and_wait", &test_jobs_run_and_wait},
                                            {"test_jobs_parallel_for_determinism", &test_jobs_parallel_for_determinism},
                                            {"test_jobs_nested_waits", &test_jobs_nested_waits},
                                            {"test_jobs_full_queue", &test_jobs_full_queue},
                                        });

} // namespace
