# eaw_audio: the engine's audio contract (sounds, devices, categories), the spatial placement every
# backend shares, and the null device. Portable; no platform code.
# eaw_audio_devices: the backends (XAudio2 on Windows, FMOD when EAW_AUDIO_FMOD is on) and the
# choice between them.

add_library(eaw_audio STATIC)

set(HEADERS_DIR "include/audio")
set(SOURCES_DIR "src")

set(EAW_AUDIO_HEADERS
    ${HEADERS_DIR}/audio_device.hpp
    ${HEADERS_DIR}/null_audio_device.hpp
    ${HEADERS_DIR}/sound.hpp
    ${HEADERS_DIR}/spatial_audio.hpp
)

set(EAW_AUDIO_SOURCES
    ${SOURCES_DIR}/null_audio_device.cpp
    ${SOURCES_DIR}/spatial_audio.cpp
)

source_group("" FILES ${EAW_AUDIO_HEADERS} ${EAW_AUDIO_SOURCES})

target_sources(eaw_audio
  PUBLIC
    FILE_SET "eaw_audio_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_AUDIO_HEADERS}
  PRIVATE
    ${EAW_AUDIO_SOURCES}
)

target_link_libraries(eaw_audio PUBLIC eaw_core eaw_math)

add_library(eaw_audio_devices STATIC)

set(EAW_AUDIO_DEVICES_HEADERS
    ${HEADERS_DIR}/audio_devices.hpp
)

set(EAW_AUDIO_DEVICES_SOURCES
    ${SOURCES_DIR}/audio_devices.cpp
)
if(WIN32)
    list(APPEND EAW_AUDIO_DEVICES_SOURCES
        ${SOURCES_DIR}/xaudio2_audio_device.cpp
        ${SOURCES_DIR}/xaudio2_audio_device.hpp
    )
endif()
if(EAW_AUDIO_FMOD)
    list(APPEND EAW_AUDIO_DEVICES_SOURCES
        ${SOURCES_DIR}/fmod_audio_device.cpp
        ${SOURCES_DIR}/fmod_audio_device.hpp
    )
endif()

source_group("" FILES ${EAW_AUDIO_DEVICES_HEADERS} ${EAW_AUDIO_DEVICES_SOURCES})

target_sources(eaw_audio_devices
  PUBLIC
    FILE_SET "eaw_audio_devices_headers"
      TYPE HEADERS
      BASE_DIRS "include/"
      FILES ${EAW_AUDIO_DEVICES_HEADERS}
  PRIVATE
    ${EAW_AUDIO_DEVICES_SOURCES}
)

target_link_libraries(eaw_audio_devices PUBLIC eaw_audio)
if(WIN32)
    # XAudio2 2.9 is part of Windows 10 and later (xaudio2.lib in the Windows SDK).
    target_link_libraries(eaw_audio_devices PRIVATE xaudio2)
endif()

if(EAW_AUDIO_FMOD)
    # FMOD Core from a local SDK (its license does not allow committing or fetching it): the
    # installer's layout, api/core/inc and api/core/lib/x64.
    if(NOT EXISTS "${FMOD_ROOT}/api/core/inc/fmod.hpp")
        message(FATAL_ERROR "EAW_AUDIO_FMOD is on, but FMOD_ROOT (${FMOD_ROOT}) does not hold the FMOD Core API (api/core/inc/fmod.hpp)")
    endif()
    set(EAW_FMOD_LIBRARY_DIR "${FMOD_ROOT}/api/core/lib/x64")
    target_include_directories(eaw_audio_devices PRIVATE "${FMOD_ROOT}/api/core/inc")
    target_compile_definitions(eaw_audio_devices PRIVATE EAW_AUDIO_FMOD)
    target_link_libraries(eaw_audio_devices PRIVATE "${EAW_FMOD_LIBRARY_DIR}/fmod_vc.lib")
    # The FMOD runtime sits next to the executables.
    add_custom_command(TARGET eaw_audio_devices POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${EAW_ENGINE_BINARY_DIR}/$<CONFIG>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EAW_FMOD_LIBRARY_DIR}/fmod.dll" "${EAW_ENGINE_BINARY_DIR}/$<CONFIG>"
    )
endif()
