# Operating-system services: window and input, crash handling, log sinks (Win32).
set(EAW_PLATFORM_HEADERS
    include/eaw/crash_handler.hpp
    include/eaw/log_sinks.hpp
    include/eaw/platform_window.hpp
)

set(EAW_PLATFORM_SOURCES
    win32_crash_handler.cpp
    win32_log_sinks.cpp
    win32_window.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_PLATFORM_HEADERS} ${EAW_PLATFORM_SOURCES})

add_library(eaw_platform STATIC ${EAW_PLATFORM_HEADERS} ${EAW_PLATFORM_SOURCES})
eaw_target_defaults(eaw_platform "libraries")
target_include_directories(eaw_platform PUBLIC include)
target_link_libraries(eaw_platform PUBLIC eaw_core PRIVATE user32)
