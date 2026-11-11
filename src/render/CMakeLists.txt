# The render contract (engine-native visual data, header only, portable) and the Direct3D 12
# backend (Windows).
set(EAW_RENDER_CONTRACT_HEADERS
    include/eaw/visual_lod.hpp
    include/eaw/visual_scene.hpp
)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_RENDER_CONTRACT_HEADERS})

add_library(eaw_render_contract INTERFACE ${EAW_RENDER_CONTRACT_HEADERS})
target_include_directories(eaw_render_contract INTERFACE include)
target_link_libraries(eaw_render_contract INTERFACE eaw_math)

if(NOT WIN32)
    return()
endif()

set(EAW_RENDER_D3D12_HEADERS
    include/eaw/d3d12_renderer.hpp
    include/eaw/gpu_resource_lifetime.hpp
)

set(EAW_RENDER_D3D12_SOURCES
    d3d12_renderer.cpp
    d3d12_renderer_device.cpp
    d3d12_renderer_frame.cpp
    d3d12_renderer_impl.hpp
    d3d12_renderer_overlay.cpp
    d3d12_renderer_pipelines.cpp
    d3d12_renderer_resources.cpp
    d3d12_support.cpp
    d3d12_support.hpp
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

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_RENDER_D3D12_HEADERS} ${EAW_RENDER_D3D12_SOURCES} ${EAW_RENDER_D3D12_SHADERS})

add_library(eaw_render_d3d12 STATIC ${EAW_RENDER_D3D12_HEADERS} ${EAW_RENDER_D3D12_SOURCES} ${EAW_RENDER_D3D12_SHADERS})
target_include_directories(eaw_render_d3d12 PUBLIC include)
target_link_libraries(eaw_render_d3d12 PUBLIC eaw_core eaw_formats eaw_render_contract PRIVATE d3d12 dxgi eaw_imgui)

eaw_compile_shader(eaw_render_d3d12 dxc shaders/terrain.hlsl terrain_vs vs_6_0 g_eaw_terrain_vs)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/terrain.hlsl terrain_ps ps_6_0 g_eaw_terrain_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/sandbox.hlsl vs_main vs_6_0 g_eaw_sandbox_vs)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/sandbox.hlsl ps_main ps_6_0 g_eaw_sandbox_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/debug.hlsl debug_vs vs_6_0 g_eaw_debug_vs)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/debug.hlsl debug_ps ps_6_0 g_eaw_debug_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/particle.hlsl particle_vs vs_6_0 g_eaw_particle_vs)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/particle.hlsl particle_ps ps_6_0 g_eaw_particle_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/particle.hlsl particle_modulate_ps ps_6_0 g_eaw_particle_modulate_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/post.hlsl fullscreen_vs vs_6_0 g_eaw_post_vs)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/post.hlsl bloom_extract_ps ps_6_0 g_eaw_post_extract_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/post.hlsl bloom_blur_ps ps_6_0 g_eaw_post_blur_ps)
eaw_compile_shader(eaw_render_d3d12 dxc shaders/post.hlsl bloom_composite_ps ps_6_0 g_eaw_post_composite_ps)
