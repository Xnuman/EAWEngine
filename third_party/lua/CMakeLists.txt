# Lua 5.0.3 (MIT, lua.org), the language version Empire at War's scripts are written for. Pinned by
# the hash of the official release archive. Built as C (its errors use setjmp/longjmp, which never
# cross C++ frames this way), with only the core and the libraries the script host's sandbox opens:
# base, string, table, and math. No io, os, debug, or dynamic library loading is compiled in.
include(FetchContent)
FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.0.3.tar.gz
    URL_HASH SHA256=1193a61b0e08acaa6eee0eecf29709179ee49c71baebc59b682a25c3b5a45671
)
FetchContent_MakeAvailable(lua)

# The script sandbox's limits inside Lua (eaw_lua_config.h): a memory cap through lmem.c's
# l_realloc, and a step limit on pattern matching in a patched copy of lstrlib.c, compiled instead
# of the original.
include(eaw_lua_patch_lstrlib.cmake)
set(EAW_LUA_LSTRLIB "${CMAKE_CURRENT_BINARY_DIR}/lstrlib.c")
eaw_lua_patch_lstrlib("${lua_SOURCE_DIR}/src/lib/lstrlib.c" "${EAW_LUA_LSTRLIB}")

set(EAW_LUA_SOURCES
    ${lua_SOURCE_DIR}/include/lauxlib.h
    ${lua_SOURCE_DIR}/include/lua.h
    ${lua_SOURCE_DIR}/include/lualib.h
    ${lua_SOURCE_DIR}/src/lapi.c
    ${lua_SOURCE_DIR}/src/lcode.c
    ${lua_SOURCE_DIR}/src/ldebug.c
    ${lua_SOURCE_DIR}/src/ldo.c
    ${lua_SOURCE_DIR}/src/ldump.c
    ${lua_SOURCE_DIR}/src/lfunc.c
    ${lua_SOURCE_DIR}/src/lgc.c
    ${lua_SOURCE_DIR}/src/llex.c
    ${lua_SOURCE_DIR}/src/lmem.c
    ${lua_SOURCE_DIR}/src/lobject.c
    ${lua_SOURCE_DIR}/src/lopcodes.c
    ${lua_SOURCE_DIR}/src/lparser.c
    ${lua_SOURCE_DIR}/src/lstate.c
    ${lua_SOURCE_DIR}/src/lstring.c
    ${lua_SOURCE_DIR}/src/ltable.c
    ${lua_SOURCE_DIR}/src/ltm.c
    ${lua_SOURCE_DIR}/src/lundump.c
    ${lua_SOURCE_DIR}/src/lvm.c
    ${lua_SOURCE_DIR}/src/lzio.c
    ${lua_SOURCE_DIR}/src/lib/lauxlib.c
    ${lua_SOURCE_DIR}/src/lib/lbaselib.c
    ${lua_SOURCE_DIR}/src/lib/lmathlib.c
    ${lua_SOURCE_DIR}/src/lib/ltablib.c
)
set(EAW_LUA_CONFIGURATION
    ${CMAKE_CURRENT_SOURCE_DIR}/eaw_lua_config.h
    ${CMAKE_CURRENT_SOURCE_DIR}/eaw_lua_memory.c
)
source_group(TREE ${lua_SOURCE_DIR} FILES ${EAW_LUA_SOURCES})
source_group("eawengine" FILES ${EAW_LUA_CONFIGURATION} ${EAW_LUA_LSTRLIB})

add_library(eaw_lua STATIC ${EAW_LUA_SOURCES} ${EAW_LUA_LSTRLIB} ${EAW_LUA_CONFIGURATION})
set_target_properties(eaw_lua PROPERTIES FOLDER "third_party" LINKER_LANGUAGE C)
target_include_directories(eaw_lua PUBLIC ${lua_SOURCE_DIR}/include PRIVATE ${lua_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(eaw_lua PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/eaw_lua_config.h")
else()
    target_compile_options(eaw_lua PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/eaw_lua_config.h")
endif()
if(MSVC)
    # Third-party C of its time: its own warning level, and the CRT's "unsafe function" notes off.
    # Reviewed 64-bit warnings: C4311 truncates pointers only to hash them into table slots
    # (ltable.c hashpointer), and C4334 widens `1 << lsizenode` in lgc.c's memory estimate, which
    # stays far below 2^31; neither changes behavior.
    target_compile_options(eaw_lua PRIVATE /W3 /wd4244 /wd4267 /wd4996 /wd4311 /wd4334)
    target_compile_definitions(eaw_lua PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
