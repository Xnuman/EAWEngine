# eaw_imgui_app: the shared Win32 + Direct3D 11 + Dear ImGui shell and file dialogs of the desktop
# tools.

add_library(eaw_imgui_app STATIC)

set(HEADERS_DIR "include/imgui_app")
set(SOURCES_DIR "src")

set(EAW_IMGUI_APP_HEADERS
    ${HEADERS_DIR}/file_dialogs.hpp
    ${HEADERS_DIR}/imgui_app.hpp
)

set(EAW_IMGUI_APP_SOURCES
    ${SOURCES_DIR}/file_dialogs.cpp
    ${SOURCES_DIR}/imgui_app.cpp
)

source_group("" FILES ${EAW_IMGUI_APP_HEADERS} ${EAW_IMGUI_APP_SOURCES})

target_sources(eaw_imgui_app
  PUBLIC
    FILE_SET "eaw_imgui_app_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_IMGUI_APP_HEADERS}
  PRIVATE
    ${EAW_IMGUI_APP_SOURCES}
)

target_link_libraries(eaw_imgui_app PUBLIC eaw_core eaw_imgui PRIVATE eaw_platform d3d11 dxgi ole32 shell32 uuid)
