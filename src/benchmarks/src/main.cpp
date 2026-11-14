// EAWBenchmarks: runs the suites serially and on the job system with each requested worker count,
// prints a table (median and fastest of the repeats, speed against the serial run, and whether the
// output matched the serial run's), and optionally writes it as tab-separated values for
// scripts/benchmark.ps1 to compare with another run.

#include "core/checksum.hpp"
#include "core/file_io.hpp"
#include "core/string_utils.hpp"
#include "jobs/job_system.hpp"

#include "benchmarks/benchmarks.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace eaw::benchmarks {

std::uint64_t fingerprint(const std::uint64_t fingerprint, const void* data, const std::size_t bytes) noexcept
{
    const auto crc = crc32_update(static_cast<std::uint32_t>(fingerprint) ^ crc32_initial, std::span(static_cast<const std::byte*>(data), bytes));
    return crc32_finish(crc);
}

namespace {

// The job system for benchmarks holds a whole batch of tiny jobs in its queue (so the tiny-job suite
// measures scheduling, not the full-queue fallback) and enough fibers for every nested wait.
constexpr std::uint32_t benchmark_queue_capacity = 1U << 18U;
constexpr std::uint32_t benchmark_fibers = 1024;

struct Options
{
    std::vector<std::string> suites;
    std::vector<std::uint32_t> workers;
    std::uint32_t repeat{5};
    std::optional<std::string> output;
    bool list{};
};

struct Row
{
    std::string suite;
    std::string variant;
    double median_ms{};
    double fastest_ms{};
    double speedup{};
    bool matches{};
};

void usage()
{
    std::cout << "Usage: EAWBenchmarks [suite...] [options]\n"
              << "  --workers <n,n,...>  Worker counts to run on the job system (default 0,1,2,4 and the hardware threads less one)\n"
              << "  --repeat <n>         Timed runs per configuration after one warm-up (default 5; the median is reported)\n"
              << "  --output <file.tsv>  Also write the results as tab-separated values (scripts/benchmark.ps1)\n"
              << "  --list               List the suites\n";
}

std::optional<std::uint32_t> parse_count(const std::string_view text)
{
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? std::optional(value) : std::nullopt;
}

std::optional<Options> parse(const int argc, char** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        const auto value = [&]() -> std::optional<std::string_view>
        {
            return index + 1 < argc ? std::optional<std::string_view>(argv[++index]) : std::nullopt;
        };
        if (argument == "--workers")
        {
            const auto text = value();
            if (!text)
            {
                return std::nullopt;
            }
            for (const auto item : split_list(*text))
            {
                const auto count = parse_count(item);
                if (!count)
                {
                    std::cerr << "--workers needs whole numbers, not '" << item << "'\n";
                    return std::nullopt;
                }
                options.workers.push_back(*count);
            }
        }
        else if (argument == "--repeat")
        {
            const auto text = value();
            const auto count = text ? parse_count(*text) : std::nullopt;
            if (!count || *count == 0)
            {
                std::cerr << "--repeat needs a count of at least 1\n";
                return std::nullopt;
            }
            options.repeat = *count;
        }
        else if (argument == "--output")
        {
            const auto text = value();
            if (!text)
            {
                return std::nullopt;
            }
            options.output = std::string(*text);
        }
        else if (argument == "--list")
        {
            options.list = true;
        }
        else if (argument.starts_with("--"))
        {
            std::cerr << "unknown option: " << argument << '\n';
            return std::nullopt;
        }
        else
        {
            options.suites.emplace_back(argument);
        }
    }
    if (options.workers.empty())
    {
        options.workers = {0, 1, 2, 4};
        if (const auto most = default_job_workers(); most > 4)
        {
            options.workers.push_back(most);
        }
    }
    return options;
}

// Times `repeat` runs after one warm-up; returns the sorted times (ms) and checks every fingerprint
// against `expected` (the first one when there is none yet).
std::vector<double> time_runs(const Workload& workload, JobSystem* jobs, const std::uint32_t repeat, std::optional<std::uint64_t>& expected, bool& matches)
{
    const auto check = [&](const std::uint64_t print)
    {
        if (!expected)
        {
            expected = print;
        }
        matches = matches && print == *expected;
    };
    check(workload.run(jobs));
    std::vector<double> times;
    for (std::uint32_t run = 0; run < repeat; ++run)
    {
        const auto start = std::chrono::steady_clock::now();
        const auto print = workload.run(jobs);
        times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        check(print);
    }
    std::ranges::sort(times);
    return times;
}

std::string format(const double value, const int decimals)
{
    std::ostringstream text;
    text << std::fixed << std::setprecision(decimals) << value;
    return text.str();
}

int run(const Options& options)
{
    std::vector<Suite> all;
    for (const auto& suite : job_suites())
    {
        all.push_back(suite);
    }
    for (const auto& suite : path_suites())
    {
        all.push_back(suite);
    }
    if (options.list)
    {
        for (const auto& suite : all)
        {
            std::cout << std::left << std::setw(14) << suite.name << suite.description << '\n';
        }
        return 0;
    }
    for (const auto& name : options.suites)
    {
        if (std::ranges::none_of(all,
                                 [&](const Suite& suite)
                                 {
                                     return suite.name == name;
                                 }))
        {
            std::cerr << "unknown suite: " << name << " (--list shows them)\n";
            return 2;
        }
    }
#ifdef NDEBUG
    const char* configuration = "Release";
#else
    const char* configuration = "Debug (timings not representative)";
#endif
    std::cout << "EAWBenchmarks, " << configuration << ", " << std::thread::hardware_concurrency() << " hardware threads, " << options.repeat << " timed runs each\n\n";
    std::vector<Row> rows;
    bool all_match = true;
    for (const auto& suite : all)
    {
        if (!options.suites.empty() && std::ranges::find(options.suites, std::string(suite.name)) == options.suites.end())
        {
            continue;
        }
        std::cout << suite.name << ": " << suite.description << '\n';
        const auto workload = suite.prepare();
        std::optional<std::uint64_t> expected;
        bool matches = true;
        const auto serial = time_runs(workload, nullptr, options.repeat, expected, matches);
        const auto serial_median = serial[serial.size() / 2];
        rows.push_back({std::string(suite.name), "serial", serial_median, serial.front(), 1.0, matches});
        for (const auto workers : options.workers)
        {
            auto created = JobSystem::create({.workers = workers, .fibers = benchmark_fibers, .queue_capacity = benchmark_queue_capacity});
            if (!created)
            {
                std::cerr << "the job system did not start with " << workers << " workers: " << created.error().message << '\n';
                return 1;
            }
            bool variant_matches = true;
            const auto times = time_runs(workload, created->get(), options.repeat, expected, variant_matches);
            const auto median = times[times.size() / 2];
            rows.push_back({std::string(suite.name), "jobs-" + std::to_string(workers) + "w", median, times.front(), median > 0.0 ? serial_median / median : 0.0, variant_matches});
        }
    }
    std::cout << '\n'
              << std::left << std::setw(14) << "suite" << std::setw(10) << "variant" << std::right << std::setw(12) << "median ms" << std::setw(12) << "fastest ms" << std::setw(10) << "speedup"
              << "  output\n";
    std::ostringstream tsv;
    tsv << "suite\tvariant\tmedian_ms\tfastest_ms\tspeedup\tmatches\n";
    for (const auto& row : rows)
    {
        all_match = all_match && row.matches;
        std::cout << std::left << std::setw(14) << row.suite << std::setw(10) << row.variant << std::right << std::setw(12) << format(row.median_ms, 3) << std::setw(12) << format(row.fastest_ms, 3)
                  << std::setw(9) << format(row.speedup, 2) << "x" << (row.matches ? "  same" : "  DIFFERENT") << '\n';
        tsv << row.suite << '\t' << row.variant << '\t' << format(row.median_ms, 4) << '\t' << format(row.fastest_ms, 4) << '\t' << format(row.speedup, 3) << '\t' << (row.matches ? "yes" : "no")
            << '\n';
    }
    if (options.output)
    {
        if (const auto written = write_text_file(*options.output, tsv.str(), WriteMode::replace); !written)
        {
            std::cerr << "could not write " << *options.output << ": " << written.error().message << '\n';
            return 1;
        }
    }
    if (!all_match)
    {
        std::cout << "\nFAILED: a job-system run's output differs from the serial run's\n";
        return 1;
    }
    return 0;
}

} // namespace

} // namespace eaw::benchmarks

int main(const int argc, char** argv)
{
    const auto options = eaw::benchmarks::parse(argc, argv);
    if (!options)
    {
        eaw::benchmarks::usage();
        return 2;
    }
    return eaw::benchmarks::run(*options);
}
