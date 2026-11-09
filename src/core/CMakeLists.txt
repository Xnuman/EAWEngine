# Formats, the layered VFS, compatibility data, and reusable asset logic. No graphics or Win32
# presentation code.
set(EAW_CORE_HEADERS
    include/eaw/ala_animation.hpp
    include/eaw/alo_interpretation.hpp
    include/eaw/alo_model.hpp
    include/eaw/asset_environment.hpp
    include/eaw/asset_path.hpp
    include/eaw/chunk_file.hpp
    include/eaw/dat_file.hpp
    include/eaw/dds_texture.hpp
    include/eaw/fatal_error.hpp
    include/eaw/game_installation.hpp
    include/eaw/game_object_catalog.hpp
    include/eaw/legacy_material.hpp
    include/eaw/localization_scan.hpp
    include/eaw/localization_validation.hpp
    include/eaw/localization_workspace.hpp
    include/eaw/log.hpp
    include/eaw/lua_script.hpp
    include/eaw/map_object_layout.hpp
    include/eaw/meg_archive.hpp
    include/eaw/mtd_directory.hpp
    include/eaw/particle_system.hpp
    include/eaw/png_file.hpp
    include/eaw/rgba_image.hpp
    include/eaw/spawn_catalog.hpp
    include/eaw/status.hpp
    include/eaw/ted_map.hpp
    include/eaw/text_encoding.hpp
    include/eaw/virtual_file_system.hpp
    include/eaw/xml_asset_database.hpp
    include/eaw/xml_document.hpp
    include/eaw/xml_schema.hpp
)

set(EAW_CORE_SOURCES
    ala_animation.cpp
    alo_interpretation.cpp
    alo_model.cpp
    asset_environment.cpp
    asset_path.cpp
    chunk_file.cpp
    dat_file.cpp
    dds_texture.cpp
    fatal_error.cpp
    game_installation.cpp
    game_object_catalog.cpp
    legacy_material.cpp
    localization_scan.cpp
    localization_validation.cpp
    localization_workspace.cpp
    log.cpp
    lua_script.cpp
    map_object_layout.cpp
    meg_archive.cpp
    mtd_directory.cpp
    particle_system.cpp
    png_file.cpp
    rgba_image.cpp
    spawn_catalog.cpp
    ted_map.cpp
    text_encoding.cpp
    virtual_file_system.cpp
    xml_asset_database.cpp
    xml_document.cpp
    xml_schema_internal.hpp
    xml_schema_load.cpp
    xml_schema_validate.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_CORE_HEADERS} ${EAW_CORE_SOURCES})

add_library(eaw_core STATIC ${EAW_CORE_HEADERS} ${EAW_CORE_SOURCES})
target_include_directories(eaw_core PUBLIC include)
target_link_libraries(eaw_core PUBLIC eaw_math)
if(WIN32)
    target_link_libraries(eaw_core PRIVATE advapi32)
endif()
