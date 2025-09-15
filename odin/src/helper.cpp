#if defined(DM_PLATFORM_OSX) || defined(DM_PLATFORM_WINDOWS) || defined(DM_PLATFORM_LINUX) || defined(DM_PLATFORM_ANDROID) || defined(DM_PLATFORM_IOS)

#include <dmsdk/sdk.h>


void table_pushstring(lua_State* L, const char* key, const char* value)
{
    lua_pushstring(L, key);
    lua_pushstring(L, value);
    lua_rawset(L, -3);
}

void table_pushint(lua_State* L, const char* key, int value)
{
    lua_pushstring(L, key);
    lua_pushinteger(L, value);
    lua_rawset(L, -3);
}

void table_pushboolean(lua_State* L, const char* key, bool b)
{
    lua_pushstring(L, key);
    lua_pushboolean(L, b);
    lua_rawset(L, -3);
}


#endif