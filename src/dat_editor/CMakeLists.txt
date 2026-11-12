# EAWDatEditor: command-line DAT localization tool.

add_executable(EAWDatEditor)

set(HEADERS_DIR "include/dat_editor")
set(SOURCES_DIR "src")

set(EAW_DAT_EDITOR_HEADERS
    ${HEADERS_DIR}/dat_editor.hpp
)

set(EAW_DAT_EDITOR_SOURCES
    ${SOURCES_DIR}/dat_commands.cpp
    ${SOURCES_DIR}/dat_editor_options.cpp
    ${SOURCES_DIR}/main.cpp
    ${SOURCES_DIR}/workspace_commands.cpp
)

source_group("" FILES ${EAW_DAT_EDITOR_HEADERS} ${EAW_DAT_EDITOR_SOURCES})

target_sources(EAWDatEditor
  PRIVATE
    FILE_SET "eaw_dat_editor_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_DAT_EDITOR_HEADERS}
  PRIVATE
    ${EAW_DAT_EDITOR_SOURCES}
)

target_link_libraries(EAWDatEditor PRIVATE eaw_localization)
