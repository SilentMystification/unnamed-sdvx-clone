# Root CMake file
cmake_minimum_required(VERSION 3.12)
set(CMAKE_CXX_STANDARD 17 CACHE STRING "v")
set(CMAKE_CXX_STANDARD_REQUIRED True)
#set(VCPKG_CRT_LINKAGE static)
#set(VCPKG_LIBRARY_LINKAGE static)
#set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "Vcpkg target triplet (ex. x86-windows)")

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "")
    message("Found vcpkg root '$ENV{VCPKG_ROOT}'")
elseif(WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "Could not find vcpkg root")
endif()

option(BUILD_SHARED_LIBS "Build libraries as shared libraries" OFF)
option(USC_GNU_WERROR "Set Werror for gcc." OFF)
option(USE_SYSTEM_CPR "Use system CPR" OFF)

# Render/window backend selection.
# GL3 (default): desktop OpenGL 3.3 core, used on Windows/Linux/macOS-Intel+AppleSilicon.
# GLES3: embedded target, equivalent to the legacy EMBEDDED option below.
# GL1_LEGACY: fixed-function OpenGL 1.x, for pre-shader hardware (e.g. macOS 10.4/PowerPC).
set(USC_RENDER_BACKEND "GL3" CACHE STRING "Render backend (GL3, GLES3, GL1_LEGACY)")
set_property(CACHE USC_RENDER_BACKEND PROPERTY STRINGS GL3 GLES3 GL1_LEGACY)

# SDL2 (default): current desktop windowing.
# CARBON: Carbon+AGL windowing for macOS 10.4/PowerPC, where SDL2 does not run.
set(USC_WINDOW_BACKEND "SDL2" CACHE STRING "Window backend (SDL2, CARBON)")
set_property(CACHE USC_WINDOW_BACKEND PROPERTY STRINGS SDL2 CARBON)

if(NOT USC_RENDER_BACKEND MATCHES "^(GL3|GLES3|GL1_LEGACY)$")
    message(FATAL_ERROR "USC_RENDER_BACKEND must be one of GL3, GLES3, GL1_LEGACY (got '${USC_RENDER_BACKEND}')")
endif()
if(NOT USC_WINDOW_BACKEND MATCHES "^(SDL2|CARBON)$")
    message(FATAL_ERROR "USC_WINDOW_BACKEND must be one of SDL2, CARBON (got '${USC_WINDOW_BACKEND}')")
endif()

project(USC VERSION 0.6.0)
if(WIN32 AND ${CMAKE_VERSION} VERSION_GREATER "3.12")
    cmake_policy(SET CMP0079 NEW)
endif()
if (${CMAKE_VERSION} VERSION_GREATER "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.12)
endif()

# NOTE: everything below must come after project() - a CMAKE_TOOLCHAIN_FILE's own forced
# values for USC_RENDER_BACKEND/USC_WINDOW_BACKEND (see
# cmake/Toolchains/ppc-apple-darwin8.cmake) only actually land in the cache once
# project()/enable_language() first processes the toolchain file; before that point in the
# script these two still read their un-forced "GL3"/"SDL2" option() defaults regardless of
# the toolchain file. Gating discord-rpc ahead of project() previously meant a
# toolchain-forced GL1_LEGACY/CARBON build still saw the pre-toolchain GL3/SDL2 defaults
# here and left discord-rpc (and its -DUSC_ENABLE_DISCORD_RPC compile definition) on, even
# though third_party/CMakeLists.txt's own later, post-project() check correctly disabled
# building discord-rpc itself - Main/ would then fail to find discord_rpc.h at all.
message(STATUS "USC render backend: ${USC_RENDER_BACKEND}")
message(STATUS "USC window backend: ${USC_WINDOW_BACKEND}")

option(USC_ENABLE_DISCORD_RPC "Enable Discord Rich Presence integration" ON)
if(USC_WINDOW_BACKEND STREQUAL "CARBON" OR USC_RENDER_BACKEND STREQUAL "GL1_LEGACY")
    # No PPC/10.4 port of discord-rpc exists; there's no Discord client for this OS anyway.
    set(USC_ENABLE_DISCORD_RPC OFF)
endif()
if(USC_ENABLE_DISCORD_RPC)
    add_definitions(-DUSC_ENABLE_DISCORD_RPC)
endif()
# Project configurations
set(CMAKE_CONFIGURATION_TYPES Debug Release)
set(CMAKE_DEBUG_POSTFIX _Debug)
set(CMAKE_RELEASE_POSTFIX _Release)
execute_process(COMMAND git log -1 --date=short --format="%cd_%h"
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE GIT_DATE_HASH
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE)


# Set output folders
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${PROJECT_SOURCE_DIR}/bin )
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${PROJECT_SOURCE_DIR}/bin )
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${PROJECT_SOURCE_DIR}/lib )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

set(CMAKE_MACOSX_RPATH 1)

# Set library paths for MacOS
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(MACOSX TRUE)
endif()

# Homebrew provides freetype/libarchive/etc on a native macOS build host; cross-compiling
# (e.g. the GL1_LEGACY/CARBON PPC target - see cmake/Toolchains/ppc-apple-darwin8.cmake)
# has no Homebrew at all on the Linux/etc host doing the compiling, so this whole block
# is native-macOS-only.
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin" AND NOT CMAKE_CROSSCOMPILING)
    # Set homebrew's include dir
    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    include_directories("${HOMEBREW_PREFIX}/include")

    # Libarchive is shipped as a keg so we must get its path manually
    execute_process(
        COMMAND brew --prefix libarchive
        OUTPUT_VARIABLE LIBARCHIVE_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    set(LibArchive_INCLUDE_DIR "${LIBARCHIVE_PREFIX}/include")
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin" AND CMAKE_CROSSCOMPILING)
    # Cross-compile equivalent of the Homebrew include_directories() above: several
    # targets (Graphics for libpng16/png.h, Audio for vorbis/*.h) never explicitly add
    # find_package()'s own *_INCLUDE_DIRS to their target include paths - on a native
    # macOS build that silently worked because Homebrew's global include_directories()
    # covered it. USC_PPC_DEPS_ROOT (see cmake/Toolchains/ppc-apple-darwin8.cmake) is
    # where those cross-built dependencies actually live for this target.
    include_directories("${USC_PPC_DEPS_ROOT}/include")
endif()

# Set path for discord-rpc dependency so that it doesn't download old version
if (USC_ENABLE_DISCORD_RPC AND ${CMAKE_VERSION} VERSION_GREATER "4.0")
    file(
        MAKE_DIRECTORY
        ${PROJECT_SOURCE_DIR}/third_party/discord-rpc/thirdparty/
    )
    file(
        CREATE_LINK
        ${PROJECT_SOURCE_DIR}/third_party/rapidjson
        ${PROJECT_SOURCE_DIR}/third_party/discord-rpc/thirdparty/rapidjson
        SYMBOLIC
    )
endif()

# Set folder where to find FindXXX.cmake and
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/Modules/")

# Find external dependencies
find_package(Freetype REQUIRED)
find_package(ZLIB REQUIRED)
if(USC_WINDOW_BACKEND STREQUAL "SDL2")
    find_package(SDL2 REQUIRED)
endif()
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)
find_package(Vorbis REQUIRED)
find_package(OGG REQUIRED)
find_package(LibArchive REQUIRED)
find_package(Iconv REQUIRED)

if(USC_WINDOW_BACKEND STREQUAL "CARBON")
    find_library(CARBON_FRAMEWORK Carbon REQUIRED)
    find_library(AGL_FRAMEWORK AGL REQUIRED)
    find_library(APPLICATION_SERVICES_FRAMEWORK ApplicationServices REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
endif()

# All projects use unicode define
# this is mainly for windows functions either being defined to call A or W prefixed functions
add_definitions(-DUNICODE -D_UNICODE)

#https://stackoverflow.com/questions/60041896/reuse-target-compile-options-from-variable-for-multiple-targets-cmake/60047012#60047012
add_library(cc-common INTERFACE)

if(WIN32)
    target_compile_options(cc-common INTERFACE /Zi)
endif()

OPTION(EMBEDDED "Enable embedded build (legacy alias for USC_RENDER_BACKEND=GLES3)" OFF)

if(EMBEDDED)
    set(USC_RENDER_BACKEND "GLES3")
endif()

if(USC_RENDER_BACKEND STREQUAL "GLES3")
	message("Enabling embedded (GLES3) render backend")
    add_definitions(-DEMBEDDED)
elseif(USC_RENDER_BACKEND STREQUAL "GL1_LEGACY")
    message("Enabling GL1_LEGACY (fixed-function) render backend")
    add_definitions(-DUSC_GL1_LEGACY)
elseif(USC_RENDER_BACKEND STREQUAL "GL3")
    add_definitions(-DUSC_SEPARATE_SHADER_OBJECTS)
endif()

OPTION(CRASHDUMP "Enable collecting crash dumps" ON)
if(CRASHDUMP)
    message("Enabling crash dumps")
    add_definitions(-DCRASHDUMP)
endif()

OPTION(ASAN "Build With ASAN" OFF)
if(ASAN)
    target_compile_options(cc-common INTERFACE
        -fsanitize=address
        -fno-omit-frame-pointer
    )
endif()

# Include macros
include(${PROJECT_SOURCE_DIR}/cmake/Macros.cmake)

if(USC_RENDER_BACKEND STREQUAL "GL1_LEGACY")
    # cpr vendors/fetches curl, which auto-selects Secure Transport as its SSL backend on
    # any Apple target (correct) but its NTLM auth code unconditionally wants
    # CommonCrypto/CommonCryptor.h - only added in the 10.5 SDK, absent on our 10.4
    # target's SDK (it only ships CommonDigest.h). NTLM isn't needed for USC's HTTPS API
    # calls; disabling it sidesteps the missing header rather than needing it.
    set(CURL_DISABLE_NTLM ON CACHE BOOL "" FORCE)
endif()

# Sub-Project directories
add_subdirectory(third_party)
add_subdirectory(Shared)
add_subdirectory(Graphics)
add_subdirectory(Main)
add_subdirectory(Audio)
add_subdirectory(Beatmap)
add_subdirectory(GUI)

# Unit test projects
add_subdirectory(Tests)
add_subdirectory(Tests.Shared)
add_subdirectory(Tests.Game)

# Enabled project filters on windows
if(MSVC)
    #updater for windows
    add_subdirectory(updater)

    # Use filters in VS projects
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)

    # Set usc-game as default target in VS
    set_directory_properties(PROPERTY VS_STARTUP_PROJECT usc-game)

    # Put all third party libraries in a seperate folder in the VS solution
    set_target_properties(cpr PROPERTIES FOLDER "Third Party")
    set_target_properties(nanovg PROPERTIES FOLDER "Third Party")
    set_target_properties(sqlite3 PROPERTIES FOLDER "Third Party")
    if(USC_ENABLE_DISCORD_RPC)
        set_target_properties(discord-rpc PROPERTIES FOLDER "Third Party")
    endif()
    set_target_properties(rapidjson PROPERTIES FOLDER "Third Party")
    set_target_properties(minimp3 PROPERTIES FOLDER "Third Party")
    set_target_properties(soundtouch PROPERTIES FOLDER "Third Party")
    set_target_properties(lua PROPERTIES FOLDER "Third Party")
    set_target_properties(GLEW PROPERTIES FOLDER "Third Party")

    # My libraries in the libraries folder
    set_target_properties(Shared PROPERTIES FOLDER Libraries)
    set_target_properties(Graphics PROPERTIES FOLDER Libraries)
    set_target_properties(Audio PROPERTIES FOLDER Libraries)
    set_target_properties(Beatmap PROPERTIES FOLDER Libraries)
    set_target_properties(GUI PROPERTIES FOLDER Libraries)

    # Unit tests
    set_target_properties(Tests PROPERTIES FOLDER "Tests")
    set_target_properties(Tests.Shared PROPERTIES FOLDER "Tests")
    set_target_properties(Tests.Game PROPERTIES FOLDER "Tests")

endif(MSVC)

if(APPLE)
    # usc-game is a MACOSX_BUNDLE target there (Main/CMakeLists.txt) - install() requires
    # an explicit BUNDLE DESTINATION for those, RUNTIME alone isn't enough.
    install(TARGETS usc-game BUNDLE DESTINATION .)
else()
    install(TARGETS usc-game RUNTIME)
endif()
install(DIRECTORY bin/audio DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/unnamed-sdvx-clone)
install(DIRECTORY bin/fonts DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/unnamed-sdvx-clone)
install(DIRECTORY bin/LightPlugins DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/unnamed-sdvx-clone)
install(DIRECTORY bin/skins DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/unnamed-sdvx-clone)
