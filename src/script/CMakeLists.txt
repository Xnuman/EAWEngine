# The Lua script host: one sandboxed Lua 5.0 state per script, script threads on simulation ticks,
# and recording stubs for the engine functions not implemented yet. No graphics or platform code.
set(EAW_SCRIPT_HEADERS
    include/eaw/host_api_recorder.hpp
    include/eaw/script_host.hpp
)

set(EAW_SCRIPT_SOURCES
    host_api_recorder.cpp
    script_host.cpp
    script_host_internal.hpp
    script_stubs.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_SCRIPT_HEADERS} ${EAW_SCRIPT_SOURCES})

add_library(eaw_script STATIC ${EAW_SCRIPT_HEADERS} ${EAW_SCRIPT_SOURCES})
eaw_target_defaults(eaw_script "libraries")
target_include_directories(eaw_script PUBLIC include)
target_link_libraries(eaw_script PUBLIC eaw_core PRIVATE eaw_lua)
