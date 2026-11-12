# eaw_control: Cameras and RTS control: camera controllers and math, box selection and picking, and the terrain
# height field picking reads.

add_library(eaw_control STATIC)

set(HEADERS_DIR "include/control")
set(SOURCES_DIR "src")

set(EAW_CONTROL_HEADERS
    ${HEADERS_DIR}/camera_controller.hpp
    ${HEADERS_DIR}/camera_math.hpp
    ${HEADERS_DIR}/rts_control.hpp
    ${HEADERS_DIR}/terrain_height.hpp
)

set(EAW_CONTROL_SOURCES
    ${SOURCES_DIR}/camera_controller.cpp
    ${SOURCES_DIR}/camera_math.cpp
    ${SOURCES_DIR}/rts_control.cpp
    ${SOURCES_DIR}/terrain_height.cpp
)

source_group( "" FILES ${EAW_CONTROL_SOURCES} ${EAW_CONTROL_HEADERS} )

target_sources(eaw_control
  PUBLIC
    FILE_SET "eaw_control_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_CONTROL_HEADERS}
  PRIVATE
    ${EAW_CONTROL_SOURCES}
)

target_link_libraries(eaw_control PUBLIC eaw_render_contract eaw_simulation)
