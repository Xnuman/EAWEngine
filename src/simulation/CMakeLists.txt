# Deterministic tactical simulation core: no graphics, platform, or file access.
set(EAW_SIMULATION_HEADERS
    include/eaw/combat.hpp
    include/eaw/formation.hpp
    include/eaw/ground_map.hpp
    include/eaw/pathfinding.hpp
    include/eaw/simulation.hpp
    include/eaw/simulation_types.hpp
    include/eaw/spatial_grid.hpp
)

set(EAW_SIMULATION_SOURCES
    combat.cpp
    formation.cpp
    ground_map.cpp
    pathfinding.cpp
    simulation.cpp
    simulation_behavior.cpp
    simulation_combat.cpp
    simulation_internal.hpp
    simulation_locomotion.cpp
    simulation_navigation.cpp
    spatial_grid.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_SIMULATION_HEADERS} ${EAW_SIMULATION_SOURCES})

add_library(eaw_simulation STATIC ${EAW_SIMULATION_HEADERS} ${EAW_SIMULATION_SOURCES})
target_include_directories(eaw_simulation PUBLIC include)
# Only for Status/Result and fatal errors; the simulation still reads no files.
target_link_libraries(eaw_simulation PUBLIC eaw_core)
