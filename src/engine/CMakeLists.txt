# eaw_engine: platform-independent engine orchestration: battles above the simulation, visual scene
# construction from decoded assets, presentation, cameras, and effects. It consumes the render
# contract (engine-native visual data) but no graphics API.

add_library(eaw_engine STATIC)

set(HEADERS_DIR "include/engine")
set(SOURCES_DIR "src")

# Battles: orders, logs, the runtime, scripts, the battle side of maps, and gameplay data from XML.
set(EAW_ENGINE_BATTLE
    ${HEADERS_DIR}/battle.hpp
    ${HEADERS_DIR}/battle_log.hpp
    ${HEADERS_DIR}/battle_map.hpp
    ${HEADERS_DIR}/battle_map_build.hpp
    ${HEADERS_DIR}/battle_runtime.hpp
    ${HEADERS_DIR}/battle_scripts.hpp
    ${HEADERS_DIR}/combat_content.hpp
    ${HEADERS_DIR}/object_types.hpp
    ${SOURCES_DIR}/battle.cpp
    ${SOURCES_DIR}/battle_log.cpp
    ${SOURCES_DIR}/battle_map_builder.cpp
    ${SOURCES_DIR}/battle_runtime.cpp
    ${SOURCES_DIR}/battle_scripts.cpp
    ${SOURCES_DIR}/combat_content.cpp
    ${SOURCES_DIR}/object_types.cpp
)

# Scene building: decoded assets to engine-native visual data, and what XML types say about their look.
set(EAW_ENGINE_SCENE
    ${HEADERS_DIR}/scene_builder_internal.hpp
    ${HEADERS_DIR}/visual_scene_builder.hpp
    ${SOURCES_DIR}/scene_materials.cpp
    ${SOURCES_DIR}/scene_models.cpp
    ${SOURCES_DIR}/scene_particles.cpp
    ${SOURCES_DIR}/scene_projectiles.cpp
    ${SOURCES_DIR}/scene_terrain.cpp
    ${SOURCES_DIR}/type_looks.cpp
    ${SOURCES_DIR}/visual_map_builder.cpp
)

# Presentation: simulation snapshots to drawn instances, animation, effects, and sound.
set(EAW_ENGINE_PRESENTATION
    ${HEADERS_DIR}/attached_effects.hpp
    ${HEADERS_DIR}/battle_audio.hpp
    ${HEADERS_DIR}/battle_effects.hpp
    ${HEADERS_DIR}/battle_frame.hpp
    ${HEADERS_DIR}/debug_draw.hpp
    ${HEADERS_DIR}/entity_look.hpp
    ${HEADERS_DIR}/particle_effects.hpp
    ${HEADERS_DIR}/projectile_visuals.hpp
    ${HEADERS_DIR}/scene_presentation.hpp
    ${HEADERS_DIR}/unit_animation.hpp
    ${HEADERS_DIR}/visual_rig.hpp
    ${SOURCES_DIR}/attached_effects.cpp
    ${SOURCES_DIR}/battle_audio.cpp
    ${SOURCES_DIR}/battle_effects.cpp
    ${SOURCES_DIR}/battle_frame.cpp
    ${SOURCES_DIR}/debug_draw.cpp
    ${SOURCES_DIR}/particle_effects.cpp
    ${SOURCES_DIR}/projectile_visuals.cpp
    ${SOURCES_DIR}/scene_presentation.cpp
    ${SOURCES_DIR}/unit_animation.cpp
    ${SOURCES_DIR}/visual_rig.cpp
)

# Input and cameras: camera controllers, RTS control, and picking on the terrain height field.
set(EAW_ENGINE_INPUT
    ${HEADERS_DIR}/camera_controller.hpp
    ${HEADERS_DIR}/camera_math.hpp
    ${HEADERS_DIR}/rts_control.hpp
    ${HEADERS_DIR}/terrain_height.hpp
    ${SOURCES_DIR}/camera_controller.cpp
    ${SOURCES_DIR}/camera_math.cpp
    ${SOURCES_DIR}/rts_control.cpp
    ${SOURCES_DIR}/terrain_height.cpp
)

# The sandbox's testable parts and the headless battle command: options, background content
# builds, debug lines, text format, and headless runs.
set(EAW_ENGINE_SANDBOX
    ${HEADERS_DIR}/battle_runner.hpp
    ${HEADERS_DIR}/sandbox_content.hpp
    ${HEADERS_DIR}/sandbox_debug_lines.hpp
    ${HEADERS_DIR}/sandbox_options.hpp
    ${HEADERS_DIR}/text_format.hpp
    ${SOURCES_DIR}/battle_runner.cpp
    ${SOURCES_DIR}/sandbox_content.cpp
    ${SOURCES_DIR}/sandbox_debug_lines.cpp
    ${SOURCES_DIR}/sandbox_options.cpp
    ${SOURCES_DIR}/text_format.cpp
)

set(EAW_ENGINE_FILES
    ${EAW_ENGINE_BATTLE}
    ${EAW_ENGINE_SCENE}
    ${EAW_ENGINE_PRESENTATION}
    ${EAW_ENGINE_INPUT}
    ${EAW_ENGINE_SANDBOX}
)
# Public headers are the files under HEADERS_DIR; the rest (sources, private headers) are private.
set(EAW_ENGINE_HEADERS ${EAW_ENGINE_FILES})
list(FILTER EAW_ENGINE_HEADERS INCLUDE REGEX "^${HEADERS_DIR}/")
set(EAW_ENGINE_SOURCES ${EAW_ENGINE_FILES})
list(FILTER EAW_ENGINE_SOURCES EXCLUDE REGEX "^${HEADERS_DIR}/")

source_group("battle" FILES ${EAW_ENGINE_BATTLE})
source_group("scene" FILES ${EAW_ENGINE_SCENE})
source_group("presentation" FILES ${EAW_ENGINE_PRESENTATION})
source_group("input" FILES ${EAW_ENGINE_INPUT})
source_group("sandbox" FILES ${EAW_ENGINE_SANDBOX})

target_sources(eaw_engine
  PUBLIC
    FILE_SET "eaw_engine_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_ENGINE_HEADERS}
  PRIVATE
    ${EAW_ENGINE_SOURCES}
)

target_link_libraries(eaw_engine PUBLIC eaw_assets eaw_audio eaw_script eaw_simulation eaw_render_contract)
