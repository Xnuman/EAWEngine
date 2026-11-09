# eaw_core: the foundation every module uses: error handling (Status, Result, fatal errors), the
# log, and string utilities. No graphics, platform, or file-format code.
#
# Transitional: the format decoders, the VFS, and the game-data code still live here under
# include/eaw/ until the reorganization moves them to their own libraries (AGENTS.md,
# "Reorganization in progress").

add_library(eaw_core STATIC)

set(HEADERS_DIR "include/core")
set(SOURCES_DIR "src")

set(EAW_CORE_ERROR_HANDLING
    ${HEADERS_DIR}/fatal_error.hpp
    ${HEADERS_DIR}/status.hpp
    ${SOURCES_DIR}/fatal_error.cpp
)

set(EAW_CORE_LOG
    ${HEADERS_DIR}/log.hpp
    ${SOURCES_DIR}/log.cpp
)

set(EAW_CORE_STRING_UTILS
    ${HEADERS_DIR}/string_utils.hpp
    ${SOURCES_DIR}/string_utils.cpp
)

set(EAW_CORE_TRANSITIONAL_HEADERS
    include/eaw/ala_animation.hpp
    include/eaw/alo_interpretation.hpp
    include/eaw/alo_model.hpp
    include/eaw/asset_environment.hpp
    include/eaw/asset_path.hpp
    include/eaw/chunk_file.hpp
    include/eaw/dat_file.hpp
    include/eaw/dds_texture.hpp
    include/eaw/game_installation.hpp
    include/eaw/game_object_catalog.hpp
    include/eaw/legacy_material.hpp
    include/eaw/localization_scan.hpp
    include/eaw/localization_validation.hpp
    include/eaw/localization_workspace.hpp
    include/eaw/lua_script.hpp
    include/eaw/map_object_layout.hpp
    include/eaw/meg_archive.hpp
    include/eaw/mtd_directory.hpp
    include/eaw/particle_system.hpp
    include/eaw/png_file.hpp
    include/eaw/rgba_image.hpp
    include/eaw/spawn_catalog.hpp
    include/eaw/ted_map.hpp
    include/eaw/virtual_file_system.hpp
    include/eaw/xml_asset_database.hpp
    include/eaw/xml_document.hpp
    include/eaw/xml_schema.hpp
)

set(EAW_CORE_TRANSITIONAL_SOURCES
    ala_animation.cpp
    alo_interpretation.cpp
    alo_model.cpp
    asset_environment.cpp
    asset_path.cpp
    chunk_file.cpp
    dat_file.cpp
    dds_texture.cpp
    game_installation.cpp
    game_object_catalog.cpp
    legacy_material.cpp
    localization_scan.cpp
    localization_validation.cpp
    localization_workspace.cpp
    lua_script.cpp
    map_object_layout.cpp
    meg_archive.cpp
    mtd_directory.cpp
    particle_system.cpp
    png_file.cpp
    rgba_image.cpp
    spawn_catalog.cpp
    ted_map.cpp
    virtual_file_system.cpp
    xml_asset_database.cpp
    xml_document.cpp
    xml_schema_internal.hpp
    xml_schema_load.cpp
    xml_schema_validate.cpp
)

source_group("error_handling" FILES ${EAW_CORE_ERROR_HANDLING})
source_group("log" FILES ${EAW_CORE_LOG})
source_group("string_utils" FILES ${EAW_CORE_STRING_UTILS})
source_group("transitional" FILES ${EAW_CORE_TRANSITIONAL_HEADERS} ${EAW_CORE_TRANSITIONAL_SOURCES})

target_sources(eaw_core
  PUBLIC
    FILE_SET "eaw_core_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES
        ${HEADERS_DIR}/fatal_error.hpp
        ${HEADERS_DIR}/log.hpp
        ${HEADERS_DIR}/status.hpp
        ${HEADERS_DIR}/string_utils.hpp
        ${EAW_CORE_TRANSITIONAL_HEADERS}
  PRIVATE
    ${SOURCES_DIR}/fatal_error.cpp
    ${SOURCES_DIR}/log.cpp
    ${SOURCES_DIR}/string_utils.cpp
    ${EAW_CORE_TRANSITIONAL_SOURCES}
)

target_link_libraries(eaw_core PUBLIC eaw_math)
if(WIN32)
    target_link_libraries(eaw_core PRIVATE advapi32)
endif()
