#pragma once
#include "../lua/lua.h"

// Opens Lua, runs our shim, then the game's scripts from data/*.lua. NULL on failure (message printed).
lua_State *script_open(const char *data, const char *shim_path);
