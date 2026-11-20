# rti: run-time type information (after Singe's rti): type names and numbers, the type registry with
# properties, constructors, metadata, and parents, variants, and ordering by metadata. Its types
# register in EAW_RTI_REGISTRATION blocks, so it is linked whole ($<LINK_LIBRARY:WHOLE_ARCHIVE,rti>).
project(rti)

add_library(rti STATIC)

set(HEADERS_DIR "include/rti")
set(SOURCES_DIR "src")

set(RTI_HEADERS
    ${HEADERS_DIR}/order.hpp
    ${HEADERS_DIR}/registration.hpp
    ${HEADERS_DIR}/registrator.hpp
    ${HEADERS_DIR}/type.hpp
    ${HEADERS_DIR}/type_info.hpp
    ${HEADERS_DIR}/type_name.hpp
    ${HEADERS_DIR}/variant.hpp
)

set(RTI_SOURCES
    ${SOURCES_DIR}/builtin_types.cpp
    ${SOURCES_DIR}/order.cpp
    ${SOURCES_DIR}/type.cpp
    ${SOURCES_DIR}/type_info.cpp
    ${SOURCES_DIR}/variant.cpp
)

source_group("" FILES ${RTI_HEADERS} ${RTI_SOURCES})

target_sources(rti
  PUBLIC
    FILE_SET "rti_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${RTI_HEADERS}
  PRIVATE
    ${RTI_SOURCES}
)

target_link_libraries(rti PUBLIC eaw_core)

set_target_properties(rti PROPERTIES FOLDER "core")
