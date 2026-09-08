# Audio library

set(INCROOT ${CMAKE_CURRENT_SOURCE_DIR}/include/Audio/)
set(SRCROOT ${CMAKE_CURRENT_SOURCE_DIR}/src/)
set(PCHROOT ${CMAKE_CURRENT_SOURCE_DIR}/)

file(GLOB INC "${INCROOT}/*.hpp")
source_group("Public Headers" FILES ${INC})

if(APPLE)
    # This project only uses miniaudio (AudioStreamMa.cpp) for its decoders - actual
    # device I/O goes through the dedicated AudioOutput_WASAPI.cpp/AudioOutput_SDL.cpp/
    # AudioOutput_CoreAudio.cpp backends instead. miniaudio's own CoreAudio device
    # backend assumes the modern AudioComponent/AUHAL API (10.6+), which our 10.4 target
    # doesn't have; disabling it removes dead-weight device code we never call. Must be
    # a compile definition (not a local #define in one .cpp) - miniaudio's
    # platform-detection block runs once behind an include guard that both
    # AudioStreamMa.hpp's declarations-only include and AudioStreamMa.cpp's
    # implementation include share, so it needs to be set before either's first
    # #include "miniaudio.h", not just the second one.
    add_compile_definitions(MA_NO_COREAUDIO)
endif()

file(GLOB SRC "${SRCROOT}/*.cpp" "${SRCROOT}/*.hpp")
source_group("Sources" FILES ${SRC})

# Platform specific source files. The Carbon/10.4 window backend implies the CoreAudio
# audio backend, same as it implies GL1_LEGACY for rendering - SDL2 (Unix/*.cpp) doesn't
# run there any more than it does for windowing (SDL2 needs 10.6+/Cocoa).
if(WIN32)
    file(GLOB PLATFORM_SRC "${SRCROOT}/Windows/*.cpp")
elseif(USC_WINDOW_BACKEND STREQUAL "CARBON")
    file(GLOB PLATFORM_SRC "${SRCROOT}/Mac/*.cpp")
else()
    file(GLOB PLATFORM_SRC "${SRCROOT}/Unix/*.cpp")
endif()
source_group("Sources\\Platform" FILES ${PLATFORM_SRC})

set(AUDIO_SRC ${SRC} ${INC} ${PLATFORM_SRC})

set(PCH_SRC ${PCHROOT}/stdafx.cpp)
set(PCH_INC ${PCHROOT}/stdafx.h)
set(PCH_FILES ${PCH_SRC} ${PCH_INC})
source_group("" FILES ${PCH_FILES})

# Compiler stuff
enable_precompiled_headers("${AUDIO_SRC}" "${PCH_SRC}")

add_library(Audio ${AUDIO_SRC} ${PCH_FILES})
target_compile_features(Audio PUBLIC cxx_std_17)
target_include_directories(Audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(Audio PRIVATE
    ${SRCROOT}
    ${INCROOT}
    ${PCHROOT}
)

#set_werror(Audio)
#Special case for audio
#if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND USC_GNU_WERROR)
#    target_compile_options(Audio PUBLIC -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-parameter -Wno-unused-function -Wno-maybe-uninitialized)
#endif()

# Dependencies
## Local Dependencies
target_link_libraries(Audio Shared)

## 3rd Party dependencies
### Find dependencies roots
if(USC_WINDOW_BACKEND STREQUAL "SDL2")
    target_include_directories(Audio PUBLIC ${SDL2_INCLUDE_DIR})
elseif(USC_WINDOW_BACKEND STREQUAL "CARBON")
    # Audio/stdafx.h includes <SDL2/SDL.h> unconditionally (its precompiled header, used
    # regardless of which platform's AudioOutput_*.cpp actually gets compiled) - same
    # vendored-types-only headers Graphics uses for this backend, see
    # Graphics/include/backends/window/carbon-sdl-headers/README.md. AudioOutput_Mac.cpp
    # (CoreAudio) itself never calls a real SDL function.
    target_include_directories(Audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../Graphics/include/backends/window/carbon-sdl-headers)
endif()
### Add dependencies to linkage
target_link_libraries(Audio soundtouch)
target_link_libraries(Audio minimp3)
target_link_libraries(Audio miniaudio)
if(USC_WINDOW_BACKEND STREQUAL "SDL2")
    target_link_libraries(Audio ${SDL2_LIBRARY})
elseif(USC_WINDOW_BACKEND STREQUAL "CARBON")
    find_library(CORE_AUDIO_FRAMEWORK CoreAudio REQUIRED)
    target_link_libraries(Audio ${CORE_AUDIO_FRAMEWORK})
endif()
target_link_libraries(Audio ${OGG_LIBRARIES})
target_link_libraries(Audio ${Vorbis_LIBRARIES})

target_link_libraries(Audio cc-common)

# Enable multiprocess compiling
if(MSVC)
    target_compile_options(Audio PRIVATE /MP)
endif(MSVC)