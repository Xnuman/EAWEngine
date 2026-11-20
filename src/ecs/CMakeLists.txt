project(ecs)

add_library(ecs STATIC)

set(HEADERS_DIR "include/ecs")
set(SOURCES_DIR "src")

set(ECS_HEADERS
    ${HEADERS_DIR}/component_pool.hpp
    ${HEADERS_DIR}/system_view.hpp
)

set(ECS_SOURCES
    ${SOURCES_DIR}/component_pool.cpp
)

source_group("" FILES ${ECS_HEADERS} ${ECS_SOURCES})

target_sources(ecs
  PUBLIC
    FILE_SET "ecs_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${ECS_HEADERS}
  PRIVATE
    ${ECS_SOURCES}
)

target_link_libraries(ecs
  PUBLIC
     eaw_core
     eaw_jobs
     eaw_math
     junk)

set_target_properties(ecs PROPERTIES FOLDER "core")