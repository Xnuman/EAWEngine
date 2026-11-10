# eaw_assets: the game asset layer. It interprets file-level data for the engine and the tools:
# the XML object database and schema, game object and spawn catalogs, map object layout, legacy
# material and ALO naming rules, and the engine-native Skeleton and Animation with their loaders and
# pose sampling. Model and the particle-effect loader follow (AGENTS.md, "Reorganization in
# progress", step 5).

add_library(eaw_assets STATIC)

set(HEADERS_DIR "include/assets")
set(SOURCES_DIR "src")

set(EAW_ASSETS_GAME_DATA
    ${HEADERS_DIR}/game_object_catalog.hpp
    ${HEADERS_DIR}/map_object_layout.hpp
    ${HEADERS_DIR}/spawn_catalog.hpp
    ${HEADERS_DIR}/xml_asset_database.hpp
    ${HEADERS_DIR}/xml_schema.hpp
    ${SOURCES_DIR}/game_object_catalog.cpp
    ${SOURCES_DIR}/map_object_layout.cpp
    ${SOURCES_DIR}/spawn_catalog.cpp
    ${SOURCES_DIR}/xml_asset_database.cpp
    ${SOURCES_DIR}/xml_schema_internal.hpp
    ${SOURCES_DIR}/xml_schema_load.cpp
    ${SOURCES_DIR}/xml_schema_validate.cpp
)

set(EAW_ASSETS_MODEL
    ${HEADERS_DIR}/alo_interpretation.hpp
    ${HEADERS_DIR}/legacy_material.hpp
    ${HEADERS_DIR}/model.hpp
    ${SOURCES_DIR}/alo_interpretation.cpp
    ${SOURCES_DIR}/legacy_material.cpp
)

set(EAW_ASSETS_ANIMATION
    ${HEADERS_DIR}/animation.hpp
    ${HEADERS_DIR}/pose.hpp
    ${HEADERS_DIR}/skeleton.hpp
    ${SOURCES_DIR}/animation.cpp
    ${SOURCES_DIR}/pose.cpp
    ${SOURCES_DIR}/skeleton.cpp
)

set(EAW_ASSETS_LOADERS
    ${HEADERS_DIR}/alamo_loaders.hpp
    ${SOURCES_DIR}/alamo_loaders.cpp
)

set(EAW_ASSETS_HEADERS
    ${HEADERS_DIR}/alamo_loaders.hpp
    ${HEADERS_DIR}/alo_interpretation.hpp
    ${HEADERS_DIR}/animation.hpp
    ${HEADERS_DIR}/game_object_catalog.hpp
    ${HEADERS_DIR}/legacy_material.hpp
    ${HEADERS_DIR}/map_object_layout.hpp
    ${HEADERS_DIR}/model.hpp
    ${HEADERS_DIR}/pose.hpp
    ${HEADERS_DIR}/skeleton.hpp
    ${HEADERS_DIR}/spawn_catalog.hpp
    ${HEADERS_DIR}/xml_asset_database.hpp
    ${HEADERS_DIR}/xml_schema.hpp
)

source_group("game_data" FILES ${EAW_ASSETS_GAME_DATA})
source_group("model" FILES ${EAW_ASSETS_MODEL})
source_group("animation" FILES ${EAW_ASSETS_ANIMATION})
source_group("loaders" FILES ${EAW_ASSETS_LOADERS})

target_sources(eaw_assets
  PUBLIC
    FILE_SET "eaw_assets_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_ASSETS_HEADERS}
  PRIVATE
    ${SOURCES_DIR}/alamo_loaders.cpp
    ${SOURCES_DIR}/alo_interpretation.cpp
    ${SOURCES_DIR}/animation.cpp
    ${SOURCES_DIR}/game_object_catalog.cpp
    ${SOURCES_DIR}/legacy_material.cpp
    ${SOURCES_DIR}/map_object_layout.cpp
    ${SOURCES_DIR}/pose.cpp
    ${SOURCES_DIR}/skeleton.cpp
    ${SOURCES_DIR}/spawn_catalog.cpp
    ${SOURCES_DIR}/xml_asset_database.cpp
    ${SOURCES_DIR}/xml_schema_internal.hpp
    ${SOURCES_DIR}/xml_schema_load.cpp
    ${SOURCES_DIR}/xml_schema_validate.cpp
)

target_link_libraries(eaw_assets PUBLIC eaw_core eaw_formats eaw_math eaw_render_contract eaw_vfs)
