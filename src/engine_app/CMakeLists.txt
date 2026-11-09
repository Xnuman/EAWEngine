# EAWEngine: startup, discovery, the doctor probe, headless battles, and (Windows) the sandbox
# session, which drives the window and the D3D12 renderer.
set(EAW_ENGINE_APP_SOURCES
    main.cpp
)
if(WIN32)
    list(APPEND EAW_ENGINE_APP_SOURCES
        sandbox_app.cpp
        sandbox_app.hpp
        sandbox_overlay.cpp
        sandbox_overlay.hpp
        sandbox_panels.cpp
        sandbox_panels.hpp
    )
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_ENGINE_APP_SOURCES})

add_executable(EAWEngine ${EAW_ENGINE_APP_SOURCES})
eaw_target_defaults(EAWEngine "programs")
target_link_libraries(EAWEngine PRIVATE eaw_core eaw_engine)
if(WIN32)
    target_link_libraries(EAWEngine PRIVATE eaw_platform eaw_render_d3d12 eaw_imgui)
endif()
