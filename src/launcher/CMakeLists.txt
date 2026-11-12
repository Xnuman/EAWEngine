# eaw_launcher: launch configuration and process start: sandbox scenarios and presets (portable),
# child processes (Windows).

add_library(eaw_launcher STATIC)

set(HEADERS_DIR "include/launcher")
set(SOURCES_DIR "src")

set(EAW_LAUNCHER_HEADERS
    ${HEADERS_DIR}/child_process.hpp
    ${HEADERS_DIR}/sandbox_scenario.hpp
)

set(EAW_LAUNCHER_SOURCES
    ${SOURCES_DIR}/sandbox_scenario.cpp
)
if(WIN32)
    list(APPEND EAW_LAUNCHER_SOURCES ${SOURCES_DIR}/child_process.cpp)
endif()

source_group("" FILES ${EAW_LAUNCHER_HEADERS} ${EAW_LAUNCHER_SOURCES})

target_sources(eaw_launcher
  PUBLIC
    FILE_SET "eaw_launcher_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_LAUNCHER_HEADERS}
  PRIVATE
    ${EAW_LAUNCHER_SOURCES}
)

target_link_libraries(eaw_launcher PUBLIC eaw_vfs)
