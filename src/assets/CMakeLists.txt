# eaw_assets: the game asset layer. It interprets file-level data for the engine and the tools:
# the XML object database and schema, game object and spawn catalogs, map object layout, legacy
# material and ALO naming rules, the engine-native Model, Skeleton, Animation, and ParticleEffect
# with their loaders, pose sampling, the particle fidelity rules, sound events, and the
# ResourceManager with AssetCache (typed loading and caching with hot-reload dependencies).

add_library(eaw_assets STATIC)

set(HEADERS_DIR "include/assets")
set(SOURCES_DIR "src")

set(EAW_ASSETS_GAME_DATA
    ${HEADERS_DIR}/game_object_catalog.hpp
    ${HEADERS_DIR}/map_object_layout.hpp
    ${HEADERS_DIR}/spawn_catalog.hpp
    ${HEADERS_DIR}/surface_effects.hpp
    ${HEADERS_DIR}/type_fields.hpp
    ${HEADERS_DIR}/type_hardpoints.hpp
    ${HEADERS_DIR}/xml_asset_database.hpp
    ${HEADERS_DIR}/xml_schema.hpp
    ${SOURCES_DIR}/game_object_catalog.cpp
    ${SOURCES_DIR}/map_object_layout.cpp
    ${SOURCES_DIR}/spawn_catalog.cpp
    ${SOURCES_DIR}/surface_effects.cpp
    ${SOURCES_DIR}/type_fields.cpp
    ${SOURCES_DIR}/type_hardpoints.cpp
    ${SOURCES_DIR}/xml_asset_database.cpp
    ${SOURCES_DIR}/xml_schema_internal.hpp
    ${SOURCES_DIR}/xml_schema_load.cpp
    ${SOURCES_DIR}/xml_schema_validate.cpp
)

set(EAW_ASSETS_MODEL
    ${HEADERS_DIR}/alo_interpretation.hpp
    ${HEADERS_DIR}/legacy_material.hpp
    ${HEADERS_DIR}/model.hpp
    ${HEADERS_DIR}/model_geometry.hpp
    ${SOURCES_DIR}/alo_interpretation.cpp
    ${SOURCES_DIR}/legacy_material.cpp
    ${SOURCES_DIR}/model_geometry.cpp
)

set(EAW_ASSETS_ANIMATION
    ${HEADERS_DIR}/animation.hpp
    ${HEADERS_DIR}/animation_events.hpp
    ${HEADERS_DIR}/pose.hpp
    ${HEADERS_DIR}/skeleton.hpp
    ${SOURCES_DIR}/animation.cpp
    ${SOURCES_DIR}/animation_events.cpp
    ${SOURCES_DIR}/pose.cpp
    ${SOURCES_DIR}/skeleton.cpp
)

set(EAW_ASSETS_AUDIO
    ${HEADERS_DIR}/sound_events.hpp
    ${HEADERS_DIR}/sound_loaders.hpp
    ${SOURCES_DIR}/sound_events.cpp
    ${SOURCES_DIR}/sound_loaders.cpp
)

set(EAW_ASSETS_PARTICLE
    ${HEADERS_DIR}/particle_rules.hpp
    ${SOURCES_DIR}/particle_rules.cpp
)

set(EAW_ASSETS_LOADERS
    ${HEADERS_DIR}/alamo_loaders.hpp
    ${SOURCES_DIR}/alamo_loaders.cpp
)

set(EAW_ASSETS_RESOURCES
    ${HEADERS_DIR}/asset_cache.hpp
    ${HEADERS_DIR}/resource_loaders.hpp
    ${HEADERS_DIR}/resource_manager.hpp
    ${SOURCES_DIR}/asset_cache.cpp
    ${SOURCES_DIR}/resource_loaders.cpp
)

set(EAW_ASSETS_FILES
    ${EAW_ASSETS_GAME_DATA}
    ${EAW_ASSETS_MODEL}
    ${EAW_ASSETS_ANIMATION}
    ${EAW_ASSETS_AUDIO}
    ${EAW_ASSETS_PARTICLE}
    ${EAW_ASSETS_LOADERS}
    ${EAW_ASSETS_RESOURCES}
)
# Public headers are the files under HEADERS_DIR; the rest (sources, private headers) are private.
set(EAW_ASSETS_HEADERS ${EAW_ASSETS_FILES})
list(FILTER EAW_ASSETS_HEADERS INCLUDE REGEX "^${HEADERS_DIR}/")
set(EAW_ASSETS_SOURCES ${EAW_ASSETS_FILES})
list(FILTER EAW_ASSETS_SOURCES EXCLUDE REGEX "^${HEADERS_DIR}/")

source_group("game_data" FILES ${EAW_ASSETS_GAME_DATA})
source_group("model" FILES ${EAW_ASSETS_MODEL})
source_group("animation" FILES ${EAW_ASSETS_ANIMATION})
source_group("audio" FILES ${EAW_ASSETS_AUDIO})
source_group("particle" FILES ${EAW_ASSETS_PARTICLE})
source_group("loaders" FILES ${EAW_ASSETS_LOADERS})
source_group("resources" FILES ${EAW_ASSETS_RESOURCES})

target_sources(eaw_assets
  PUBLIC
    FILE_SET "eaw_assets_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_ASSETS_HEADERS}
  PRIVATE
    ${EAW_ASSETS_SOURCES}
)

target_link_libraries(eaw_assets PUBLIC eaw_audio eaw_core eaw_formats eaw_math eaw_render_contract eaw_vfs)
