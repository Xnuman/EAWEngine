// The job system's own costs and scaling: tiny jobs (scheduling overhead per job), compute-heavy
// chunks (scaling with workers), and jobs that wait on their own jobs (parking and resuming fibers).

#include "jobs/job_system.hpp"
#include "jobs/parallel.hpp"

#include "benchmarks/benchmarks.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace eaw::benchmarks {

namespace {

// 200,000 jobs that each write one number: the cost of scheduling a job, against a plain call.
constexpr std::uint32_t tiny_job_count = 200000;
// 2,000,000 items of 48 rounds of float math each, in chunks of 8,192.
constexpr std::size_t math_items = 2000000;
constexpr int math_rounds = 48;
constexpr std::size_t math_grain = 8192;
// 1,000 short loops of 512 items of math in chunks of 4, as a simulation tick runs them (ground
// avoidance: a few hundred steering units, one loop a tick): the fixed cost of a parallel_for call.
constexpr std::size_t loop_calls = 1000;
constexpr std::size_t loop_items = 512;
constexpr std::size_t loop_grain = 4;
// 512 jobs that each run 32 jobs and wait on them.
constexpr std::uint32_t outer_jobs = 512;
constexpr std::uint32_t inner_jobs = 32;

void tiny_job(void* argument, const std::uint32_t index)
{
    static_cast<std::uint32_t*>(argument)[index] = index * 2654435761U;
}

Workload prepare_tiny_jobs()
{
    auto slots = std::make_shared<std::vector<std::uint32_t>>(tiny_job_count);
    auto list = std::make_shared<std::vector<Job>>();
    for (std::uint32_t index = 0; index < tiny_job_count; ++index)
    {
        list->push_back({&tiny_job, slots->data(), index});
    }
    return {[slots, list](JobSystem* jobs)
            {
                std::fill(slots->begin(), slots->end(), 0U);
                if (jobs == nullptr)
                {
                    for (const auto& job : *list)
                    {
                        job.function(job.argument, job.index);
                    }
                }
                else
                {
                    JobCounter counter;
                    jobs->run(*list, JobPriority::simulation, counter);
                    jobs->wait(counter);
                }
                return fingerprint(0, slots->data(), slots->size() * sizeof(std::uint32_t));
            }};
}

float churn(const float seed) noexcept
{
    auto value = seed;
    for (int round = 0; round < math_rounds; ++round)
    {
        value = std::sin(value) * 1.7F + std::sqrt(std::fabs(value) + 1.0F) * 0.3F;
    }
    return value;
}

// The items [begin, end) of a math workload, `offset` added to each seed. Never inlined: inlined
// into the callers, the serial loop and the chunk got different code from the same source (the
// serial run 23% slower after a change to parallel_for alone), and times compared different code.
__declspec(noinline) void churn_range(float* output, const std::size_t begin, const std::size_t end, const std::size_t offset) noexcept
{
    for (std::size_t index = begin; index < end; ++index)
    {
        output[index] = churn(static_cast<float>(index + offset) * 0.001F);
    }
}

Workload prepare_math()
{
    auto output = std::make_shared<std::vector<float>>(math_items);
    return {[output](JobSystem* jobs)
            {
                const auto chunk = [&](const std::size_t begin, const std::size_t end, std::size_t)
                {
                    churn_range(output->data(), begin, end, 0);
                };
                if (jobs == nullptr)
                {
                    for (std::size_t begin = 0; begin < math_items; begin += math_grain)
                    {
                        chunk(begin, std::min(begin + math_grain, math_items), 0);
                    }
                }
                else
                {
                    parallel_for(*jobs, math_items, math_grain, chunk);
                }
                return fingerprint(0, output->data(), output->size() * sizeof(float));
            }};
}

Workload prepare_loops()
{
    auto output = std::make_shared<std::vector<float>>(loop_items);
    return {[output](JobSystem* jobs)
            {
                std::uint64_t print = 0;
                for (std::size_t call = 0; call < loop_calls; ++call)
                {
                    const auto chunk = [&](const std::size_t begin, const std::size_t end, std::size_t)
                    {
                        churn_range(output->data(), begin, end, call);
                    };
                    if (jobs == nullptr)
                    {
                        chunk(0, loop_items, 0);
                    }
                    else
                    {
                        parallel_for(*jobs, loop_items, loop_grain, chunk);
                    }
                    print = fingerprint(print, output->data(), output->size() * sizeof(float));
                }
                return print;
            }};
}

struct Nested
{
    JobSystem* jobs{};
    std::vector<std::uint32_t>* results{};
};

void inner_job(void* argument, const std::uint32_t index)
{
    static_cast<std::uint32_t*>(argument)[index] = index + 1;
}

void outer_job(void* argument, const std::uint32_t index)
{
    auto& nested = *static_cast<Nested*>(argument);
    auto* slots = nested.results->data() + static_cast<std::size_t>(index) * inner_jobs;
    std::array<Job, inner_jobs> list{};
    for (std::uint32_t inner = 0; inner < inner_jobs; ++inner)
    {
        list[inner] = {&inner_job, slots, inner};
    }
    if (nested.jobs == nullptr)
    {
        for (const auto& job : list)
        {
            job.function(job.argument, job.index);
        }
        return;
    }
    JobCounter counter;
    nested.jobs->run(list, JobPriority::frame, counter);
    nested.jobs->wait(counter);
}

Workload prepare_nested()
{
    auto results = std::make_shared<std::vector<std::uint32_t>>(static_cast<std::size_t>(outer_jobs) * inner_jobs);
    return {[results](JobSystem* jobs)
            {
                std::fill(results->begin(), results->end(), 0U);
                Nested nested{jobs, results.get()};
                if (jobs == nullptr)
                {
                    for (std::uint32_t index = 0; index < outer_jobs; ++index)
                    {
                        outer_job(&nested, index);
                    }
                }
                else
                {
                    std::vector<Job> list;
                    for (std::uint32_t index = 0; index < outer_jobs; ++index)
                    {
                        list.push_back({&outer_job, &nested, index});
                    }
                    JobCounter counter;
                    jobs->run(list, JobPriority::simulation, counter);
                    jobs->wait(counter);
                }
                return fingerprint(0, results->data(), results->size() * sizeof(std::uint32_t));
            }};
}

constexpr std::array<Suite, 4> suites{{
    {"jobs-tiny", "200,000 jobs writing one number each: the scheduling cost per job", &prepare_tiny_jobs},
    {"jobs-math", "2,000,000 items of float math in 8,192-item chunks: scaling with workers", &prepare_math},
    {"jobs-loops", "1,000 parallel_for calls of 512 math items in chunks of 4: the fixed cost of a short loop, as a tick runs them", &prepare_loops},
    {"jobs-nested", "512 jobs each running 32 jobs and waiting on them: parking and resuming fibers", &prepare_nested},
}};

} // namespace

std::span<const Suite> job_suites()
{
    return suites;
}

} // namespace eaw::benchmarks
