# CMake toolchain file for cross-compiling USC to macOS 10.4 Tiger / PowerPC (the iMac G4
# target - see /home/andrew/.claude/plans/humming-sleeping-stonebraker.md for the full port
# plan this is part of).
#
# Needs a powerpc-apple-darwin8 cross toolchain + 10.4u SDK already installed - this does
# NOT build one. VariantXYZ/gcc-powerpc-apple-darwin8 (https://github.com/VariantXYZ/gcc-powerpc-apple-darwin8)
# provides a Docker-based GCC 14.2 toolchain targeting powerpc-apple-darwin8 with the
# 10.4u SDK, confirmed still working as of a March 2025 writeup by Brian Callahan
# (https://briancallahan.net/blog/20250329.html). Point USC_PPC_TOOLCHAIN_ROOT at wherever
# that ends up installed (its bin/ should contain powerpc-apple-darwin8-{gcc,g++,ar,...}).
#
# Usage:
#   cmake -S . -B build.ppc \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/ppc-apple-darwin8.cmake \
#       -DUSC_PPC_TOOLCHAIN_ROOT=/path/to/toolchain \
#       -DUSC_PPC_SYSROOT=/path/to/toolchain/SDKs/MacOSX10.4u.sdk

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR powerpc)
set(CMAKE_OSX_ARCHITECTURES ppc)
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.4 CACHE STRING "")

set(USC_PPC_TOOLCHAIN_ROOT "/usr/local" CACHE PATH
    "Root of the powerpc-apple-darwin8 cross toolchain (its bin/ holds the powerpc-apple-darwin8-* binaries) - /usr/local matches the ghcr.io/variantxyz/gcc-powerpc-apple-darwin8 image's own layout")
set(USC_PPC_SYSROOT "${USC_PPC_TOOLCHAIN_ROOT}/MacOSX10.4u.sdk" CACHE PATH
    "Path to the MacOSX10.4u SDK bundled with the cross toolchain")

set(CMAKE_C_COMPILER "${USC_PPC_TOOLCHAIN_ROOT}/bin/powerpc-apple-darwin8-gcc")
set(CMAKE_CXX_COMPILER "${USC_PPC_TOOLCHAIN_ROOT}/bin/powerpc-apple-darwin8-g++")
set(CMAKE_AR "${USC_PPC_TOOLCHAIN_ROOT}/bin/powerpc-apple-darwin8-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${USC_PPC_TOOLCHAIN_ROOT}/bin/powerpc-apple-darwin8-ranlib" CACHE FILEPATH "")

set(CMAKE_OSX_SYSROOT "${USC_PPC_SYSROOT}")

# This cross toolchain doesn't trip the 10.4u SDK's default feature-test gate for POSIX
# extensions (gmtime_r/localtime_r etc - lua's loslib.c needs these) the way a native
# Apple toolchain does; _DARWIN_C_SOURCE unconditionally requests the full set regardless
# of that gate. Safe/standard for any Darwin cross-compile, not a hack specific to us.
#
# -mcpu=7400 targets the baseline instruction set every G4 chip shipped in an iMac G4
# supports (7400/7410/7441/7445/7447/7450/7455/7457 are all supersets of it, so this is
# the safe floor rather than the specific 1.25GHz model's own 7450-family chip) while
# -mtune=7450 schedules for that chip's deeper pipeline specifically. -maltivec enables
# codegen for the G4's AltiVec SIMD unit - with -O3 (CMAKE_CXX_FLAGS_RELEASE below)
# GCC's own auto-vectorizer uses it for eligible loops project-wide (audio mixing, pixel
# conversion, buffer copies) with no source changes required. Hand-written vec_ intrinsics
# were deliberately not added on top: correctness there hinges on load/store alignment
# assumptions that can't be verified without real G4 hardware to run against, and silently
# wrong SIMD audio math is a worse outcome than scalar-safe auto-vectorized code. A couple
# of the hottest loops (Audio_Impl::Mix in Audio/src/Audio.cpp) were restructured to be
# easier for the auto-vectorizer to recognize instead.
set(USC_PPC_ALTIVEC_FLAGS "-mcpu=7400 -mtune=7450 -maltivec")
set(CMAKE_C_FLAGS_INIT "-D_DARWIN_C_SOURCE ${USC_PPC_ALTIVEC_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "-D_DARWIN_C_SOURCE ${USC_PPC_ALTIVEC_FLAGS}")

# Freetype/zlib/libpng/libjpeg/libogg/libvorbis/libarchive have no PPC/10.4 build
# anywhere - the toolchain image only provides the compiler and Apple's own SDK, not
# these (they're normally just apt-installed on Linux, never vendored in-tree). Point
# this at wherever they were cross-built to (see the docker-based build log in this
# session's history for the exact recipe used against this same toolchain image).
set(USC_PPC_DEPS_ROOT "/opt/ppc-deps" CACHE PATH
    "Prefix where zlib/libpng/libjpeg-turbo/libogg/libvorbis/freetype/libarchive were cross-built for powerpc-apple-darwin8")

set(CMAKE_FIND_ROOT_PATH "${USC_PPC_SYSROOT}" "${USC_PPC_DEPS_ROOT}")
set(CMAKE_PREFIX_PATH "${USC_PPC_DEPS_ROOT}")

# The 10.4u SDK itself ships an ancient bundled X11 Freetype (2.1.4, ~2004 - missing
# FT_ADVANCES_H/FT_Get_Advance that nanovg's fontstash.h needs) under usr/X11R6/, and
# FindFreetype.cmake finds that ahead of the cross-built one above because it's inside
# CMAKE_FIND_ROOT_PATH's sysroot entry. Pre-seed its own cache variables so it uses ours
# instead of searching.
set(FREETYPE_INCLUDE_DIR_ft2build "${USC_PPC_DEPS_ROOT}/include/freetype2" CACHE PATH "")
set(FREETYPE_INCLUDE_DIR_freetype2 "${USC_PPC_DEPS_ROOT}/include/freetype2" CACHE PATH "")
set(FREETYPE_LIBRARY_RELEASE "${USC_PPC_DEPS_ROOT}/lib/libfreetype.a" CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Same "found the SDK's own bundled copy instead of the cross-built one" problem as
# FREETYPE_* above, for zlib: FindZLIB.cmake was resolving to the 10.4u SDK's own
# usr/lib/libz.dylib - an old enough zlib that it's missing inflateReset2 (added in later
# zlib releases), which our cross-built libpng 1.6.44 requires - this doesn't fail until
# link time (undefined symbol), since the SDK's libz stub doesn't declare it either,
# meaning the real Mac OS X 10.4 system zlib almost certainly doesn't have it. Force the
# cross-built static one instead - also removes a runtime dependency on /usr/lib/libz.1.dylib.
set(ZLIB_INCLUDE_DIR "${USC_PPC_DEPS_ROOT}/include" CACHE PATH "" FORCE)
set(ZLIB_LIBRARY_RELEASE "${USC_PPC_DEPS_ROOT}/lib/libz.a" CACHE FILEPATH "" FORCE)

# libpng's own cross-build produced BOTH a static libpng16.a and a shared png.framework in
# USC_PPC_DEPS_ROOT/lib - CMake's default CMAKE_FIND_FRAMEWORK=FIRST on Apple targets means
# FindPNG.cmake picked the framework, and that framework's install_name is an absolute path
# into the Docker build container (/root/libpng-1.6.44/build/png.framework/...) that will
# not exist on the actual target machine - the app would fail to even launch there
# ("Library not loaded"). Pre-seeding PNG_LIBRARY_RELEASE directly (matching the
# FREETYPE_LIBRARY_RELEASE pattern above) fixes libpng specifically. Not using a blanket
# CMAKE_FIND_FRAMEWORK NEVER here - the CARBON/AGL/ApplicationServices/IOKit find_library()
# calls below genuinely need framework-mode search to find the real
# /System/Library/Frameworks/*.framework bundles on the SDK sysroot.
set(PNG_LIBRARY_RELEASE "${USC_PPC_DEPS_ROOT}/lib/libpng.a" CACHE FILEPATH "" FORCE)

# Same "must not depend on anything only present on the build machine" concern applies to
# the C++ runtime: this cross toolchain's g++ defaults to dynamically linking against its
# own bundled libstdc++.6.dylib/libgcc_s.1.1.dylib at /usr/local/powerpc-apple-darwin8/lib/
# - a path that only exists inside this Docker container, not on any real Mac. Statically
# linking both bakes them into the executable instead, so the only runtime dependencies left
# are the target OS's own base libraries (/usr/lib/*, /System/Library/Frameworks/*).
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")

# This target has no working default: GL1_LEGACY (no GLSL/core-profile GPU in the
# supported hardware range) paired with CARBON (no SDL2 pre-10.6) is the only real
# combination for macOS 10.4/PPC.
set(USC_RENDER_BACKEND "GL1_LEGACY" CACHE STRING "" FORCE)
set(USC_WINDOW_BACKEND "CARBON" CACHE STRING "" FORCE)

# No PPC/10.4 build of these exists - see CMakeLists.txt for what turning this off
# actually disables (Discord requires a Discord client that doesn't exist on this OS
# anyway). third_party/cpr (networking) is left enabled here but is unverified on this
# toolchain - see humming-sleeping-stonebraker.md's "explicitly out of scope" section.
set(USC_ENABLE_DISCORD_RPC OFF CACHE BOOL "" FORCE)
