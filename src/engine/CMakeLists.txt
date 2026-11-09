# Platform-independent engine orchestration: battles above the simulation, visual scene construction
# from decoded assets, presentation, cameras, and effects. It consumes the render contract
# (engine-native visual data) but no graphics API.
set(EAW_ENGINE_SOURCES
    asset_cache.cpp
    asset_cache.hpp
    attached_effects.cpp
    attached_effects.hpp
    battle.cpp
    battle.hpp
    battle_effects.cpp
    battle_effects.hpp
    battle_log.cpp
    battle_log.hpp
    battle_runner.cpp
    battle_runner.hpp
    camera_controller.cpp
    camera_controller.hpp
    camera_math.cpp
    camera_math.hpp
    combat_content.cpp
    combat_content.hpp
    debug_draw.cpp
    debug_draw.hpp
    object_type_properties.cpp
    particle_effects.cpp
    particle_effects.hpp
    particle_rules.cpp
    particle_rules.hpp
    projectile_visuals.cpp
    projectile_visuals.hpp
    rts_control.cpp
    rts_control.hpp
    sandbox_content.cpp
    sandbox_content.hpp
    sandbox_debug_lines.cpp
    sandbox_debug_lines.hpp
    sandbox_options.cpp
    sandbox_options.hpp
    scene_builder_internal.hpp
    scene_materials.cpp
    scene_models.cpp
    scene_particles.cpp
    scene_projectiles.cpp
    scene_presentation.cpp
    scene_presentation.hpp
    scene_terrain.cpp
    terrain_height.cpp
    terrain_height.hpp
    text_format.cpp
    text_format.hpp
    unit_animation.cpp
    unit_animation.hpp
    visual_map_builder.cpp
    visual_scene_builder.hpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_ENGINE_SOURCES})

add_library(eaw_engine STATIC ${EAW_ENGINE_SOURCES})
target_include_directories(eaw_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eaw_engine PUBLIC eaw_core eaw_simulation eaw_render_contract)
