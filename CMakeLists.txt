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
    src/core/game_installation.cpp
    src/core/localization_scan.cpp
    src/core/localization_workspace.cpp
    src/core/meg_archive.cpp
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

if(EAW_BUILD_GUI AND WIN32)
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

    add_executable(EAWTools WIN32
        src/tools_gui/file_dialogs.cpp
        src/tools_gui/gui_app.cpp
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
    target_compile_features(EAWTools PRIVATE cxx_std_20)
    target_compile_definitions(EAWTools PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(EAWTools PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
endif()

if(WIN32)
    target_link_libraries(eaw_core PRIVATE advapi32)
endif()

add_executable(EAWEngine src/engine/main.cpp)
target_link_libraries(EAWEngine PRIVATE eaw_core)
target_compile_features(EAWEngine PRIVATE cxx_std_20)

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
    target_link_libraries(EAWEngineTests PRIVATE eaw_core)
    target_compile_features(EAWEngineTests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(EAWEngineTests PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
    add_test(NAME EAWEngineTests COMMAND EAWEngineTests)
endif()

