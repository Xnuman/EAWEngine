# eaw_math: vectors, quaternions, matrices, and transforms. Plain scalar float code with no
# dependencies, so simulation, decoders, and presentation share one deterministic implementation.

add_library(eaw_math STATIC)

set(HEADERS_DIR "include/math")
set(SOURCES_DIR "src")

set(EAW_MATH_HEADERS
    ${HEADERS_DIR}/matrix.hpp
    ${HEADERS_DIR}/quaternion.hpp
    ${HEADERS_DIR}/scalar.hpp
    ${HEADERS_DIR}/transform.hpp
    ${HEADERS_DIR}/vector.hpp
)

set(EAW_MATH_SOURCES
    ${SOURCES_DIR}/matrix.cpp
    ${SOURCES_DIR}/quaternion.cpp
    ${SOURCES_DIR}/transform.cpp
)

source_group( "" FILES ${EAW_MATH_SOURCES} ${EAW_MATH_HEADERS} )

target_sources(eaw_math
  PUBLIC
    FILE_SET "eaw_math_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_MATH_HEADERS}
  PRIVATE
    ${EAW_MATH_SOURCES}
)
