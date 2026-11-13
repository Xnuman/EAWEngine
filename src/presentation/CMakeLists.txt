# eaw_presentation: A running battle presented: snapshots to drawn instances and frames, rigs and unit animation,
# particles, attached and battle effects, projectile visuals, battle sound, and debug drawing. Never
# feeds back into the battle.

add_library(eaw_presentation STATIC)

set(HEADERS_DIR "include/presentation")
set(SOURCES_DIR "src")

set(EAW_PRESENTATION_HEADERS
    ${HEADERS_DIR}/attached_effects.hpp
    ${HEADERS_DIR}/battle_audio.hpp
    ${HEADERS_DIR}/battle_effects.hpp
    ${HEADERS_DIR}/battle_frame.hpp
    ${HEADERS_DIR}/damage_states.hpp
    ${HEADERS_DIR}/debris.hpp
    ${HEADERS_DIR}/debug_draw.hpp
    ${HEADERS_DIR}/entity_look.hpp
    ${HEADERS_DIR}/map_weather.hpp
    ${HEADERS_DIR}/particle_effects.hpp
    ${HEADERS_DIR}/projectile_visuals.hpp
    ${HEADERS_DIR}/scene_presentation.hpp
    ${HEADERS_DIR}/unit_animation.hpp
    ${HEADERS_DIR}/visual_rig.hpp
)

set(EAW_PRESENTATION_SOURCES
    ${SOURCES_DIR}/attached_effects.cpp
    ${SOURCES_DIR}/battle_audio.cpp
    ${SOURCES_DIR}/battle_effects.cpp
    ${SOURCES_DIR}/battle_frame.cpp
    ${SOURCES_DIR}/damage_states.cpp
    ${SOURCES_DIR}/debris.cpp
    ${SOURCES_DIR}/debug_draw.cpp
    ${SOURCES_DIR}/map_weather.cpp
    ${SOURCES_DIR}/particle_effects.cpp
    ${SOURCES_DIR}/projectile_visuals.cpp
    ${SOURCES_DIR}/scene_presentation.cpp
    ${SOURCES_DIR}/unit_animation.cpp
    ${SOURCES_DIR}/visual_rig.cpp
)

source_group( "" FILES ${EAW_PRESENTATION_SOURCES} ${EAW_PRESENTATION_HEADERS} )

target_sources(eaw_presentation
  PUBLIC
    FILE_SET "eaw_presentation_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_PRESENTATION_HEADERS}
  PRIVATE
    ${EAW_PRESENTATION_SOURCES}
)

target_link_libraries(eaw_presentation PUBLIC eaw_audio eaw_battle eaw_render_contract)
