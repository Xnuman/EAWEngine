# eaw_jobs: the job system (docs/ecs-design.md, section 7): jobs on fibers over worker threads,
# counters as the only synchronization, lock-free queues, and deterministic parallel loops.

add_library(eaw_jobs STATIC)

set(HEADERS_DIR "include/jobs")
set(SOURCES_DIR "src")

set(EAW_JOBS_HEADERS
    ${HEADERS_DIR}/job_system.hpp
    ${HEADERS_DIR}/parallel.hpp
)

set(EAW_JOBS_SOURCES
    ${SOURCES_DIR}/job_system.cpp
    ${SOURCES_DIR}/mpmc_queue.hpp
)

source_group( "" FILES ${EAW_JOBS_SOURCES} ${EAW_JOBS_HEADERS} )

target_sources(eaw_jobs
  PUBLIC
    FILE_SET "eaw_jobs_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_JOBS_HEADERS}
  PRIVATE
    ${EAW_JOBS_SOURCES}
)

target_link_libraries(eaw_jobs PUBLIC eaw_core)
