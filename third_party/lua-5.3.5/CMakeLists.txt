# PNG Library
cmake_minimum_required(VERSION 3.4)

include_directories(src)
add_library(lua
    src/lapi.c
    src/lauxlib.c
    src/lbaselib.c
    src/lbitlib.c
    src/lcode.c
    src/lcorolib.c
    src/lctype.c
    src/ldblib.c
    src/ldebug.c
    src/ldo.c
    src/ldump.c
    src/lfunc.c
    src/lgc.c
    src/linit.c
    src/liolib.c
    src/llex.c
    src/lmathlib.c
    src/lmem.c
    src/loadlib.c
    src/lobject.c
    src/lopcodes.c
    src/loslib.c
    src/lparser.c
    src/lstate.c
    src/lstring.c
    src/lstrlib.c
    src/ltable.c
    src/ltablib.c
    src/ltm.c
    src/lundump.c
    src/lutf8lib.c
    src/lvm.c
    src/lzio.c
)
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(UNIX)
    target_compile_definitions(lua PUBLIC LUA_USE_POSIX)
endif()
if(APPLE)
    # lprefix.h (lua's own, included first in every lua .c file) unconditionally defines
    # _XOPEN_SOURCE 600, which on the old 10.4 SDK's headers ends up hiding gmtime_r/
    # localtime_r entirely (even with _DARWIN_C_SOURCE also defined - empirically
    # verified, not just theorized). lprefix.h documents this exact escape hatch itself:
    # "-D_XOPEN_SOURCE=0 to undefine it", which restores full Darwin POSIX visibility.
    target_compile_definitions(lua PUBLIC _XOPEN_SOURCE=0)
endif()
