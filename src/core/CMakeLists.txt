# eaw_core: the foundation every module uses: error handling (Status, Result, fatal errors), the
# log, string utilities, and whole-file I/O. No graphics, platform, or file-format code.
#
# Transitional: the game-data code, the localization tools, and pose sampling still live in this directory
# under include/eaw/ as eaw_core_transitional (below) until the reorganization moves them to their own
# libraries (AGENTS.md, "Reorganization in progress").

add_library(eaw_core STATIC)

set(HEADERS_DIR "include/core")
set(SOURCES_DIR "src")

set(EAW_CORE_CHECKSUM
    ${HEADERS_DIR}/checksum.hpp
    ${SOURCES_DIR}/checksum.cpp
)

set(EAW_CORE_ERROR_HANDLING
    ${HEADERS_DIR}/fatal_error.hpp
    ${HEADERS_DIR}/status.hpp
    ${SOURCES_DIR}/fatal_error.cpp
)

set(EAW_CORE_FILE_IO
    ${HEADERS_DIR}/file_io.hpp
    ${SOURCES_DIR}/file_io.cpp
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
    include/eaw/alamo_pose.hpp
    include/eaw/alo_interpretation.hpp
    include/eaw/game_object_catalog.hpp
    include/eaw/legacy_material.hpp
    include/eaw/localization_scan.hpp
    include/eaw/localization_validation.hpp
    include/eaw/localization_workspace.hpp
    include/eaw/map_object_layout.hpp
    include/eaw/spawn_catalog.hpp
    include/eaw/xml_asset_database.hpp
    include/eaw/xml_schema.hpp
)

set(EAW_CORE_TRANSITIONAL_SOURCES
    alamo_pose.cpp
    alo_interpretation.cpp
    game_object_catalog.cpp
    legacy_material.cpp
    localization_scan.cpp
    localization_validation.cpp
    localization_workspace.cpp
    map_object_layout.cpp
    spawn_catalog.cpp
    xml_asset_database.cpp
    xml_schema_internal.hpp
    xml_schema_load.cpp
    xml_schema_validate.cpp
)

source_group("checksum" FILES ${EAW_CORE_CHECKSUM})
source_group("error_handling" FILES ${EAW_CORE_ERROR_HANDLING})
source_group("file_io" FILES ${EAW_CORE_FILE_IO})
source_group("log" FILES ${EAW_CORE_LOG})
source_group("string_utils" FILES ${EAW_CORE_STRING_UTILS})

target_sources(eaw_core
  PUBLIC
    FILE_SET "eaw_core_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES
        ${HEADERS_DIR}/checksum.hpp
        ${HEADERS_DIR}/fatal_error.hpp
        ${HEADERS_DIR}/file_io.hpp
        ${HEADERS_DIR}/log.hpp
        ${HEADERS_DIR}/status.hpp
        ${HEADERS_DIR}/string_utils.hpp
  PRIVATE
    ${SOURCES_DIR}/checksum.cpp
    ${SOURCES_DIR}/fatal_error.cpp
    ${SOURCES_DIR}/file_io.cpp
    ${SOURCES_DIR}/log.cpp
    ${SOURCES_DIR}/string_utils.cpp
)

target_link_libraries(eaw_core PUBLIC eaw_math)

# eaw_core_transitional: the game data, localization tools, and pose sampling,
# which still live here until the reorganization moves them to their own libraries. It sits above
# eaw_formats (the formats need only the foundation), so the dependency direction holds.
add_library(eaw_core_transitional STATIC)
source_group("transitional" FILES ${EAW_CORE_TRANSITIONAL_HEADERS} ${EAW_CORE_TRANSITIONAL_SOURCES})
target_sources(eaw_core_transitional
  PUBLIC
    FILE_SET "eaw_core_transitional_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_CORE_TRANSITIONAL_HEADERS}
  PRIVATE
    ${EAW_CORE_TRANSITIONAL_SOURCES}
)
target_link_libraries(eaw_core_transitional PUBLIC eaw_core eaw_formats eaw_math eaw_vfs)
