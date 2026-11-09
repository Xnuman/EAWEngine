# Shared Win32 + Direct3D 11 + Dear ImGui shell and file dialogs for the desktop tools.
set(EAW_IMGUI_APP_HEADERS
    include/eaw/file_dialogs.hpp
    include/eaw/imgui_app.hpp
)

set(EAW_IMGUI_APP_SOURCES
    file_dialogs.cpp
    imgui_app.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_IMGUI_APP_HEADERS} ${EAW_IMGUI_APP_SOURCES})

add_library(eaw_imgui_app STATIC ${EAW_IMGUI_APP_HEADERS} ${EAW_IMGUI_APP_SOURCES})
eaw_target_defaults(eaw_imgui_app "libraries")
target_include_directories(eaw_imgui_app PUBLIC include)
target_link_libraries(eaw_imgui_app PUBLIC eaw_core eaw_imgui PRIVATE eaw_platform d3d11 dxgi ole32 shell32 uuid)
