# eaw_scene: Scene building: decoded assets to engine-native visual data (maps, models, terrain, materials,
# particles, rigs) for the renderer and presentation, and what XML types say about their look.

add_library(eaw_scene STATIC)

set(HEADERS_DIR "include/scene")
set(SOURCES_DIR "src")

set(EAW_SCENE_HEADERS
    ${HEADERS_DIR}/scene_builder_internal.hpp
    ${HEADERS_DIR}/visual_scene_builder.hpp
)

set(EAW_SCENE_SOURCES
    ${SOURCES_DIR}/scene_materials.cpp
    ${SOURCES_DIR}/scene_models.cpp
    ${SOURCES_DIR}/scene_particles.cpp
    ${SOURCES_DIR}/scene_debris.cpp
    ${SOURCES_DIR}/scene_projectiles.cpp
    ${SOURCES_DIR}/scene_terrain.cpp
    ${SOURCES_DIR}/type_looks.cpp
    ${SOURCES_DIR}/visual_map_builder.cpp
)

source_group( "" FILES ${EAW_SCENE_SOURCES} ${EAW_SCENE_HEADERS} )

target_sources(eaw_scene
  PUBLIC
    FILE_SET "eaw_scene_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_SCENE_HEADERS}
  PRIVATE
    ${EAW_SCENE_SOURCES}
)

target_link_libraries(eaw_scene PUBLIC eaw_battle eaw_presentation eaw_render_contract PRIVATE eaw_render_core)
