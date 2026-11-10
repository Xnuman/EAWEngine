cmake_minimum_required( VERSION 3.30 )

if( NOT CMAKE_GENERATOR_PLATFORM STREQUAL x64 )
  message( FATAL_ERROR "Unsupported platform [${CMAKE_GENERATOR_PLATFORM}]" )
endif()

project( EAWEngine
    VERSION 0.1.0
    LANGUAGES C CXX
)

add_definitions( -DUNICODE -D_UNICODE )

set( CMAKE_VS_INCLUDE_INSTALL_TO_DEFAULT_BUILD ON )

option( EAW_BUILD_TESTS "Build EAWEngine tests" OFF )
option( EAW_BUILD_GUI "Build the Dear ImGui tool workbench" ON )

set( EAW_ENGINE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" )

set_property( GLOBAL PROPERTY USE_FOLDERS ON )

get_filename_component( EAW_ENGINE_BINARY_DIR "${EAW_ENGINE_ROOT_DIR}/executables/" ABSOLUTE )

set( CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${EAW_ENGINE_BINARY_DIR}" )
set( CMAKE_RUNTIME_OUTPUT_DIRECTORY "${EAW_ENGINE_BINARY_DIR}" )

# EAWEngine does not use C++ exceptions: recoverable failures return eaw::Status/eaw::Result and
# broken invariants end the process through eaw::fatal_error. Every target, Dear ImGui included,
# compiles with exception handling disabled (/EHs-c-) and a standard library configured without
# exceptions (_HAS_EXCEPTIONS=0). CMake's default MSVC flags enable, so it is removed here.
if( MSVC )
    string( REGEX REPLACE "/EH[a-z]*" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" )
    add_compile_options( $<$<COMPILE_LANGUAGE:CXX>:/EHs-c-> )
    add_compile_definitions( _HAS_EXCEPTIONS=0 )
endif( )

add_compile_definitions(
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

add_compile_options(/W4 /permissive- /utf-8)

if(WIN32)
    # Shaders are compiled offline into generated headers that embed the bytecode, so runtime
    # never invokes a shader compiler. DXC (shader model 6) serves Direct3D 12 and FXC (shader
    # model 5) the Direct3D 11 tool previews. Both ship with the Windows SDK selected for the
    # build; set EAW_DXC_EXECUTABLE / EAW_FXC_EXECUTABLE to override.
    get_filename_component(EAW_WINDOWS_KITS_ROOT "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
    set(EAW_WINDOWS_SDK_BIN_HINTS
        "${EAW_WINDOWS_KITS_ROOT}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        "${EAW_WINDOWS_KITS_ROOT}/bin/x64"
    )
    find_program(EAW_DXC_EXECUTABLE dxc HINTS ${EAW_WINDOWS_SDK_BIN_HINTS})
    find_program(EAW_FXC_EXECUTABLE fxc HINTS ${EAW_WINDOWS_SDK_BIN_HINTS})
    if(NOT EAW_DXC_EXECUTABLE OR NOT EAW_FXC_EXECUTABLE)
        message(FATAL_ERROR "dxc.exe and fxc.exe were not found in the Windows SDK (looked in ${EAW_WINDOWS_SDK_BIN_HINTS}). "
                            "Install a Windows 10/11 SDK or set EAW_DXC_EXECUTABLE and EAW_FXC_EXECUTABLE.")
    endif()
    set(EAW_GENERATED_SHADER_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
    file(MAKE_DIRECTORY "${EAW_GENERATED_SHADER_DIR}")

    # eaw_compile_shader(<target> <dxc|fxc> <source> <entry> <profile> <symbol>) compiles one
    # entry point of <source> (relative to the calling directory) into
    # ${EAW_GENERATED_SHADER_DIR}/<symbol>.h (a byte array named <symbol>).
    function(eaw_compile_shader target compiler source entry profile symbol)
        set(output "${EAW_GENERATED_SHADER_DIR}/${symbol}.h")
        set(input "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
        get_filename_component(include_dir "${input}" DIRECTORY)
        file(GLOB shader_headers CONFIGURE_DEPENDS "${include_dir}/*.hlsli")
        if(compiler STREQUAL "dxc")
            add_custom_command(
                OUTPUT "${output}"
                COMMAND "${EAW_DXC_EXECUTABLE}" -nologo -T ${profile} -E ${entry} -HV 2021 -WX -O3 -force-rootsig-ver rootsig_1_0 -Qstrip_debug -Qstrip_reflect
                        -I "${include_dir}" -Fh "${output}" -Vn ${symbol} "${input}"
                DEPENDS "${input}" ${shader_headers}
                COMMENT "DXC ${source} (${entry}, ${profile})"
                VERBATIM)
        else()
            add_custom_command(
                OUTPUT "${output}"
                COMMAND "${EAW_FXC_EXECUTABLE}" /nologo /T ${profile} /E ${entry} /WX /O3 /I "${include_dir}" /Fh "${output}" /Vn ${symbol} "${input}"
                DEPENDS "${input}" ${shader_headers}
                COMMENT "FXC ${source} (${entry}, ${profile})"
                VERBATIM)
        endif()
        target_sources(${target} PRIVATE "${output}")
        source_group("generated" FILES "${output}")
        target_include_directories(${target} PRIVATE "${EAW_GENERATED_SHADER_DIR}")
    endfunction()
endif()

if(EAW_BUILD_TESTS)
    # Before the subdirectories, so their add_test() calls reach ctest from the build root.
    enable_testing()
endif()

# Third-party code.
if(WIN32)
    add_subdirectory(third_party/imgui)
endif()
add_subdirectory(third_party/lua)

# Libraries, innermost first.
add_subdirectory(src/math)
add_subdirectory(src/core)
add_subdirectory(src/formats)
add_subdirectory(src/simulation)
add_subdirectory(src/script)
add_subdirectory(src/render)
if(WIN32)
    add_subdirectory(src/platform)
endif()
add_subdirectory(src/engine)
add_subdirectory(src/launcher)
if(EAW_BUILD_GUI AND WIN32)
    add_subdirectory(src/imgui_app)
endif()

# Programs and tools.
add_subdirectory(src/engine_app)
if(WIN32)
    add_subdirectory(src/launcher_app)
endif()
if(EAW_BUILD_GUI AND WIN32)
    add_subdirectory(src/dev_launcher)
    add_subdirectory(src/tools_gui)
endif()
add_subdirectory(src/asset_inspector)
add_subdirectory(src/dat_editor)

if(EAW_BUILD_TESTS)
    add_subdirectory(tests)
endif()
