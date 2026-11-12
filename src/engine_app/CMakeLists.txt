# EAWEngine: startup, discovery, the doctor probe, headless battles, and (Windows) the sandbox
# session, which drives the window and the D3D12 renderer.

add_executable(EAWEngine)

set(HEADERS_DIR "include/engine_app")
set(SOURCES_DIR "src")

set(EAW_ENGINE_APP_HEADERS
    ${HEADERS_DIR}/image_compare_command.hpp
    ${HEADERS_DIR}/lua_survey_command.hpp
)

set(EAW_ENGINE_APP_SOURCES
    ${SOURCES_DIR}/image_compare_command.cpp
    ${SOURCES_DIR}/lua_survey_command.cpp
    ${SOURCES_DIR}/main.cpp
)

if(WIN32)
    list(APPEND EAW_ENGINE_APP_HEADERS
        ${HEADERS_DIR}/frame_watchdog.hpp
        ${HEADERS_DIR}/sandbox_app.hpp
        ${HEADERS_DIR}/sandbox_overlay.hpp
        ${HEADERS_DIR}/sandbox_panels.hpp
        ${HEADERS_DIR}/sandbox_session.hpp
    )
    list(APPEND EAW_ENGINE_APP_SOURCES
        ${SOURCES_DIR}/frame_watchdog.cpp
        ${SOURCES_DIR}/sandbox_app.cpp
        ${SOURCES_DIR}/sandbox_overlay.cpp
        ${SOURCES_DIR}/sandbox_panels.cpp
        ${SOURCES_DIR}/sandbox_session_content.cpp
        ${SOURCES_DIR}/sandbox_session_input.cpp
        ${SOURCES_DIR}/sandbox_session_spawning.cpp
        ${SOURCES_DIR}/sandbox_session_ui.cpp
        ${SOURCES_DIR}/sandbox_session_world.cpp
    )
endif()

source_group("" FILES ${EAW_ENGINE_APP_HEADERS} ${EAW_ENGINE_APP_SOURCES})

target_sources(EAWEngine
  PRIVATE
    FILE_SET "eaw_engine_app_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_ENGINE_APP_HEADERS}
  PRIVATE
    ${EAW_ENGINE_APP_SOURCES}
)

target_link_libraries(EAWEngine PRIVATE eaw_assets eaw_audio_devices eaw_engine eaw_script)
if(WIN32)
    target_link_libraries(EAWEngine PRIVATE eaw_platform eaw_render_d3d12 eaw_imgui)
endif()
