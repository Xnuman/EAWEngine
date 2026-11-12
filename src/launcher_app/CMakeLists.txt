# EAWLauncher: chooses the game profile, installation, mods, and options, then starts EAWEngine.

add_executable(EAWLauncher)

set(SOURCES_DIR "src")

set(EAW_LAUNCHER_APP_SOURCES
    ${SOURCES_DIR}/main.cpp
)

source_group("" FILES ${EAW_LAUNCHER_APP_SOURCES})

target_sources(EAWLauncher PRIVATE ${EAW_LAUNCHER_APP_SOURCES})

target_link_libraries(EAWLauncher PRIVATE eaw_launch eaw_vfs)
# The engine's file name in this configuration (EAWEngine.exe, or EAWEngine_d.exe in Debug).
target_compile_definitions(EAWLauncher PRIVATE "EAW_ENGINE_FILE_NAME=\"$<TARGET_FILE_NAME:EAWEngine>\"")
add_dependencies(EAWLauncher EAWEngine)
