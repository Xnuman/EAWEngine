# EAWAssetInspector: command-line inspection and corpus audits on eaw_core.
set(EAW_ASSET_INSPECTOR_SOURCES
    audit_lua.cpp
    audit_maps.cpp
    audit_particles.cpp
    audit_textures.cpp
    audit_xml.cpp
    inspect_asset.cpp
    inspector.hpp
    inspector_common.cpp
    inspector_options.cpp
    main.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_ASSET_INSPECTOR_SOURCES})

add_executable(EAWAssetInspector ${EAW_ASSET_INSPECTOR_SOURCES})
eaw_target_defaults(EAWAssetInspector "tools")
target_link_libraries(EAWAssetInspector PRIVATE eaw_core)
