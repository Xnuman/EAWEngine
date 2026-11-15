# eaw_battle: Battles above the simulation: orders, logs, the runtime that steps a battle and its game-object
# scripts, the battle side of maps, and what gameplay reads of XML types and their models. No render
# contract.

add_library(eaw_battle STATIC)

set(HEADERS_DIR "include/battle")
set(SOURCES_DIR "src")

set(EAW_BATTLE_HEADERS
    ${HEADERS_DIR}/battle.hpp
    ${HEADERS_DIR}/battle_log.hpp
    ${HEADERS_DIR}/battle_map.hpp
    ${HEADERS_DIR}/battle_map_build.hpp
    ${HEADERS_DIR}/battle_runtime.hpp
    ${HEADERS_DIR}/battle_scripts.hpp
    ${HEADERS_DIR}/combat_content.hpp
    ${HEADERS_DIR}/object_types.hpp
    ${HEADERS_DIR}/type_recipe.hpp
)

set(EAW_BATTLE_SOURCES
    ${SOURCES_DIR}/battle.cpp
    ${SOURCES_DIR}/battle_log.cpp
    ${SOURCES_DIR}/battle_map_builder.cpp
    ${SOURCES_DIR}/battle_runtime.cpp
    ${SOURCES_DIR}/battle_scripts.cpp
    ${SOURCES_DIR}/combat_content.cpp
    ${SOURCES_DIR}/object_types.cpp
    ${SOURCES_DIR}/type_recipe.cpp
)

source_group( "" FILES ${EAW_BATTLE_SOURCES} ${EAW_BATTLE_HEADERS} )

target_sources(eaw_battle
  PUBLIC
    FILE_SET "eaw_battle_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_BATTLE_HEADERS}
  PRIVATE
    ${EAW_BATTLE_SOURCES}
)

target_link_libraries(eaw_battle PUBLIC eaw_assets eaw_script eaw_simulation)
