# EAWLauncher: chooses the game profile, installation, mods, and options, then starts EAWEngine.
set(EAW_LAUNCHER_APP_SOURCES
    main.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_LAUNCHER_APP_SOURCES})

add_executable(EAWLauncher ${EAW_LAUNCHER_APP_SOURCES})
target_link_libraries(EAWLauncher PRIVATE eaw_launch eaw_vfs)
# The engine's file name in this configuration (EAWEngine.exe, or EAWEngine_d.exe in Debug).
target_compile_definitions(EAWLauncher PRIVATE "EAW_ENGINE_FILE_NAME=\"$<TARGET_FILE_NAME:EAWEngine>\"")
add_dependencies(EAWLauncher EAWEngine)
