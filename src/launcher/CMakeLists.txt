# eaw_launch: launch configuration and process start: sandbox scenarios and presets (portable),
# child processes (Windows).

add_library(eaw_launch STATIC)

set(HEADERS_DIR "include/launcher")
set(SOURCES_DIR "src")

set(EAW_LAUNCH_HEADERS
    ${HEADERS_DIR}/child_process.hpp
    ${HEADERS_DIR}/sandbox_scenario.hpp
)

set(EAW_LAUNCH_SOURCES
    ${SOURCES_DIR}/sandbox_scenario.cpp
)
if(WIN32)
    list(APPEND EAW_LAUNCH_SOURCES ${SOURCES_DIR}/child_process.cpp)
endif()

source_group("" FILES ${EAW_LAUNCH_HEADERS} ${EAW_LAUNCH_SOURCES})

target_sources(eaw_launch
  PUBLIC
    FILE_SET "eaw_launch_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_LAUNCH_HEADERS}
  PRIVATE
    ${EAW_LAUNCH_SOURCES}
)

target_link_libraries(eaw_launch PUBLIC eaw_vfs)
