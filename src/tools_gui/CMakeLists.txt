# EAWTools: the Dear ImGui / Direct3D 11 asset workbench.
set(EAW_TOOLS_SOURCES
    gui_app.cpp
    gui_app.hpp
    gui_asset_workspace.cpp
    gui_internal.hpp
    gui_localization.cpp
    gui_map_view.cpp
    gui_model_view.cpp
    gui_state.cpp
    gui_support.cpp
    main.cpp
    preview_renderer.cpp
    preview_renderer.hpp
)

set(EAW_TOOLS_SHADERS
    shaders/preview.hlsl
)
set_source_files_properties(${EAW_TOOLS_SHADERS} PROPERTIES VS_TOOL_OVERRIDE "None")

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_TOOLS_SOURCES} ${EAW_TOOLS_SHADERS})

add_executable(EAWTools WIN32 ${EAW_TOOLS_SOURCES} ${EAW_TOOLS_SHADERS})
eaw_target_defaults(EAWTools "tools")
target_link_libraries(EAWTools PRIVATE
    eaw_core
    eaw_imgui
    eaw_imgui_app
    eaw_platform
    d3d11
    dxgi
    dwmapi
    ole32
    shell32
    uuid
)

eaw_compile_shader(EAWTools fxc shaders/preview.hlsl vertex_main vs_5_0 g_eaw_preview_vs)
eaw_compile_shader(EAWTools fxc shaders/preview.hlsl pixel_main ps_5_0 g_eaw_preview_ps)
