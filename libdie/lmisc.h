#ifndef LMISC_H
#define LMISC_H

extern "C" {
#include <lua.h>
}

#if LUA_VERSION_NUM < 502
#define luaL_setfuncs(L, m, n) luaL_openlib(L, 0, m, n)
#define luaL_requiref(L, n, f, v) lua_pushcfunction(L, f); lua_pushstring(L, n); lua_call(L, 1, 0); lua_pushnil(L)
#endif

#endif
