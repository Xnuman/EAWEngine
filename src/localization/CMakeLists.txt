# eaw_localization: the localization tools shared by EAWDatEditor and EAWTools: DAT string tables
# from disk, TSV workspaces, unit text keys, TEXT_* references in mod XML, and checks of a
# translation against its reference.

add_library(eaw_localization STATIC)

set(HEADERS_DIR "include/localization")
set(SOURCES_DIR "src")

set(EAW_LOCALIZATION_HEADERS
    ${HEADERS_DIR}/localization_scan.hpp
    ${HEADERS_DIR}/localization_validation.hpp
    ${HEADERS_DIR}/localization_workspace.hpp
)

set(EAW_LOCALIZATION_SOURCES
    ${SOURCES_DIR}/localization_scan.cpp
    ${SOURCES_DIR}/localization_validation.cpp
    ${SOURCES_DIR}/localization_workspace.cpp
)

source_group("localization" FILES ${EAW_LOCALIZATION_HEADERS} ${EAW_LOCALIZATION_SOURCES})

target_sources(eaw_localization
  PUBLIC
    FILE_SET "eaw_localization_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_LOCALIZATION_HEADERS}
  PRIVATE
    ${EAW_LOCALIZATION_SOURCES}
)

target_link_libraries(eaw_localization PUBLIC eaw_core eaw_formats)
