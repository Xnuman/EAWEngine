/*
 * EAWEngine's build configuration of Lua 5.0.3, force-included into every Lua source file (see
 * CMakeLists.txt). The script sandbox's limits that live inside Lua itself:
 *
 * - Memory: lmem.c allocates through l_realloc; EAWEngine's version (eaw_lua_memory.c) refuses to
 *   grow a Lua state past EAW_LUA_MEMORY_LIMIT bytes, using the state's own count of allocated
 *   bytes, so a runaway script gets Lua's "not enough memory" error instead of exhausting the
 *   process. luaM_realloc names its state `L`, which the macro passes on.
 * - Pattern matching: a patched copy of lstrlib.c (eaw_lua_patch_lstrlib.cmake) stops a
 *   string.find, gfind iteration, or gsub after EAW_LUA_MAX_MATCH_STEPS calls of its matcher, which
 *   backtracks exponentially on patterns such as "a?a?a?...aaa".
 *
 * Both limits are engine choices, far above what stock scripts need.
 */
#ifndef EAW_LUA_CONFIG_H
#define EAW_LUA_CONFIG_H

#include <stddef.h>

struct lua_State;

#define EAW_LUA_MEMORY_LIMIT (64UL * 1024UL * 1024UL)
#define EAW_LUA_MAX_MATCH_STEPS 5000000UL

void *eaw_lua_realloc(struct lua_State *L, void *block, size_t old_size, size_t size);
#define l_realloc(b, os, s) eaw_lua_realloc(L, (b), (os), (s))

#endif
