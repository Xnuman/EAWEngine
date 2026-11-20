project(junk)

add_library(junk STATIC)

set(HEADERS_DIR "include/junk")
set(SOURCES_DIR "src")

set(JUNK_HEADERS
    ${HEADERS_DIR}/local_point.hpp
    ${HEADERS_DIR}/entity_id.hpp
)

source_group( "" FILES ${JUNK_HEADERS} )

target_sources(junk
  PUBLIC
    FILE_SET "junk_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${JUNK_HEADERS}
)

set_target_properties(junk PROPERTIES LINKER_LANGUAGE CXX)