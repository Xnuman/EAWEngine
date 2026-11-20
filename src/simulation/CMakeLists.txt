project(simulation)


add_library(simulation STATIC)

set(HEADERS_DIR "include/simulation")
set(SOURCES_DIR "src")

set(SIMULATION_HEADERS
    ${HEADERS_DIR}/combat.hpp
    ${HEADERS_DIR}/simulation.hpp
    ${HEADERS_DIR}/system_view.hpp
)

set(SIMULATION_SOURCES
    ${SOURCES_DIR}/combat.cpp
    ${SOURCES_DIR}/simulation.cpp
    ${SOURCES_DIR}/simulation_behavior.cpp
    ${SOURCES_DIR}/simulation_combat.cpp
    ${SOURCES_DIR}/simulation_contact.cpp
    ${SOURCES_DIR}/simulation_internal.hpp
    ${SOURCES_DIR}/simulation_locomotion.cpp
    ${SOURCES_DIR}/simulation_route_trace.cpp
    ${SOURCES_DIR}/simulation_schedule.cpp
    ${SOURCES_DIR}/simulation_squads.cpp
    ${SOURCES_DIR}/simulation_walk.cpp
)

source_group("" FILES ${SIMULATION_HEADERS} ${SIMULATION_SOURCES})

target_sources(simulation
  PUBLIC
    FILE_SET "simulation_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${SIMULATION_HEADERS}
  PRIVATE
    ${SIMULATION_SOURCES}
)

# eaw_core only for Status/Result and fatal errors; the simulation reads no files.
target_link_libraries(simulation
  PUBLIC
    ecs
     eaw_core
     eaw_jobs
     eaw_math
     junk
     navigation)
