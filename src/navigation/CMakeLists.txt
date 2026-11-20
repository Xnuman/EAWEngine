project(navigation)

add_library(navigation STATIC)

set(HEADERS_DIR "include/navigation")
set(SOURCES_DIR "src")

set(NAVIGATION_HEADERS
    ${HEADERS_DIR}/chunk_graph.hpp
    ${HEADERS_DIR}/clearance.hpp
    ${HEADERS_DIR}/footprint.hpp
    ${HEADERS_DIR}/formation.hpp
    ${HEADERS_DIR}/ground_map.hpp
    ${HEADERS_DIR}/navigation_junk_pile.hpp
    ${HEADERS_DIR}/orca.hpp
    ${HEADERS_DIR}/pathfinding.hpp
    ${HEADERS_DIR}/spatial_grid.hpp
)

set(NAVIGATION_SOURCES
    ${SOURCES_DIR}/chunk_graph.cpp
    ${SOURCES_DIR}/clearance.cpp
    ${SOURCES_DIR}/formation.cpp
    ${SOURCES_DIR}/ground_map.cpp
    ${SOURCES_DIR}/orca.cpp
    ${SOURCES_DIR}/pathfinding.cpp
    ${SOURCES_DIR}/simulation_avoidance.cpp
    ${SOURCES_DIR}/spatial_grid.cpp
)

set(ECS_SYSTEMS_HEADERS
    ${HEADERS_DIR}/ecs/systems/navigation_system.hpp
)

set(ECS_SYSTEMS_SOURCES
    ${SOURCES_DIR}/ecs/systems/navigation_system.cpp
)

source_group( "" FILES ${NAVIGATION_HEADERS} ${NAVIGATION_SOURCES} )
source_group( "ecs/systems" FILES ${ECS_SYSTEMS_HEADERS} ${ECS_SYSTEMS_SOURCES} )

target_sources( navigation
  PUBLIC
    FILE_SET "navigation_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${NAVIGATION_HEADERS}
    FILE_SET "navigation_ecs_systems_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${ECS_SYSTEMS_HEADERS}
  PRIVATE
    ${NAVIGATION_SOURCES}
    ${ECS_SYSTEMS_SOURCES}
)

target_link_libraries(navigation PUBLIC eaw_core eaw_jobs eaw_math junk ecs)

set_target_properties(navigation PROPERTIES FOLDER "navigation")
