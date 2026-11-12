/* EAWEngine's l_realloc for Lua 5.0.3 (see eaw_lua_config.h): growth past the state's memory limit
 * fails, which Lua reports as "not enough memory" (LUA_ERRMEM). Shrinking and freeing always work,
 * so the collector can make room. */
#include "lstate.h"

#include <stdlib.h>

void *eaw_lua_realloc(struct lua_State *L, void *block, size_t old_size, size_t size)
{
    if (L != NULL && G(L) != NULL && size > old_size && G(L)->nblocks - old_size + size > EAW_LUA_MEMORY_LIMIT)
    {
        return NULL;
    }
    return realloc(block, size);
}
