# EAWBenchmarks: workloads timed serially and on the job system with several worker counts, with
# their outputs checked against the serial run (scripts/benchmark.ps1 compares two result files).

add_executable(EAWBenchmarks)

set(HEADERS_DIR "include/benchmarks")
set(SOURCES_DIR "src")

set(EAW_BENCHMARKS_HEADERS
    ${HEADERS_DIR}/benchmarks.hpp
)

set(EAW_BENCHMARKS_SOURCES
    ${SOURCES_DIR}/bench_jobs.cpp
    ${SOURCES_DIR}/bench_paths.cpp
    ${SOURCES_DIR}/bench_poses.cpp
    ${SOURCES_DIR}/main.cpp
)

source_group("" FILES ${EAW_BENCHMARKS_HEADERS} ${EAW_BENCHMARKS_SOURCES})

target_sources(EAWBenchmarks
  PRIVATE
    FILE_SET "eaw_benchmarks_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_BENCHMARKS_HEADERS}
  PRIVATE
    ${EAW_BENCHMARKS_SOURCES}
)

target_link_libraries(EAWBenchmarks PRIVATE eaw_jobs eaw_presentation eaw_simulation)
