cmake_minimum_required(VERSION 3.25)

project(EAWEngine VERSION 0.1.0 LANGUAGES CXX)

# Targets sit in solution folders, and each target lists its headers beside its sources so Visual
# Studio shows them; source_group(TREE) mirrors the directory layout in the project.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(EAW_BUILD_TESTS "Build EAWEngine tests" ON)
option(EAW_BUILD_GUI "Build the Dear ImGui tool workbench" ON)

# EAWEngine does not use C++ exceptions: recoverable failures return eaw::Status/eaw::Result and
# broken invariants end the process through eaw::fatal_error. Every target, Dear ImGui included,
# compiles with exception handling disabled (/EHs-c-) and a standard library configured without
# exceptions (_HAS_EXCEPTIONS=0). CMake's default MSVC flags enable, so it is removed here.
if(MSVC)
    string(REGEX REPLACE "/EH[a-z]*" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    add_compile_options(/EHs-c-)
    add_compile_definitions(_HAS_EXCEPTIONS=0)
endif()

set(EAW_CORE_HEADERS
    src/core/include/eaw/ala_animation.hpp
    src/core/include/eaw/alo_model.hpp
    src/core/include/eaw/asset_environment.hpp
    src/core/include/eaw/asset_path.hpp
    src/core/include/eaw/dat_file.hpp
    src/core/include/eaw/dds_texture.hpp
    src/core/include/eaw/fatal_error.hpp
    src/core/include/eaw/game_installation.hpp
    src/core/include/eaw/game_object_catalog.hpp
    src/core/include/eaw/legacy_material.hpp
    src/core/include/eaw/localization_scan.hpp
    src/core/include/eaw/localization_workspace.hpp
    src/core/include/eaw/log.hpp
    src/core/include/eaw/lua_script.hpp
    src/core/include/eaw/map_object_layout.hpp
    src/core/include/eaw/meg_archive.hpp
    src/core/include/eaw/mtd_directory.hpp
    src/core/include/eaw/particle_system.hpp
    src/core/include/eaw/spawn_catalog.hpp
    src/core/include/eaw/status.hpp
    src/core/include/eaw/ted_map.hpp
    src/core/include/eaw/text_encoding.hpp
    src/core/include/eaw/virtual_file_system.hpp
    src/core/include/eaw/xml_asset_database.hpp
    src/core/include/eaw/xml_document.hpp
    src/core/include/eaw/xml_schema.hpp
)

set(EAW_CORE_SOURCES
    src/core/ala_animation.cpp
    src/core/alo_model.cpp
    src/core/asset_environment.cpp
    src/core/asset_path.cpp
    src/core/dat_file.cpp
    src/core/dds_texture.cpp
    src/core/fatal_error.cpp
    src/core/game_installation.cpp
    src/core/game_object_catalog.cpp
    src/core/legacy_material.cpp
    src/core/localization_scan.cpp
    src/core/localization_workspace.cpp
    src/core/log.cpp
    src/core/lua_script.cpp
    src/core/map_object_layout.cpp
    src/core/meg_archive.cpp
    src/core/mtd_directory.cpp
    src/core/particle_system.cpp
    src/core/spawn_catalog.cpp
    src/core/ted_map.cpp
    src/core/text_encoding.cpp
    src/core/virtual_file_system.cpp
    src/core/xml_asset_database.cpp
    src/core/xml_document.cpp
    src/core/xml_schema.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src/core FILES ${EAW_CORE_HEADERS} ${EAW_CORE_SOURCES})
add_library(eaw_core STATIC ${EAW_CORE_HEADERS} ${EAW_CORE_SOURCES})
set_target_properties(eaw_core PROPERTIES FOLDER "libraries")

target_include_directories(eaw_core PUBLIC src/core/include)
target_compile_features(eaw_core PUBLIC cxx_std_20)
target_compile_definitions(eaw_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(eaw_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(eaw_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    # Shaders are compiled offline into generated headers that embed the bytecode, so runtime
    # never invokes a shader compiler. DXC (shader model 6) serves Direct3D 12 and FXC (shader
    # model 5) the Direct3D 11 tool previews. Both ship with the Windows SDK selected for the
    # build; set EAW_DXC_EXECUTABLE / EAW_FXC_EXECUTABLE to override.
    get_filename_component(EAW_WINDOWS_KITS_ROOT "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
    set(EAW_WINDOWS_SDK_BIN_HINTS
        "${EAW_WINDOWS_KITS_ROOT}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        "${EAW_WINDOWS_KITS_ROOT}/bin/x64"
    )
    find_program(EAW_DXC_EXECUTABLE dxc HINTS ${EAW_WINDOWS_SDK_BIN_HINTS})
    find_program(EAW_FXC_EXECUTABLE fxc HINTS ${EAW_WINDOWS_SDK_BIN_HINTS})
    if(NOT EAW_DXC_EXECUTABLE OR NOT EAW_FXC_EXECUTABLE)
        message(FATAL_ERROR "dxc.exe and fxc.exe were not found in the Windows SDK (looked in ${EAW_WINDOWS_SDK_BIN_HINTS}). "
                            "Install a Windows 10/11 SDK or set EAW_DXC_EXECUTABLE and EAW_FXC_EXECUTABLE.")
    endif()
    set(EAW_GENERATED_SHADER_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
    file(MAKE_DIRECTORY "${EAW_GENERATED_SHADER_DIR}")

    # eaw_compile_shader(<target> <dxc|fxc> <source> <entry> <profile> <symbol>) compiles one
    # entry point into ${EAW_GENERATED_SHADER_DIR}/<symbol>.h (a byte array named <symbol>).
    function(eaw_compile_shader target compiler source entry profile symbol)
        set(output "${EAW_GENERATED_SHADER_DIR}/${symbol}.h")
        set(input "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
        get_filename_component(include_dir "${input}" DIRECTORY)
        file(GLOB shader_headers CONFIGURE_DEPENDS "${include_dir}/*.hlsli")
        if(compiler STREQUAL "dxc")
            add_custom_command(
                OUTPUT "${output}"
                COMMAND "${EAW_DXC_EXECUTABLE}" -nologo -T ${profile} -E ${entry} -HV 2021 -WX -O3 -force-rootsig-ver rootsig_1_0 -Qstrip_debug -Qstrip_reflect
                        -I "${include_dir}" -Fh "${output}" -Vn ${symbol} "${input}"
                DEPENDS "${input}" ${shader_headers}
                COMMENT "DXC ${source} (${entry}, ${profile})"
                VERBATIM)
        else()
            add_custom_command(
                OUTPUT "${output}"
                COMMAND "${EAW_FXC_EXECUTABLE}" /nologo /T ${profile} /E ${entry} /WX /O3 /I "${include_dir}" /Fh "${output}" /Vn ${symbol} "${input}"
                DEPENDS "${input}" ${shader_headers}
                COMMENT "FXC ${source} (${entry}, ${profile})"
                VERBATIM)
        endif()
        target_sources(${target} PRIVATE "${output}")
        source_group("generated" FILES "${output}")
        target_include_directories(${target} PRIVATE "${EAW_GENERATED_SHADER_DIR}")
    endfunction()
endif()

# Dear ImGui (pinned) serves the EAWTools workbench and the D3D12 renderer's debug overlay.
if(WIN32)
    include(FetchContent)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG 5c7c293a66d7a659d4e6a1abdf18291ce004c07e
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(imgui)

    add_library(eaw_imgui STATIC
        ${imgui_SOURCE_DIR}/imconfig.h
        ${imgui_SOURCE_DIR}/imgui.h
        ${imgui_SOURCE_DIR}/imgui_internal.h
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.h
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.h
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.h
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.h
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    )
    target_include_directories(eaw_imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${imgui_SOURCE_DIR}/misc/cpp
    )
    target_compile_features(eaw_imgui PUBLIC cxx_std_20)
    set_target_properties(eaw_imgui PROPERTIES FOLDER "third_party")
    target_compile_definitions(eaw_imgui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(eaw_imgui PRIVATE /W3 /utf-8)
    endif()
endif()

if(EAW_BUILD_GUI AND WIN32)
    # Shared Win32 + Direct3D 11 + Dear ImGui shell and file dialogs for the desktop tools.
    set(EAW_IMGUI_APP_SOURCES
        src/imgui_app/file_dialogs.cpp
        src/imgui_app/imgui_app.cpp
        src/imgui_app/include/eaw/file_dialogs.hpp
        src/imgui_app/include/eaw/imgui_app.hpp
    )
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_app FILES ${EAW_IMGUI_APP_SOURCES})
    add_library(eaw_imgui_app STATIC ${EAW_IMGUI_APP_SOURCES})
    set_target_properties(eaw_imgui_app PROPERTIES FOLDER "libraries")
    target_include_directories(eaw_imgui_app PUBLIC src/imgui_app/include)
    target_compile_features(eaw_imgui_app PUBLIC cxx_std_20)
    target_compile_definitions(eaw_imgui_app PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(eaw_imgui_app PUBLIC eaw_core eaw_imgui PRIVATE eaw_platform d3d11 dxgi ole32 shell32 uuid)
    if(MSVC)
        target_compile_options(eaw_imgui_app PRIVATE /W4 /permissive- /utf-8)
    endif()

    set(EAW_TOOLS_SOURCES
        src/tools_gui/gui_app.cpp
        src/tools_gui/gui_app.hpp
        src/tools_gui/gui_asset_workspace.cpp
        src/tools_gui/gui_internal.hpp
        src/tools_gui/gui_localization.cpp
        src/tools_gui/gui_map_view.cpp
        src/tools_gui/gui_model_view.cpp
        src/tools_gui/gui_state.cpp
        src/tools_gui/gui_support.cpp
        src/tools_gui/main.cpp
        src/tools_gui/preview_renderer.cpp
        src/tools_gui/preview_renderer.hpp
        src/tools_gui/shaders/preview.hlsl
    )
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src/tools_gui FILES ${EAW_TOOLS_SOURCES})
    add_executable(EAWTools WIN32 ${EAW_TOOLS_SOURCES})
    set_target_properties(EAWTools PROPERTIES FOLDER "tools")
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
    set_source_files_properties(src/tools_gui/shaders/preview.hlsl PROPERTIES VS_TOOL_OVERRIDE "None")
    eaw_compile_shader(EAWTools fxc src/tools_gui/shaders/preview.hlsl vertex_main vs_5_0 g_eaw_preview_vs)
    eaw_compile_shader(EAWTools fxc src/tools_gui/shaders/preview.hlsl pixel_main ps_5_0 g_eaw_preview_ps)
    target_compile_features(EAWTools PRIVATE cxx_std_20)
    target_compile_definitions(EAWTools PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(EAWTools PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()

if(WIN32)
    target_link_libraries(eaw_core PRIVATE advapi32)

    set(EAW_PLATFORM_SOURCES
        src/platform/include/eaw/crash_handler.hpp
        src/platform/include/eaw/log_sinks.hpp
        src/platform/include/eaw/platform_window.hpp
        src/platform/win32_crash_handler.cpp
        src/platform/win32_log_sinks.cpp
        src/platform/win32_window.cpp
    )
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform FILES ${EAW_PLATFORM_SOURCES})
    add_library(eaw_platform STATIC ${EAW_PLATFORM_SOURCES})
    set_target_properties(eaw_platform PROPERTIES FOLDER "libraries")
    target_include_directories(eaw_platform PUBLIC src/platform/include)
    target_compile_features(eaw_platform PUBLIC cxx_std_20)
    target_compile_definitions(eaw_platform PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(eaw_platform PUBLIC eaw_core PRIVATE user32)

    set(EAW_RENDER_D3D12_SOURCES
        src/render/d3d12_renderer.cpp
        src/render/d3d12_renderer_device.cpp
        src/render/d3d12_renderer_frame.cpp
        src/render/d3d12_renderer_impl.hpp
        src/render/d3d12_renderer_overlay.cpp
        src/render/d3d12_renderer_pipelines.cpp
        src/render/d3d12_renderer_resources.cpp
        src/render/d3d12_support.cpp
        src/render/d3d12_support.hpp
        src/render/include/eaw/d3d12_renderer.hpp
        src/render/include/eaw/gpu_resource_lifetime.hpp
        src/render/include/eaw/visual_scene.hpp
    )
    set(EAW_RENDER_D3D12_SHADERS
        src/render/shaders/debug.hlsl
        src/render/shaders/legacy_common.hlsli
        src/render/shaders/particle.hlsl
        src/render/shaders/post.hlsl
        src/render/shaders/sandbox.hlsl
        src/render/shaders/terrain.hlsl
    )
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src/render FILES ${EAW_RENDER_D3D12_SOURCES} ${EAW_RENDER_D3D12_SHADERS})
    add_library(eaw_render_d3d12 STATIC ${EAW_RENDER_D3D12_SOURCES} ${EAW_RENDER_D3D12_SHADERS})
    set_target_properties(eaw_render_d3d12 PROPERTIES FOLDER "libraries")
    target_include_directories(eaw_render_d3d12 PUBLIC src/render/include)
    target_compile_features(eaw_render_d3d12 PUBLIC cxx_std_20)
    target_compile_definitions(eaw_render_d3d12 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(eaw_render_d3d12 PUBLIC eaw_core PRIVATE d3d12 dxgi eaw_imgui)
    set_source_files_properties(${EAW_RENDER_D3D12_SHADERS} PROPERTIES VS_TOOL_OVERRIDE "None")
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/terrain.hlsl terrain_vs vs_6_0 g_eaw_terrain_vs)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/terrain.hlsl terrain_ps ps_6_0 g_eaw_terrain_ps)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/sandbox.hlsl vs_main vs_6_0 g_eaw_sandbox_vs)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/sandbox.hlsl ps_main ps_6_0 g_eaw_sandbox_ps)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/debug.hlsl debug_vs vs_6_0 g_eaw_debug_vs)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/debug.hlsl debug_ps ps_6_0 g_eaw_debug_ps)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/particle.hlsl particle_vs vs_6_0 g_eaw_particle_vs)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/particle.hlsl particle_ps ps_6_0 g_eaw_particle_ps)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/particle.hlsl particle_modulate_ps ps_6_0 g_eaw_particle_modulate_ps)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/post.hlsl fullscreen_vs vs_6_0 g_eaw_post_vs)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/post.hlsl bloom_extract_ps ps_6_0 g_eaw_post_extract_ps)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/post.hlsl bloom_blur_ps ps_6_0 g_eaw_post_blur_ps)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/post.hlsl bloom_composite_ps ps_6_0 g_eaw_post_composite_ps)

    if(MSVC)
        target_compile_options(eaw_platform PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(eaw_render_d3d12 PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()

# Deterministic tactical simulation core: no graphics, platform, or file access.
set(EAW_SIMULATION_SOURCES
    src/simulation/combat.cpp
    src/simulation/formation.cpp
    src/simulation/ground_map.cpp
    src/simulation/include/eaw/combat.hpp
    src/simulation/include/eaw/formation.hpp
    src/simulation/include/eaw/ground_map.hpp
    src/simulation/include/eaw/pathfinding.hpp
    src/simulation/include/eaw/simulation.hpp
    src/simulation/include/eaw/simulation_types.hpp
    src/simulation/include/eaw/spatial_grid.hpp
    src/simulation/pathfinding.cpp
    src/simulation/simulation.cpp
    src/simulation/simulation_behavior.cpp
    src/simulation/simulation_combat.cpp
    src/simulation/simulation_internal.hpp
    src/simulation/simulation_locomotion.cpp
    src/simulation/simulation_navigation.cpp
    src/simulation/spatial_grid.cpp
)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src/simulation FILES ${EAW_SIMULATION_SOURCES})
add_library(eaw_simulation STATIC ${EAW_SIMULATION_SOURCES})
set_target_properties(eaw_simulation PROPERTIES FOLDER "libraries")
target_include_directories(eaw_simulation PUBLIC src/simulation/include)
target_compile_features(eaw_simulation PUBLIC cxx_std_20)
# Only for Status/Result and fatal errors; the simulation still reads no files.
target_link_libraries(eaw_simulation PUBLIC eaw_core)
if(MSVC)
    target_compile_options(eaw_simulation PRIVATE /W4 /permissive- /utf-8)
endif()

# Platform-independent engine orchestration: visual scene construction and camera control. It
# consumes the render contract headers (engine-native data) but no graphics API.
set(EAW_ENGINE_SOURCES
    src/engine/asset_cache.cpp
    src/engine/asset_cache.hpp
    src/engine/attached_effects.cpp
    src/engine/attached_effects.hpp
    src/engine/battle.cpp
    src/engine/battle.hpp
    src/engine/battle_log.cpp
    src/engine/battle_log.hpp
    src/engine/battle_runner.cpp
    src/engine/battle_runner.hpp
    src/engine/camera_controller.cpp
    src/engine/camera_controller.hpp
    src/engine/camera_math.cpp
    src/engine/camera_math.hpp
    src/engine/combat_content.cpp
    src/engine/combat_content.hpp
    src/engine/debug_draw.cpp
    src/engine/debug_draw.hpp
    src/engine/object_type_properties.cpp
    src/engine/particle_effects.cpp
    src/engine/particle_effects.hpp
    src/engine/particle_rules.cpp
    src/engine/particle_rules.hpp
    src/engine/projectile_visuals.cpp
    src/engine/projectile_visuals.hpp
    src/engine/rts_control.cpp
    src/engine/rts_control.hpp
    src/engine/sandbox_content.cpp
    src/engine/sandbox_content.hpp
    src/engine/sandbox_debug_lines.cpp
    src/engine/sandbox_debug_lines.hpp
    src/engine/sandbox_options.cpp
    src/engine/sandbox_options.hpp
    src/engine/scene_builder_internal.hpp
    src/engine/scene_materials.cpp
    src/engine/scene_models.cpp
    src/engine/scene_particles.cpp
    src/engine/scene_presentation.cpp
    src/engine/scene_presentation.hpp
    src/engine/scene_terrain.cpp
    src/engine/terrain_height.cpp
    src/engine/terrain_height.hpp
    src/engine/text_format.cpp
    src/engine/text_format.hpp
    src/engine/unit_animation.cpp
    src/engine/unit_animation.hpp
    src/engine/visual_map_builder.cpp
    src/engine/visual_scene_builder.hpp
    src/render/include/eaw/visual_scene.hpp
)
# The render contract header (engine-native visual data) is shown under the engine too.
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src FILES ${EAW_ENGINE_SOURCES})
add_library(eaw_engine STATIC ${EAW_ENGINE_SOURCES})
set_target_properties(eaw_engine PROPERTIES FOLDER "libraries")
target_include_directories(eaw_engine PUBLIC src/engine src/render/include)
target_compile_features(eaw_engine PUBLIC cxx_std_20)
target_compile_definitions(eaw_engine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(eaw_engine PUBLIC eaw_core eaw_simulation)

set(EAW_ENGINE_EXECUTABLE_SOURCES src/engine/main.cpp)
if(WIN32)
    # The sandbox session drives the window and the D3D12 renderer, so it lives in the executable.
    list(APPEND EAW_ENGINE_EXECUTABLE_SOURCES
        src/engine/sandbox_app.cpp
        src/engine/sandbox_app.hpp
        src/engine/sandbox_overlay.cpp
        src/engine/sandbox_overlay.hpp
        src/engine/sandbox_panels.cpp
        src/engine/sandbox_panels.hpp
    )
endif()
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src/engine FILES ${EAW_ENGINE_EXECUTABLE_SOURCES})
add_executable(EAWEngine ${EAW_ENGINE_EXECUTABLE_SOURCES})
set_target_properties(EAWEngine PROPERTIES FOLDER "programs")
target_link_libraries(EAWEngine PRIVATE eaw_core eaw_engine)
if(WIN32)
    target_link_libraries(EAWEngine PRIVATE eaw_platform eaw_render_d3d12 eaw_imgui)
endif()
target_compile_features(EAWEngine PRIVATE cxx_std_20)
target_compile_definitions(EAWEngine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

# Launch configuration and process start: sandbox scenarios, presets, and spawnable types
# (portable), child processes (Windows).
set(EAW_LAUNCH_SOURCES
    src/launcher/include/eaw/child_process.hpp
    src/launcher/include/eaw/sandbox_scenario.hpp
    src/launcher/sandbox_scenario.cpp
)
if(WIN32)
    list(APPEND EAW_LAUNCH_SOURCES src/launcher/child_process.cpp)
endif()
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src/launcher FILES ${EAW_LAUNCH_SOURCES})
add_library(eaw_launch STATIC ${EAW_LAUNCH_SOURCES})
set_target_properties(eaw_launch PROPERTIES FOLDER "libraries")
target_include_directories(eaw_launch PUBLIC src/launcher/include)
target_compile_features(eaw_launch PUBLIC cxx_std_20)
target_compile_definitions(eaw_launch PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(eaw_launch PUBLIC eaw_core)

if(WIN32)
    add_executable(EAWLauncher src/launcher/main.cpp)
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src/launcher FILES src/launcher/main.cpp)
    set_target_properties(EAWLauncher PROPERTIES FOLDER "programs")
    target_link_libraries(EAWLauncher PRIVATE eaw_core eaw_launch)
    target_compile_features(EAWLauncher PRIVATE cxx_std_20)
    target_compile_definitions(EAWLauncher PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(EAWLauncher PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()

if(EAW_BUILD_GUI AND WIN32)
    # Development launcher: edit, save, and start sandbox scenarios.
    set(EAW_DEV_LAUNCHER_SOURCES
        src/dev_launcher/dev_launcher_app.cpp
        src/dev_launcher/dev_launcher_app.hpp
        src/dev_launcher/main.cpp
    )
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src/dev_launcher FILES ${EAW_DEV_LAUNCHER_SOURCES})
    add_executable(EAWDevLauncher WIN32 ${EAW_DEV_LAUNCHER_SOURCES})
    set_target_properties(EAWDevLauncher PROPERTIES FOLDER "programs")
    target_link_libraries(EAWDevLauncher PRIVATE eaw_core eaw_launch eaw_imgui_app eaw_imgui shell32)
    target_compile_features(EAWDevLauncher PRIVATE cxx_std_20)
    target_compile_definitions(EAWDevLauncher PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(EAWDevLauncher PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()

set(EAW_ASSET_INSPECTOR_SOURCES
    src/asset_inspector/audit_lua.cpp
    src/asset_inspector/audit_maps.cpp
    src/asset_inspector/audit_particles.cpp
    src/asset_inspector/audit_textures.cpp
    src/asset_inspector/audit_xml.cpp
    src/asset_inspector/inspect_asset.cpp
    src/asset_inspector/inspector.hpp
    src/asset_inspector/inspector_common.cpp
    src/asset_inspector/inspector_options.cpp
    src/asset_inspector/main.cpp
)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src/asset_inspector FILES ${EAW_ASSET_INSPECTOR_SOURCES})
add_executable(EAWAssetInspector ${EAW_ASSET_INSPECTOR_SOURCES})
set_target_properties(EAWAssetInspector PROPERTIES FOLDER "tools")
target_link_libraries(EAWAssetInspector PRIVATE eaw_core)
target_compile_features(EAWAssetInspector PRIVATE cxx_std_20)

add_executable(EAWDatEditor src/dat_editor/main.cpp)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src/dat_editor FILES src/dat_editor/main.cpp)
set_target_properties(EAWDatEditor PROPERTIES FOLDER "tools")
target_link_libraries(EAWDatEditor PRIVATE eaw_core)
target_compile_features(EAWDatEditor PRIVATE cxx_std_20)
target_compile_definitions(EAWDatEditor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(eaw_engine PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(EAWEngine PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(eaw_launch PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(EAWAssetInspector PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(EAWDatEditor PRIVATE /W4 /permissive- /utf-8)
endif()

if(EAW_BUILD_TESTS)
    enable_testing()
    # Each module is tests/test_<module>.cpp, registers its tests under that name, and gets its own
    # ctest entry; add new modules here.
    set(EAW_TEST_MODULES engine formats foundation launcher render simulation vfs xml)
    set(EAW_TEST_SOURCES tests/test_main.cpp tests/test_support.cpp tests/test_support.hpp)
    foreach(test_module IN LISTS EAW_TEST_MODULES)
        list(APPEND EAW_TEST_SOURCES tests/test_${test_module}.cpp)
    endforeach()
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/tests FILES ${EAW_TEST_SOURCES})
    add_executable(EAWEngineTests ${EAW_TEST_SOURCES})
    set_target_properties(EAWEngineTests PROPERTIES FOLDER "tests")
    target_link_libraries(EAWEngineTests PRIVATE eaw_core eaw_engine eaw_launch)
    target_compile_features(EAWEngineTests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(EAWEngineTests PRIVATE /W4 /permissive- /utf-8)
    endif()
    # EAWEngineTests runs every module when given none.
    foreach(test_module IN LISTS EAW_TEST_MODULES)
        add_test(NAME EAWEngineTests.${test_module} COMMAND EAWEngineTests ${test_module})
    endforeach()
endif()

