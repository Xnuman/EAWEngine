# EAWLauncher: chooses the game profile, installation, mods, and options, then starts EAWEngine.
set(EAW_LAUNCHER_APP_SOURCES
    main.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_LAUNCHER_APP_SOURCES})

add_executable(EAWLauncher ${EAW_LAUNCHER_APP_SOURCES})
target_link_libraries(EAWLauncher PRIVATE eaw_launch eaw_vfs)
