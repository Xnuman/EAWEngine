# eaw_sandbox: The sandbox's testable parts and the headless battle command: command-line options, background
# content builds and hot-reload checks, debug lines, number formatting, and headless runs.

add_library(eaw_sandbox STATIC)

set(HEADERS_DIR "include/sandbox")
set(SOURCES_DIR "src")

set(EAW_SANDBOX_HEADERS
    ${HEADERS_DIR}/battle_runner.hpp
    ${HEADERS_DIR}/sandbox_content.hpp
    ${HEADERS_DIR}/sandbox_debug_lines.hpp
    ${HEADERS_DIR}/sandbox_options.hpp
    ${HEADERS_DIR}/text_format.hpp
)

set(EAW_SANDBOX_SOURCES
    ${SOURCES_DIR}/battle_runner.cpp
    ${SOURCES_DIR}/sandbox_content.cpp
    ${SOURCES_DIR}/sandbox_debug_lines.cpp
    ${SOURCES_DIR}/sandbox_options.cpp
    ${SOURCES_DIR}/text_format.cpp
)

source_group( "" FILES ${EAW_SANDBOX_SOURCES} ${EAW_SANDBOX_HEADERS} )

target_sources(eaw_sandbox
  PUBLIC
    FILE_SET "eaw_sandbox_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_SANDBOX_HEADERS}
  PRIVATE
    ${EAW_SANDBOX_SOURCES}
)

target_link_libraries(eaw_sandbox PUBLIC eaw_battle eaw_control eaw_presentation eaw_scene)
