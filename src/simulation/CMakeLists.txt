# eaw_simulation: the deterministic tactical simulation: entity ids, structure-of-arrays
# components, fixed-step ticks, commands, and snapshots. No graphics, platform, or file access.

add_library(eaw_simulation STATIC)

set(HEADERS_DIR "include/simulation")
set(SOURCES_DIR "src")

set(EAW_SIMULATION_HEADERS
    ${HEADERS_DIR}/combat.hpp
    ${HEADERS_DIR}/component_pool.hpp
    ${HEADERS_DIR}/formation.hpp
    ${HEADERS_DIR}/ground_map.hpp
    ${HEADERS_DIR}/pathfinding.hpp
    ${HEADERS_DIR}/simulation.hpp
    ${HEADERS_DIR}/simulation_types.hpp
    ${HEADERS_DIR}/spatial_grid.hpp
)

set(EAW_SIMULATION_SOURCES
    ${SOURCES_DIR}/combat.cpp
    ${SOURCES_DIR}/formation.cpp
    ${SOURCES_DIR}/ground_map.cpp
    ${SOURCES_DIR}/pathfinding.cpp
    ${SOURCES_DIR}/simulation.cpp
    ${SOURCES_DIR}/simulation_behavior.cpp
    ${SOURCES_DIR}/simulation_combat.cpp
    ${SOURCES_DIR}/simulation_internal.hpp
    ${SOURCES_DIR}/simulation_locomotion.cpp
    ${SOURCES_DIR}/simulation_navigation.cpp
    ${SOURCES_DIR}/simulation_schedule.cpp
    ${SOURCES_DIR}/simulation_walk.cpp
    ${SOURCES_DIR}/spatial_grid.cpp
)

source_group("" FILES ${EAW_SIMULATION_HEADERS} ${EAW_SIMULATION_SOURCES})

target_sources(eaw_simulation
  PUBLIC
    FILE_SET "eaw_simulation_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_SIMULATION_HEADERS}
  PRIVATE
    ${EAW_SIMULATION_SOURCES}
)

# The access check of the tick's systems, in DebugValidation builds only (simulation_schedule.cpp).
target_compile_definitions(eaw_simulation PRIVATE $<$<CONFIG:DebugValidation>:EAW_SIMULATION_ACCESS_CHECK>)

# eaw_core only for Status/Result and fatal errors; the simulation reads no files.
target_link_libraries(eaw_simulation PUBLIC eaw_core eaw_math)
