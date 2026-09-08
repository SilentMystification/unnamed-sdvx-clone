# Main Project

# Find files used for project

set(SRCROOT ${CMAKE_CURRENT_SOURCE_DIR}/src/)
set(INCROOT ${CMAKE_CURRENT_SOURCE_DIR}/include/)
set(PCHROOT ${CMAKE_CURRENT_SOURCE_DIR}/)

file(GLOB SRC "${SRCROOT}/*.cpp" "${SRCROOT}/*.hpp")
# SDLCompat_CARBON.cpp provides real SDL2 function bodies Main/ calls directly (timing,
# semaphores, dynamic loading, keyboard layout, ...) for the Carbon/10.4 build, where
# actual SDL2 doesn't exist - excluded from the generic glob so it isn't also compiled
# (and duplicate-symbol-clash with real SDL2) on the default SDL2 backend.
list(FILTER SRC EXCLUDE REGEX ".*_CARBON\\.cpp$")
if(USC_WINDOW_BACKEND STREQUAL "CARBON")
    list(APPEND SRC "${SRCROOT}/SDLCompat_CARBON.cpp")
endif()
source_group("Source" FILES ${SRC})

file(GLOB INC "${INCROOT}/*.hpp")
source_group("Include" FILES ${INC})

file(GLOB AUDIO_SRC "${SRCROOT}/Audio/*.cpp" "${SRCROOT}/Audio/*.hpp")
source_group("Source\\Audio" FILES ${AUDIO_SRC})

file(GLOB AUDIO_INC "${INCROOT}/Audio/*.hpp")
source_group("Include\\Audio" FILES ${AUDIO_INC})

file(GLOB FAST_GUI_SRC "${SRCROOT}/FastGUI/*.cpp" "${SRCROOT}/FastGUI/*.hpp")
source_group("Source\\FastGUI" FILES ${FAST_GUI_SRC})

file(GLOB FAST_GUI_INC "${INCROOT}/FastGUI/*.hpp")
source_group("Include\\FastGUI" FILES ${FAST_GUI_INC})

file(GLOB NUK_INC "${INCROOT}/nuklear/*.h")
source_group("Include\\nuklear" FILES ${NUK_INC})

file(GLOB NUK_INC "${INCROOT}/nuklear/*.h")
source_group("Include\\nuklear" FILES ${NUK_INC})

file(GLOB LIGHT_INC "${INCROOT}/LightPlugin/*.h")
source_group("Include\\LightPlugin" FILES ${LIGHT_INC})

set(MAIN_SRC ${SRC} ${INC} ${AUDIO_SRC} ${AUDIO_INC} ${FAST_GUI_SRC} ${FAST_GUI_INC} ${NUK_INC} ${LIGHT_INC})

file(GLOB RESOURCE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/Resource.rc)
source_group("" FILES ${RESOURCE_SRC})

set(PCH_SRC ${PCHROOT}/stdafx.cpp)
set(PCH_INC ${PCHROOT}/stdafx.h)
set(PCH_FILES ${PCH_SRC} ${PCH_INC})
source_group("" FILES ${PCH_FILES})

# Compiler stuff
enable_precompiled_headers("${MAIN_SRC}" ${PCH_SRC})

include_directories(Audio GUI .)
add_executable(usc-game ${MAIN_SRC} ${RESOURCE_SRC} ${PCH_FILES})
target_include_directories(usc-game PRIVATE
    ${SRCROOT}
    ${SRCROOT}/GUI
    ${SRCROOT}/Audio
    ${INCROOT}
    ${INCROOT}/GUI
    ${INCROOT}/Audio
)
target_compile_features(usc-game PUBLIC cxx_std_17)
target_compile_definitions(usc-game PRIVATE VERSION_MINOR=${PROJECT_VERSION_MINOR})
target_compile_definitions(usc-game PRIVATE VERSION_MAJOR=${PROJECT_VERSION_MAJOR})
target_compile_definitions(usc-game PRIVATE VERSION_PATCH=${PROJECT_VERSION_PATCH})
find_package (Git)
if(GIT_FOUND)
    target_compile_definitions(usc-game PRIVATE GIT_COMMIT=${GIT_DATE_HASH})
    execute_process(
      COMMAND git rev-parse --abbrev-ref HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE GIT_BRANCH
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    target_compile_definitions(usc-game PRIVATE GIT_BRANCH="${GIT_BRANCH}")
else(GIT_FOUND)
    target_compile_definitions(usc-game PRIVATE GIT_COMMIT="")
    target_compile_definitions(usc-game PRIVATE GIT_BRANCH="master")
endif(GIT_FOUND)

set_output_postfixes(usc-game)
set_werror(usc-game)

if(MSVC)
	# Set debugging folder
    set_target_properties(usc-game PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
    target_compile_options(usc-game PRIVATE /EHsc /MP)
	# Target subsystem on windows, enable multiprocess build and faster PDB gen
	set_target_properties(usc-game PROPERTIES LINK_FLAGS_DEBUG "/SUBSYSTEM:WINDOWS /DEBUG:FASTLINK")
	set_target_properties(usc-game PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /DEBUG:FULL")
endif(MSVC)

# Dependencies
target_link_libraries(usc-game Shared)
target_link_libraries(usc-game Graphics)
target_link_libraries(usc-game Audio)
target_link_libraries(usc-game GUI)
target_link_libraries(usc-game Beatmap)
target_link_libraries(usc-game nanovg)
target_link_libraries(usc-game lua)
if(USC_ENABLE_DISCORD_RPC)
    target_link_libraries(usc-game discord-rpc)
endif()
target_link_libraries(usc-game cpr)
target_link_libraries(usc-game nlohmann_json)

if(USC_WINDOW_BACKEND STREQUAL "SDL2")
    target_link_libraries(usc-game ${SDL2_LIBRARY})
endif()
target_link_libraries(usc-game ${LibArchive_LIBRARIES})
target_include_directories(usc-game SYSTEM PRIVATE ${LibArchive_INCLUDE_DIRS})

if(WIN32)
    target_link_libraries(usc-game
        optimized ${PROJECT_SOURCE_DIR}/third_party/breakpad/exception_handler_Release.lib
		optimized ${PROJECT_SOURCE_DIR}/third_party/breakpad/crash_generation_client_Release.lib
		optimized ${PROJECT_SOURCE_DIR}/third_party/breakpad/common_Release.lib
    )
    target_link_libraries(usc-game
        debug ${PROJECT_SOURCE_DIR}/third_party/breakpad/exception_handler_Debug.lib
		debug ${PROJECT_SOURCE_DIR}/third_party/breakpad/crash_generation_client_Debug.lib
		debug ${PROJECT_SOURCE_DIR}/third_party/breakpad/common_Debug.lib
    )
    target_include_directories(usc-game PUBLIC ${PROJECT_SOURCE_DIR}/third_party/breakpad/include)
endif()

find_package(ZLIB REQUIRED)
if(ZLIB_FOUND)
    target_compile_definitions(usc-game PRIVATE ZLIB_FOUND=${ZLIB_FOUND})
endif()

target_link_libraries(usc-game cc-common)

if(APPLE)
    # .app bundle packaging. Resources are copied flat alongside the executable
    # (Contents/MacOS/) rather than into Contents/Resources/ - Path::Absolute()
    # (Shared/src/Unix/Path.cpp) already resolves assets relative to the executable's
    # own directory with zero bundle-awareness needed, matching the existing bin/ layout
    # exactly; this trades Apple HIG purity for not touching well-exercised path
    # resolution code for an unsigned, non-App-Store hobby build. See
    # /home/andrew/.claude/plans/humming-sleeping-stonebraker.md.
    set_target_properties(usc-game PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "USC"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.unnamed-sdvx-clone.usc-game"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_STRING "USC ${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE ""
        MACOSX_BUNDLE_EXECUTABLE_NAME "usc-game"
    )

    foreach(resourceDir skins fonts audio LightPlugins)
        add_custom_command(TARGET usc-game POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PROJECT_SOURCE_DIR}/bin/${resourceDir}"
                "$<TARGET_FILE_DIR:usc-game>/${resourceDir}"
            COMMENT "Copying ${resourceDir} into the .app bundle"
        )
    endforeach()
endif()
