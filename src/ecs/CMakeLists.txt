project(ecs)

# ecs: entities, components, archetypes, views, systems, singletons, and worlds (after Singe's ecs),
# with types registered through rti. It holds EAW_RTI_REGISTRATION blocks, so it links rti whole and
# programs link it whole too ($<LINK_LIBRARY:WHOLE_ARCHIVE,ecs>). component_pool.hpp and
# system_view.hpp are the simulation's current storage, kept until it moves onto this library.
add_library(ecs STATIC)

set(HEADERS_DIR "include/ecs")
set(SOURCES_DIR "src")

set(ECS_HEADERS
    ${HEADERS_DIR}/archetype.hpp
    ${HEADERS_DIR}/component.hpp
    ${HEADERS_DIR}/component_pool.hpp
    ${HEADERS_DIR}/component_registrator.hpp
    ${HEADERS_DIR}/entity.hpp
    ${HEADERS_DIR}/entity_manager.hpp
    ${HEADERS_DIR}/entity_table.hpp
    ${HEADERS_DIR}/isystem.hpp
    ${HEADERS_DIR}/modifiers.hpp
    ${HEADERS_DIR}/singleton.hpp
    ${HEADERS_DIR}/system.hpp
    ${HEADERS_DIR}/system_view.hpp
    ${HEADERS_DIR}/view.hpp
    ${HEADERS_DIR}/world.hpp
)

set(ECS_SOURCES
    ${SOURCES_DIR}/archetype.cpp
    ${SOURCES_DIR}/component.cpp
    ${SOURCES_DIR}/component_pool.cpp
    ${SOURCES_DIR}/entity_manager.cpp
    ${SOURCES_DIR}/entity_table.cpp
    ${SOURCES_DIR}/world.cpp
)

source_group("" FILES ${ECS_HEADERS} ${ECS_SOURCES})

target_sources(ecs
  PUBLIC
    FILE_SET "ecs_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${ECS_HEADERS}
  PRIVATE
    ${ECS_SOURCES}
)

target_link_libraries(ecs
  PUBLIC
     eaw_core
     eaw_jobs
     eaw_math
     junk
     $<LINK_LIBRARY:WHOLE_ARCHIVE,rti>)

set_target_properties(ecs PROPERTIES FOLDER "core")
