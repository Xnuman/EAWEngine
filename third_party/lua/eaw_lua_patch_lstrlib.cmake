# Writes a copy of Lua 5.0.3's lstrlib.c whose pattern matcher stops after EAW_LUA_MAX_MATCH_STEPS
# calls (eaw_lua_config.h) with "pattern too complex", instead of backtracking for minutes on
# patterns such as "a?a?a?...aaa". The pinned archive stays untouched; each replacement must match
# the expected number of times, so a different Lua source fails the configure instead of building
# unprotected.

# Replaces `from` with `to` in the variable named `text_variable`, which must hold `from` exactly
# `expected` times.
function(eaw_lua_replace text_variable from to expected)
    set(text "${${text_variable}}")
    string(LENGTH "${text}" before)
    string(REPLACE "${from}" "" stripped "${text}")
    string(LENGTH "${stripped}" after)
    string(LENGTH "${from}" piece)
    math(EXPR found "(${before} - ${after}) / ${piece}")
    if(NOT found EQUAL expected)
        message(FATAL_ERROR "Patching Lua's lstrlib.c: expected ${expected} occurrence(s) of \"${from}\", found ${found}. Is the Lua source the pinned 5.0.3?")
    endif()
    string(REPLACE "${from}" "${to}" text "${text}")
    set(${text_variable} "${text}" PARENT_SCOPE)
endfunction()

# eaw_lua_patch_lstrlib(<original lstrlib.c> <output file>)
function(eaw_lua_patch_lstrlib original output)
    file(READ "${original}" text)
    # A step count in the matcher's state...
    eaw_lua_replace(text
        "  int level;  /* total number of captures (finished or unfinished) */\n"
        "  int level;  /* total number of captures (finished or unfinished) */\n  unsigned long steps;  /* EAWEngine: match() calls of this search */\n"
        1)
    # ...counted at each call of the matcher...
    eaw_lua_replace(text
        "static const char *match (MatchState *ms, const char *s, const char *p) {\n  init:"
        "static const char *match (MatchState *ms, const char *s, const char *p) {\n  if (++ms->steps > EAW_LUA_MAX_MATCH_STEPS)\n    luaL_error(ms->L, \"pattern too complex (more than %lu matching steps)\", (unsigned long)EAW_LUA_MAX_MATCH_STEPS);\n  init:"
        1)
    # ...and started at zero by each of str_find, gfind_aux, and str_gsub.
    eaw_lua_replace(text "ms.L = L;\n" "ms.L = L; ms.steps = 0;\n" 3)
    file(WRITE "${output}.new" "${text}")
    # Rewritten only when it changes, so a reconfigure does not rebuild Lua.
    configure_file("${output}.new" "${output}" COPYONLY)
endfunction()
