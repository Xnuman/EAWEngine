# eaw_core: the foundation every module uses: error handling (Status, Result, fatal errors), the
# log, string utilities, and whole-file I/O. No graphics, platform, or file-format code.

add_library(eaw_core STATIC)

set(HEADERS_DIR "include/core")
set(SOURCES_DIR "src")

set(EAW_CORE_BYTE_ORDER
    ${HEADERS_DIR}/byte_order.hpp
    ${SOURCES_DIR}/byte_order.cpp
)

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

set(EAW_CORE_RANDOM
    ${HEADERS_DIR}/random.hpp
    ${SOURCES_DIR}/random.cpp
)

set(EAW_CORE_STRING_UTILS
    ${HEADERS_DIR}/string_utils.hpp
    ${SOURCES_DIR}/string_utils.cpp
)

source_group("byte_order" FILES ${EAW_CORE_BYTE_ORDER})
source_group("checksum" FILES ${EAW_CORE_CHECKSUM})
source_group("error_handling" FILES ${EAW_CORE_ERROR_HANDLING})
source_group("file_io" FILES ${EAW_CORE_FILE_IO})
source_group("log" FILES ${EAW_CORE_LOG})
source_group("random" FILES ${EAW_CORE_RANDOM})
source_group("string_utils" FILES ${EAW_CORE_STRING_UTILS})

target_sources(eaw_core
  PUBLIC
    FILE_SET "eaw_core_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES
        ${HEADERS_DIR}/byte_order.hpp
        ${HEADERS_DIR}/checksum.hpp
        ${HEADERS_DIR}/fatal_error.hpp
        ${HEADERS_DIR}/file_io.hpp
        ${HEADERS_DIR}/log.hpp
        ${HEADERS_DIR}/random.hpp
        ${HEADERS_DIR}/status.hpp
        ${HEADERS_DIR}/string_utils.hpp
  PRIVATE
    ${SOURCES_DIR}/byte_order.cpp
    ${SOURCES_DIR}/checksum.cpp
    ${SOURCES_DIR}/fatal_error.cpp
    ${SOURCES_DIR}/file_io.cpp
    ${SOURCES_DIR}/log.cpp
    ${SOURCES_DIR}/random.cpp
    ${SOURCES_DIR}/string_utils.cpp
)

target_link_libraries(eaw_core PUBLIC eaw_math)
