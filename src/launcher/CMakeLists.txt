# Launch configuration and process start: sandbox scenarios and presets (portable), child processes
# (Windows).
set(EAW_LAUNCH_HEADERS
    include/eaw/child_process.hpp
    include/eaw/sandbox_scenario.hpp
)

set(EAW_LAUNCH_SOURCES
    sandbox_scenario.cpp
)
if(WIN32)
    list(APPEND EAW_LAUNCH_SOURCES child_process.cpp)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_LAUNCH_HEADERS} ${EAW_LAUNCH_SOURCES})

add_library(eaw_launch STATIC ${EAW_LAUNCH_HEADERS} ${EAW_LAUNCH_SOURCES})
target_include_directories(eaw_launch PUBLIC include)
target_link_libraries(eaw_launch PUBLIC eaw_core)
