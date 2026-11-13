# EAWAssetInspector: command-line inspection and corpus audits on the core and format libraries.

add_executable(EAWAssetInspector)

set(HEADERS_DIR "include/asset_inspector")
set(SOURCES_DIR "src")

set(EAW_ASSET_INSPECTOR_HEADERS
    ${HEADERS_DIR}/inspector.hpp
)

set(EAW_ASSET_INSPECTOR_SOURCES
    ${SOURCES_DIR}/audit_lua.cpp
    ${SOURCES_DIR}/audit_maps.cpp
    ${SOURCES_DIR}/audit_particles.cpp
    ${SOURCES_DIR}/audit_sounds.cpp
    ${SOURCES_DIR}/audit_textures.cpp
    ${SOURCES_DIR}/audit_xml.cpp
    ${SOURCES_DIR}/extract_assets.cpp
    ${SOURCES_DIR}/gait.cpp
    ${SOURCES_DIR}/inspect_asset.cpp
    ${SOURCES_DIR}/inspector_common.cpp
    ${SOURCES_DIR}/inspector_options.cpp
    ${SOURCES_DIR}/main.cpp
    ${SOURCES_DIR}/texture_png.cpp
)

source_group("" FILES ${EAW_ASSET_INSPECTOR_HEADERS} ${EAW_ASSET_INSPECTOR_SOURCES})

target_sources(EAWAssetInspector
  PRIVATE
    FILE_SET "eaw_asset_inspector_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_ASSET_INSPECTOR_HEADERS}
  PRIVATE
    ${EAW_ASSET_INSPECTOR_SOURCES}
)

target_link_libraries(EAWAssetInspector PRIVATE eaw_assets)
