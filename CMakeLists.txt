cmake_minimum_required(VERSION 3.25)

project(EAWEngine VERSION 0.1.0 LANGUAGES CXX)

option(EAW_BUILD_TESTS "Build EAWEngine tests" ON)
option(EAW_BUILD_GUI "Build the Dear ImGui tool workbench" ON)

add_library(eaw_core STATIC
    src/core/ala_animation.cpp
    src/core/alo_model.cpp
    src/core/asset_path.cpp
    src/core/asset_environment.cpp
    src/core/dat_file.cpp
    src/core/dds_texture.cpp
    src/core/game_installation.cpp
    src/core/game_object_catalog.cpp
    src/core/legacy_material.cpp
    src/core/localization_scan.cpp
    src/core/localization_workspace.cpp
    src/core/map_object_layout.cpp
    src/core/lua_script.cpp
    src/core/meg_archive.cpp
    src/core/mtd_directory.cpp
    src/core/text_encoding.cpp
    src/core/ted_map.cpp
    src/core/virtual_file_system.cpp
    src/core/xml_asset_database.cpp
    src/core/xml_document.cpp
    src/core/xml_schema.cpp
)

target_include_directories(eaw_core PUBLIC src/core/include)
target_compile_features(eaw_core PUBLIC cxx_std_20)
target_compile_definitions(eaw_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(eaw_core PRIVATE /W4 /permissive- /EHsc /utf-8)
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
    target_compile_definitions(eaw_imgui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(eaw_imgui PRIVATE /W3 /EHsc /utf-8)
    endif()
endif()

if(EAW_BUILD_GUI AND WIN32)
    add_executable(EAWTools WIN32
        src/tools_gui/file_dialogs.cpp
        src/tools_gui/gui_app.cpp
        src/tools_gui/preview_renderer.cpp
        src/tools_gui/main.cpp
    )
    target_link_libraries(EAWTools PRIVATE
        eaw_core
        eaw_imgui
        d3d11
        dxgi
        dwmapi
        ole32
        shell32
        uuid
    )
    target_sources(EAWTools PRIVATE src/tools_gui/shaders/preview.hlsl)
    set_source_files_properties(src/tools_gui/shaders/preview.hlsl PROPERTIES VS_TOOL_OVERRIDE "None")
    eaw_compile_shader(EAWTools fxc src/tools_gui/shaders/preview.hlsl vertex_main vs_5_0 g_eaw_preview_vs)
    eaw_compile_shader(EAWTools fxc src/tools_gui/shaders/preview.hlsl pixel_main ps_5_0 g_eaw_preview_ps)
    target_compile_features(EAWTools PRIVATE cxx_std_20)
    target_compile_definitions(EAWTools PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(EAWTools PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
endif()

if(WIN32)
    target_link_libraries(eaw_core PRIVATE advapi32)

    add_library(eaw_platform STATIC
        src/platform/win32_window.cpp
    )
    target_include_directories(eaw_platform PUBLIC src/platform/include)
    target_compile_features(eaw_platform PUBLIC cxx_std_20)
    target_compile_definitions(eaw_platform PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(eaw_platform PRIVATE user32)

    add_library(eaw_render_d3d12 STATIC
        src/render/d3d12_renderer.cpp
    )
    target_include_directories(eaw_render_d3d12 PUBLIC src/render/include)
    target_compile_features(eaw_render_d3d12 PUBLIC cxx_std_20)
    target_compile_definitions(eaw_render_d3d12 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(eaw_render_d3d12 PRIVATE d3d12 dxgi eaw_imgui)
    target_sources(eaw_render_d3d12 PRIVATE src/render/shaders/sandbox.hlsl src/render/shaders/terrain.hlsl src/render/shaders/debug.hlsl src/render/shaders/legacy_common.hlsli)
    set_source_files_properties(src/render/shaders/sandbox.hlsl src/render/shaders/terrain.hlsl src/render/shaders/debug.hlsl src/render/shaders/legacy_common.hlsli PROPERTIES VS_TOOL_OVERRIDE "None")
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/terrain.hlsl terrain_vs vs_6_0 g_eaw_terrain_vs)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/terrain.hlsl terrain_ps ps_6_0 g_eaw_terrain_ps)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/sandbox.hlsl vs_main vs_6_0 g_eaw_sandbox_vs)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/sandbox.hlsl ps_main ps_6_0 g_eaw_sandbox_ps)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/debug.hlsl debug_vs vs_6_0 g_eaw_debug_vs)
    eaw_compile_shader(eaw_render_d3d12 dxc src/render/shaders/debug.hlsl debug_ps ps_6_0 g_eaw_debug_ps)

    if(MSVC)
        target_compile_options(eaw_platform PRIVATE /W4 /permissive- /EHsc /utf-8)
        target_compile_options(eaw_render_d3d12 PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
endif()

# Platform-independent engine orchestration: visual scene construction and camera control. It
# consumes the render contract headers (engine-native data) but no graphics API.
add_library(eaw_engine STATIC
    src/engine/asset_cache.cpp
    src/engine/camera_controller.cpp
    src/engine/debug_draw.cpp
    src/engine/visual_scene_builder.cpp
)
target_include_directories(eaw_engine PUBLIC src/engine src/render/include)
target_compile_features(eaw_engine PUBLIC cxx_std_20)
target_compile_definitions(eaw_engine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(eaw_engine PUBLIC eaw_core)

add_executable(EAWEngine src/engine/main.cpp)
target_link_libraries(EAWEngine PRIVATE eaw_core eaw_engine)
if(WIN32)
    target_link_libraries(EAWEngine PRIVATE eaw_platform eaw_render_d3d12)
endif()
target_compile_features(EAWEngine PRIVATE cxx_std_20)
target_compile_definitions(EAWEngine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

add_executable(EAWLauncher src/launcher/main.cpp)
target_link_libraries(EAWLauncher PRIVATE eaw_core)
target_compile_features(EAWLauncher PRIVATE cxx_std_20)
target_compile_definitions(EAWLauncher PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

add_executable(EAWAssetInspector src/asset_inspector/main.cpp)
target_link_libraries(EAWAssetInspector PRIVATE eaw_core)
target_compile_features(EAWAssetInspector PRIVATE cxx_std_20)

add_executable(EAWDatEditor src/dat_editor/main.cpp)
target_link_libraries(EAWDatEditor PRIVATE eaw_core)
target_compile_features(EAWDatEditor PRIVATE cxx_std_20)
target_compile_definitions(EAWDatEditor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(eaw_engine PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(EAWEngine PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(EAWLauncher PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(EAWAssetInspector PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(EAWDatEditor PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

if(EAW_BUILD_TESTS)
    enable_testing()
    add_executable(EAWEngineTests
        tests/test_main.cpp
    )
    target_link_libraries(EAWEngineTests PRIVATE eaw_core eaw_engine)
    target_compile_features(EAWEngineTests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(EAWEngineTests PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
    add_test(NAME EAWEngineTests COMMAND EAWEngineTests)
endif()

