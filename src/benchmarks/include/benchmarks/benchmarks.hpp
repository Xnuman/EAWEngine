#pragma once

// EAWBenchmarks: workloads timed serially (the way the engine runs them without the job system) and
// on the job system with several worker counts, so a change can be compared with what came before
// (scripts/benchmark.ps1 runs it and compares two result files). Every suite fingerprints its
// output, and each job-system run must match the serial fingerprint: a faster run with a different
// result is a failure, not a win.

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace eaw {
class JobSystem;
}

namespace eaw::benchmarks {

// A prepared workload: its data is built once (untimed), then each run does the timed work, on the
// job system when one is given and serially otherwise, and returns a fingerprint of its output.
struct Workload
{
    std::function<std::uint64_t(JobSystem*)> run;
};

struct Suite
{
    std::string_view name;
    std::string_view description;
    Workload (*prepare)();
};

// Every suite, in the order they run (bench_jobs.cpp, bench_paths.cpp, bench_poses.cpp).
[[nodiscard]] std::span<const Suite> job_suites();
[[nodiscard]] std::span<const Suite> path_suites();
[[nodiscard]] std::span<const Suite> pose_suites();

// Folds bytes into a fingerprint (CRC-32 pieces, so equal outputs give equal fingerprints).
[[nodiscard]] std::uint64_t fingerprint(std::uint64_t fingerprint, const void* data, std::size_t bytes) noexcept;

} // namespace eaw::benchmarks
