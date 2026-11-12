# EAWDevLauncher: edit, save, and start sandbox scenarios.

add_executable(EAWDevLauncher WIN32)

set(HEADERS_DIR "include/dev_launcher")
set(SOURCES_DIR "src")

set(EAW_DEV_LAUNCHER_HEADERS
    ${HEADERS_DIR}/dev_launcher_app.hpp
)

set(EAW_DEV_LAUNCHER_SOURCES
    ${SOURCES_DIR}/dev_launcher_app.cpp
    ${SOURCES_DIR}/main.cpp
)

source_group("" FILES ${EAW_DEV_LAUNCHER_HEADERS} ${EAW_DEV_LAUNCHER_SOURCES})

target_sources(EAWDevLauncher
  PRIVATE
    FILE_SET "eaw_dev_launcher_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_DEV_LAUNCHER_HEADERS}
  PRIVATE
    ${EAW_DEV_LAUNCHER_SOURCES}
)

target_link_libraries(EAWDevLauncher PRIVATE eaw_assets eaw_launch eaw_imgui_app eaw_imgui shell32)
# The engine's file name in this configuration (EAWEngine.exe, or EAWEngine_d.exe in Debug).
target_compile_definitions(EAWDevLauncher PRIVATE "EAW_ENGINE_FILE_NAME=\"$<TARGET_FILE_NAME:EAWEngine>\"")
add_dependencies(EAWDevLauncher EAWEngine)
