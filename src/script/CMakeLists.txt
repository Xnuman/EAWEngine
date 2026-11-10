# eaw_script: the Lua host. One sandboxed Lua 5.0 state per script, script threads on simulation
# ticks, engine events and callbacks, values between the engine and scripts, and recording stubs for
# the engine functions not implemented yet. No graphics or platform code.

add_library(eaw_script STATIC)

set(HEADERS_DIR "include/script")
set(SOURCES_DIR "src")

set(EAW_SCRIPT_HEADERS
    ${HEADERS_DIR}/host_api_recorder.hpp
    ${HEADERS_DIR}/script_host.hpp
    ${HEADERS_DIR}/script_value.hpp
)

set(EAW_SCRIPT_SOURCES
    ${SOURCES_DIR}/host_api_recorder.cpp
    ${SOURCES_DIR}/script_events.cpp
    ${SOURCES_DIR}/script_host.cpp
    ${SOURCES_DIR}/script_host_internal.hpp
    ${SOURCES_DIR}/script_stubs.cpp
    ${SOURCES_DIR}/script_values.cpp
)

source_group("" FILES ${EAW_SCRIPT_HEADERS} ${EAW_SCRIPT_SOURCES})

target_sources(eaw_script
  PUBLIC
    FILE_SET "eaw_script_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_SCRIPT_HEADERS}
  PRIVATE
    ${EAW_SCRIPT_SOURCES}
)

target_link_libraries(eaw_script PUBLIC eaw_vfs PRIVATE eaw_lua)
