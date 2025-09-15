#pragma once


void table_pushstring(lua_State* L, const char* key, const char* value);
void table_pushint(lua_State* L, const char* key, int value);
void table_pushboolean(lua_State* L, const char* key, bool b);
