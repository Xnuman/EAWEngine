# eaw_formats: readers and writers of the original's file formats. They turn bytes into file-level
# data and back, losslessly, and never do I/O, read through the VFS, or interpret game rules.

add_library(eaw_formats STATIC)

set(HEADERS_DIR "include/formats")
set(SOURCES_DIR "src")

set(EAW_FORMATS_ALAMO
    ${HEADERS_DIR}/ala_animation.hpp
    ${HEADERS_DIR}/ala_animation_helpers.hpp
    ${HEADERS_DIR}/alamo_chunks.hpp
    ${HEADERS_DIR}/alo_generic.hpp
    ${HEADERS_DIR}/ted_map.hpp
    ${SOURCES_DIR}/ala_animation.cpp
    ${SOURCES_DIR}/ala_animation_helpers.cpp
    ${SOURCES_DIR}/alamo_chunks.cpp
    ${SOURCES_DIR}/alo_generic.cpp
    ${SOURCES_DIR}/ted_map.cpp
)

set(EAW_FORMATS_MODEL
    ${HEADERS_DIR}/alo_model.hpp
    ${HEADERS_DIR}/alo_model_helpers.hpp
    ${SOURCES_DIR}/alo_model.cpp
    ${SOURCES_DIR}/alo_model_helpers.cpp
)

set(EAW_FORMATS_PARTICLE
    ${HEADERS_DIR}/alo_particle_system.hpp
    ${SOURCES_DIR}/alo_particle_system.cpp
)

set(EAW_FORMATS_TEXTURE
    ${HEADERS_DIR}/dds_texture.hpp
    ${HEADERS_DIR}/mtd_atlas.hpp
    ${SOURCES_DIR}/dds_texture.cpp
    ${SOURCES_DIR}/mtd_atlas.cpp
)

set(EAW_FORMATS_IMAGE
    ${HEADERS_DIR}/png_image.hpp
    ${HEADERS_DIR}/rgba_image.hpp
    ${SOURCES_DIR}/png_image.cpp
    ${SOURCES_DIR}/rgba_image.cpp
)

set(EAW_FORMATS_XML
    ${HEADERS_DIR}/xml_document.hpp
    ${HEADERS_DIR}/xml_helpers.hpp
    ${SOURCES_DIR}/xml_document.cpp
    ${SOURCES_DIR}/xml_helpers.cpp
)

set(EAW_FORMATS_LOCALIZATION
    ${HEADERS_DIR}/dat_string_table.hpp
    ${HEADERS_DIR}/dat_string_table_helpers.hpp
    ${SOURCES_DIR}/dat_string_table.cpp
    ${SOURCES_DIR}/dat_string_table_helpers.cpp
)

set(EAW_FORMATS_SCRIPT
    ${HEADERS_DIR}/lua_bytecode.hpp
    ${SOURCES_DIR}/lua_bytecode.cpp
)

set(EAW_FORMATS_FILES
    ${EAW_FORMATS_ALAMO}
    ${EAW_FORMATS_MODEL}
    ${EAW_FORMATS_PARTICLE}
    ${EAW_FORMATS_TEXTURE}
    ${EAW_FORMATS_IMAGE}
    ${EAW_FORMATS_XML}
    ${EAW_FORMATS_LOCALIZATION}
    ${EAW_FORMATS_SCRIPT}
)
set(EAW_FORMATS_HEADERS ${EAW_FORMATS_FILES})
list(FILTER EAW_FORMATS_HEADERS INCLUDE REGEX "\\.hpp$")
set(EAW_FORMATS_SOURCES ${EAW_FORMATS_FILES})
list(FILTER EAW_FORMATS_SOURCES INCLUDE REGEX "\\.cpp$")

source_group("alamo" FILES ${EAW_FORMATS_ALAMO})
source_group("model" FILES ${EAW_FORMATS_MODEL})
source_group("particle" FILES ${EAW_FORMATS_PARTICLE})
source_group("texture" FILES ${EAW_FORMATS_TEXTURE})
source_group("image" FILES ${EAW_FORMATS_IMAGE})
source_group("xml" FILES ${EAW_FORMATS_XML})
source_group("localization" FILES ${EAW_FORMATS_LOCALIZATION})
source_group("script" FILES ${EAW_FORMATS_SCRIPT})

target_sources(eaw_formats
  PUBLIC
    FILE_SET "eaw_formats_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_FORMATS_HEADERS}
  PRIVATE
    ${EAW_FORMATS_SOURCES}
)

target_link_libraries(eaw_formats PUBLIC eaw_core eaw_math)
