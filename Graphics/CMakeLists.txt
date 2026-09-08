# Graphics library

set(INCROOT ${CMAKE_CURRENT_SOURCE_DIR}/include/Graphics/)
set(SRCROOT ${CMAKE_CURRENT_SOURCE_DIR}/src/)
set(PCHROOT ${CMAKE_CURRENT_SOURCE_DIR}/)

file(GLOB INC "${INCROOT}/*.hpp")
source_group("Public Headers" FILES ${INC})

file(GLOB SRC "${SRCROOT}/*.cpp" "${SRCROOT}/*.hpp")
# Backend-specific sources (e.g. Mesh_GL3.cpp, Mesh_GLES3.cpp, WindowImpl_SDL2.cpp,
# WindowImpl_CARBON.cpp) are excluded from the generic glob; only the files matching
# USC_RENDER_BACKEND and USC_WINDOW_BACKEND are added back in below.
list(FILTER SRC EXCLUDE REGEX ".*_(GL3|GLES3|GL1_LEGACY|SDL2|CARBON)\\.(cpp|hpp)$")

file(GLOB RENDER_BACKEND_SRC "${SRCROOT}/*_${USC_RENDER_BACKEND}.cpp" "${SRCROOT}/*_${USC_RENDER_BACKEND}.hpp")
if(NOT RENDER_BACKEND_SRC)
    message(FATAL_ERROR "No Graphics/src/*_${USC_RENDER_BACKEND}.cpp files found for USC_RENDER_BACKEND=${USC_RENDER_BACKEND}")
endif()
list(APPEND SRC ${RENDER_BACKEND_SRC})

file(GLOB WINDOW_BACKEND_SRC "${SRCROOT}/*_${USC_WINDOW_BACKEND}.cpp" "${SRCROOT}/*_${USC_WINDOW_BACKEND}.hpp")
if(NOT WINDOW_BACKEND_SRC)
    message(FATAL_ERROR "No Graphics/src/*_${USC_WINDOW_BACKEND}.cpp files found for USC_WINDOW_BACKEND=${USC_WINDOW_BACKEND}")
endif()
list(APPEND SRC ${WINDOW_BACKEND_SRC})
source_group("Sources" FILES ${SRC})

# Find files used for project
set(GRAPHICS_SRC ${SRC} ${INC})

set(PCH_SRC ${PCHROOT}/stdafx.cpp)
set(PCH_INC ${PCHROOT}/stdafx.h)
set(PCH_FILES ${PCH_SRC} ${PCH_INC})
source_group("" FILES ${PCH_FILES})

# Compiler stuff
enable_precompiled_headers("${GRAPHICS_SRC}" ${PCH_SRC})

add_library(Graphics ${GRAPHICS_SRC} ${PCH_FILES})
target_compile_features(Graphics PUBLIC cxx_std_17)
target_include_directories(Graphics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(Graphics PRIVATE
    ${INCROOT}
    ${SRCROOT}
    ${PCHROOT}
)

# Backend-specific GL.hpp (see include/backends/<backend>/Graphics/GL.hpp) - selects the
# right platform GL headers per USC_RENDER_BACKEND without any #ifdef in shared code.
string(TOLOWER "${USC_RENDER_BACKEND}" USC_RENDER_BACKEND_LOWER)
target_include_directories(Graphics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/backends/${USC_RENDER_BACKEND_LOWER})

# Dependencies
## Local Dependencies

target_link_libraries(Graphics Shared)

## 3rd Party dependencies
if(USC_WINDOW_BACKEND STREQUAL "SDL2")
    target_include_directories(Graphics PUBLIC ${SDL2_INCLUDE_DIR})
    target_link_libraries(Graphics ${SDL2_LIBRARY})
elseif(USC_WINDOW_BACKEND STREQUAL "CARBON")
    # No real SDL2 on 10.4/PowerPC - vendor just SDL2's public headers for the
    # SDL_Scancode/SDL_Event types Window.hpp's public interface still uses.
    # See include/backends/window/carbon-sdl-headers/README.md. Nothing is linked.
    target_include_directories(Graphics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/backends/window/carbon-sdl-headers)
    target_link_libraries(Graphics ${CARBON_FRAMEWORK} ${AGL_FRAMEWORK} ${APPLICATION_SERVICES_FRAMEWORK} ${IOKIT_FRAMEWORK})
endif()
target_include_directories(Graphics PRIVATE SYSTEM ${FREETYPE_INCLUDE_DIRS})
target_link_libraries(Graphics ${FREETYPE_LIBRARIES})
target_link_libraries(Graphics ${JPEG_LIBRARIES})
target_link_libraries(Graphics ${PNG_LIBRARIES})
if(NOT USC_RENDER_BACKEND STREQUAL "GL1_LEGACY")
    target_link_libraries(Graphics GLEW)
endif()
if(UNIX)
    target_link_libraries(Graphics pthread)
    target_link_libraries(Graphics dl)
    if(APPLE)
        find_library(OPEN_GL OpenGL)
        target_link_libraries(Graphics ${OPEN_GL})
    else(APPLE)
        target_link_libraries(Graphics GL)
        find_package(X11 REQUIRED)
        include_directories(${X11_INCLUDE_DIR})
    endif(APPLE)
endif(UNIX)


set_werror(Graphics)

target_link_libraries(Graphics cc-common)

# Enable multiprocess compiling
if(MSVC)
    target_compile_options(Graphics PRIVATE /MP)
endif(MSVC)