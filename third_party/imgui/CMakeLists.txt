# Dear ImGui (pinned) serves the EAWTools workbench, the development launcher, and the D3D12
# renderer's debug overlay. Third-party code: its own warning level, not the project's rules.
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 5c7c293a66d7a659d4e6a1abdf18291ce004c07e
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(imgui)

set(EAW_IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.h
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.h
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.h
    ${imgui_SOURCE_DIR}/imconfig.h
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui.h
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_internal.h
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.h
)
source_group(TREE ${imgui_SOURCE_DIR} FILES ${EAW_IMGUI_SOURCES})

add_library(eaw_imgui STATIC ${EAW_IMGUI_SOURCES})
set_target_properties(eaw_imgui PROPERTIES FOLDER "third_party")
target_include_directories(eaw_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
)
target_compile_features(eaw_imgui PUBLIC cxx_std_20)
target_compile_definitions(eaw_imgui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(eaw_imgui PRIVATE /W3 /utf-8)
endif()
