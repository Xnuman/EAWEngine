# EAWDatEditor: command-line DAT localization tool.
set(EAW_DAT_EDITOR_SOURCES
    main.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_DAT_EDITOR_SOURCES})

add_executable(EAWDatEditor ${EAW_DAT_EDITOR_SOURCES})
eaw_target_defaults(EAWDatEditor "tools")
target_link_libraries(EAWDatEditor PRIVATE eaw_core)
