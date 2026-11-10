# eaw_vfs: the layered virtual file system (loose files and MEG archives in the original's
# precedence, mods above game data), virtual-path normalization, and installation discovery. It
# depends only on the foundation; format readers are a separate library.

add_library(eaw_vfs STATIC)

set(HEADERS_DIR "include/vfs")
set(SOURCES_DIR "src")

set(EAW_VFS_FILE_SYSTEM
    ${HEADERS_DIR}/virtual_file_system.hpp
    ${SOURCES_DIR}/virtual_file_system.cpp
)

set(EAW_VFS_ARCHIVE
    ${HEADERS_DIR}/asset_path.hpp
    ${HEADERS_DIR}/meg_archive.hpp
    ${SOURCES_DIR}/asset_path.cpp
    ${SOURCES_DIR}/meg_archive.cpp
)

set(EAW_VFS_INSTALLATION
    ${HEADERS_DIR}/asset_environment.hpp
    ${HEADERS_DIR}/game_installation.hpp
    ${SOURCES_DIR}/asset_environment.cpp
    ${SOURCES_DIR}/game_installation.cpp
)

set(EAW_VFS_FILES
    ${EAW_VFS_FILE_SYSTEM}
    ${EAW_VFS_ARCHIVE}
    ${EAW_VFS_INSTALLATION}
)
set(EAW_VFS_HEADERS ${EAW_VFS_FILES})
list(FILTER EAW_VFS_HEADERS INCLUDE REGEX "\\.hpp$")
set(EAW_VFS_SOURCES ${EAW_VFS_FILES})
list(FILTER EAW_VFS_SOURCES INCLUDE REGEX "\\.cpp$")

source_group("file_system" FILES ${EAW_VFS_FILE_SYSTEM})
source_group("archive" FILES ${EAW_VFS_ARCHIVE})
source_group("installation" FILES ${EAW_VFS_INSTALLATION})

target_sources(eaw_vfs
  PUBLIC
    FILE_SET "eaw_vfs_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_VFS_HEADERS}
  PRIVATE
    ${EAW_VFS_SOURCES}
)

target_link_libraries(eaw_vfs PUBLIC eaw_core)
if(WIN32)
    # Installation discovery reads the registry.
    target_link_libraries(eaw_vfs PRIVATE advapi32)
endif()
