# EAWTools: the Dear ImGui / Direct3D 11 asset workbench.

add_executable(EAWTools WIN32)

set(HEADERS_DIR "include/tools_gui")
set(SOURCES_DIR "src")

set(EAW_TOOLS_HEADERS
    ${HEADERS_DIR}/gui_app.hpp
    ${HEADERS_DIR}/gui_internal.hpp
    ${HEADERS_DIR}/preview_renderer.hpp
)

set(EAW_TOOLS_SOURCES
    ${SOURCES_DIR}/gui_app.cpp
    ${SOURCES_DIR}/gui_asset_workspace.cpp
    ${SOURCES_DIR}/gui_localization.cpp
    ${SOURCES_DIR}/gui_map_view.cpp
    ${SOURCES_DIR}/gui_model_view.cpp
    ${SOURCES_DIR}/gui_state.cpp
    ${SOURCES_DIR}/gui_support.cpp
    ${SOURCES_DIR}/main.cpp
    ${SOURCES_DIR}/preview_renderer.cpp
)

set(EAW_TOOLS_SHADERS
    shaders/preview.hlsl
)
set_source_files_properties(${EAW_TOOLS_SHADERS} PROPERTIES VS_TOOL_OVERRIDE "None")

source_group("" FILES ${EAW_TOOLS_HEADERS} ${EAW_TOOLS_SOURCES})
source_group("shaders" FILES ${EAW_TOOLS_SHADERS})

target_sources(EAWTools
  PRIVATE
    FILE_SET "eaw_tools_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_TOOLS_HEADERS}
  PRIVATE
    ${EAW_TOOLS_SOURCES}
    ${EAW_TOOLS_SHADERS}
)

target_link_libraries(EAWTools PRIVATE
    eaw_assets
    eaw_imgui
    eaw_imgui_app
    eaw_localization
    eaw_platform
    d3d11
    dwmapi
    dxgi
    ole32
    shell32
    uuid
)

eaw_compile_shader(EAWTools fxc shaders/preview.hlsl vertex_main vs_5_0 g_eaw_preview_vs)
eaw_compile_shader(EAWTools fxc shaders/preview.hlsl pixel_main ps_5_0 g_eaw_preview_ps)
