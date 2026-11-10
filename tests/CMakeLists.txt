# EAWEngineTests: each module is test_<module>.cpp, registers its tests under that name, and gets
# its own ctest entry; add new modules to EAW_TEST_MODULES.
set(EAW_TEST_MODULES
    battle
    combat
    effects
    flight
    formats
    foundation
    launcher
    math
    navigation
    presentation
    render
    sandbox
    scene
    script
    simulation
    vfs
    xml
)

set(EAW_TEST_SOURCES
    test_main.cpp
    test_support.cpp
    test_support.hpp
)
foreach(test_module IN LISTS EAW_TEST_MODULES)
    list(APPEND EAW_TEST_SOURCES test_${test_module}.cpp)
endforeach()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EAW_TEST_SOURCES})

add_executable(EAWEngineTests ${EAW_TEST_SOURCES})
target_link_libraries(EAWEngineTests PRIVATE eaw_core_transitional eaw_engine eaw_launch eaw_math eaw_script)

# EAWEngineTests runs every module when given none.
foreach(test_module IN LISTS EAW_TEST_MODULES)
    add_test(NAME EAWEngineTests.${test_module} COMMAND EAWEngineTests ${test_module})
endforeach()
