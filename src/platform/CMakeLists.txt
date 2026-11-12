# eaw_platform: operating-system services: window and input, crash handling, and log sinks (Win32).

add_library(eaw_platform STATIC)

set(HEADERS_DIR "include/platform")
set(SOURCES_DIR "src")

set(EAW_PLATFORM_HEADERS
    ${HEADERS_DIR}/crash_handler.hpp
    ${HEADERS_DIR}/log_sinks.hpp
    ${HEADERS_DIR}/platform_window.hpp
)

set(EAW_PLATFORM_SOURCES
    ${SOURCES_DIR}/win32_crash_handler.cpp
    ${SOURCES_DIR}/win32_log_sinks.cpp
    ${SOURCES_DIR}/win32_window.cpp
)

source_group("" FILES ${EAW_PLATFORM_HEADERS} ${EAW_PLATFORM_SOURCES})

target_sources(eaw_platform
  PUBLIC
    FILE_SET "eaw_platform_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_PLATFORM_HEADERS}
  PRIVATE
    ${EAW_PLATFORM_SOURCES}
)

target_link_libraries(eaw_platform PUBLIC eaw_core PRIVATE user32)
