# eaw_render_contract: the render contract, engine-native visual data (header only, portable).
# eaw_render_core: rendering rules without a graphics API (detail-level selection, GPU resource
# lifetime), shared by backends and testable without a device (portable).
# eaw_render_d3d12: the Direct3D 12 backend (Windows).

set(HEADERS_DIR "include/render")
set(SOURCES_DIR "src")

add_library(eaw_render_contract INTERFACE)

set(EAW_RENDER_CONTRACT_HEADERS
    ${HEADERS_DIR}/color.hpp
    ${HEADERS_DIR}/visual_anti_aliasing.hpp
    ${HEADERS_DIR}/visual_camera.hpp
    ${HEADERS_DIR}/visual_frame.hpp
    ${HEADERS_DIR}/visual_lod.hpp
    ${HEADERS_DIR}/visual_material.hpp
    ${HEADERS_DIR}/visual_mesh.hpp
    ${HEADERS_DIR}/visual_scene.hpp
    ${HEADERS_DIR}/visual_texture.hpp
    ${HEADERS_DIR}/visual_vertex.hpp
)

source_group("" FILES ${EAW_RENDER_CONTRACT_HEADERS})

target_sources(eaw_render_contract
  INTERFACE
    FILE_SET "eaw_render_contract_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_RENDER_CONTRACT_HEADERS}
)

target_link_libraries(eaw_render_contract INTERFACE eaw_math)

add_library(eaw_render_core STATIC)

set(EAW_RENDER_CORE_HEADERS
    ${HEADERS_DIR}/frame_graph.hpp
    ${HEADERS_DIR}/gpu_resource_lifetime.hpp
    ${HEADERS_DIR}/lod_selection.hpp
    ${HEADERS_DIR}/shadow_cascades.hpp
)

set(EAW_RENDER_CORE_SOURCES
    ${SOURCES_DIR}/frame_graph.cpp
    ${SOURCES_DIR}/lod_selection.cpp
    ${SOURCES_DIR}/shadow_cascades.cpp
)

source_group("" FILES ${EAW_RENDER_CORE_HEADERS} ${EAW_RENDER_CORE_SOURCES})

target_sources(eaw_render_core
  PUBLIC
    FILE_SET "eaw_render_core_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_RENDER_CORE_HEADERS}
  PRIVATE
    ${EAW_RENDER_CORE_SOURCES}
)

target_link_libraries(eaw_render_core PUBLIC eaw_core eaw_render_contract)

if(NOT WIN32)
    return()
endif()

add_library(eaw_render_d3d12 STATIC)

set(EAW_RENDER_D3D12_HEADERS
    ${HEADERS_DIR}/d3d12_renderer.hpp
)

set(EAW_RENDER_D3D12_SOURCES
    ${SOURCES_DIR}/d3d12_renderer.cpp
    ${SOURCES_DIR}/d3d12_renderer_device.cpp
    ${SOURCES_DIR}/d3d12_renderer_frame.cpp
    ${SOURCES_DIR}/d3d12_renderer_impl.hpp
    ${SOURCES_DIR}/d3d12_renderer_overlay.cpp
    ${SOURCES_DIR}/d3d12_renderer_pipelines.cpp
    ${SOURCES_DIR}/d3d12_renderer_resources.cpp
    ${SOURCES_DIR}/d3d12_support.cpp
    ${SOURCES_DIR}/d3d12_support.hpp
)

set(EAW_RENDER_D3D12_SHADERS
    shaders/debug.hlsl
    shaders/legacy_common.hlsli
    shaders/particle.hlsl
    shaders/post.hlsl
    shaders/sandbox.hlsl
    shaders/terrain.hlsl
)
set_source_files_properties(${EAW_RENDER_D3D12_SHADERS} PROPERTIES VS_TOOL_OVERRIDE "None")

source_group("" FILES ${EAW_RENDER_D3D12_HEADERS} ${EAW_RENDER_D3D12_SOURCES})
source_group("shaders" FILES ${EAW_RENDER_D3D12_SHADERS})

target_sources(eaw_render_d3d12
  PUBLIC
    FILE_SET "eaw_render_d3d12_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_RENDER_D3D12_HEADERS}
  PRIVATE
    ${EAW_RENDER_D3D12_SOURCES}
    ${EAW_RENDER_D3D12_SHADERS}
)

target_link_libraries(eaw_render_d3d12 PUBLIC eaw_core eaw_formats eaw_render_contract PRIVATE eaw_render_core d3d12 dxgi eaw_imgui)

eaw_compile_shader(eaw_render_d3d12 dxc shaders/terrain.hlsl terrain_vs vs_6_0 g_eaw_terrain_vs)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/terrain.hlsl terrain_ps ps_6_0 g_eaw_terrain_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/terrain.hlsl terrain_shadow_vs vs_6_0 g_eaw_terrain_shadow_vs)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/terrain.hlsl terrain_shadow_ps ps_6_0 g_eaw_terrain_shadow_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/sandbox.hlsl vs_main vs_6_0 g_eaw_sandbox_vs)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/sandbox.hlsl ps_main ps_6_0 g_eaw_sandbox_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/sandbox.hlsl shadow_vs vs_6_0 g_eaw_shadow_vs)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/sandbox.hlsl shadow_ps ps_6_0 g_eaw_shadow_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/sandbox.hlsl water_ps ps_6_0 g_eaw_water_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/debug.hlsl debug_vs vs_6_0 g_eaw_debug_vs)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/debug.hlsl debug_ps ps_6_0 g_eaw_debug_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/particle.hlsl particle_vs vs_6_0 g_eaw_particle_vs)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/particle.hlsl particle_ps ps_6_0 g_eaw_particle_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/particle.hlsl particle_modulate_ps ps_6_0 g_eaw_particle_modulate_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/post.hlsl fullscreen_vs vs_6_0 g_eaw_post_vs)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/post.hlsl bloom_extract_ps ps_6_0 g_eaw_post_extract_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/post.hlsl bloom_blur_ps ps_6_0 g_eaw_post_blur_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/post.hlsl bloom_composite_ps ps_6_0 g_eaw_post_composite_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/post.hlsl edge_aa_ps ps_6_0 g_eaw_post_edge_aa_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/post.hlsl taa_resolve_ps ps_6_0 g_eaw_post_taa_ps)
