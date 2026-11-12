# eaw_image_compare: image regression, how two RGBA images differ (the capture checks and
# EAWEngine compare-images). Comparison is a tool's behavior, not format knowledge, so it sits
# beside the formats instead of in them.

add_library(eaw_image_compare STATIC)

set(HEADERS_DIR "include/image_compare")
set(SOURCES_DIR "src")

set(EAW_IMAGE_COMPARE_HEADERS
    ${HEADERS_DIR}/image_compare.hpp
)

set(EAW_IMAGE_COMPARE_SOURCES
    ${SOURCES_DIR}/image_compare.cpp
)

source_group( "" FILES ${EAW_IMAGE_COMPARE_SOURCES} ${EAW_IMAGE_COMPARE_HEADERS} )

target_sources(eaw_image_compare
  PUBLIC
    FILE_SET "eaw_image_compare_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_IMAGE_COMPARE_HEADERS}
  PRIVATE
    ${EAW_IMAGE_COMPARE_SOURCES}
)

target_link_libraries(eaw_image_compare PUBLIC eaw_core eaw_formats)
