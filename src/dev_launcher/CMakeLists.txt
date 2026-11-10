# EAWDevLauncher: edit, save, and start sandbox scenarios.
set(EAW_DEV_LAUNCHER_SOURCES
    dev_launcher_app.cpp
    dev_launcher_app.hpp
    main.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_DEV_LAUNCHER_SOURCES})

add_executable(EAWDevLauncher WIN32 ${EAW_DEV_LAUNCHER_SOURCES})
target_link_libraries(EAWDevLauncher PRIVATE eaw_assets eaw_launch eaw_imgui_app eaw_imgui shell32)
