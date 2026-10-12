#pragma once
#include <stdint.h>
#include "../lua/lua.h"
#include "level.h"

// Opens Lua, runs our shim, then the game's scripts from data/*.lua and data/Editor/*.lua.
// savefile = where progress is stored. NULL on failure (message printed).
lua_State *script_open(const char *data, const char *shim_path, const char *savefile);

// Calls the function below nargs args on the stack, with traceback on error. 0 on success (nresults left on stack).
int script_call(lua_State *L, int nargs, int nresults);

// Starts the game: the level the save says comes first (level 1, or the map once it's won).
int script_start(lua_State *L);

// One turn of input: right/up/left/down/idle/undo/restart, or "enter"/"leave" for the map.
int script_input(lua_State *L, const char *key);

// Draws the current level state; sets the renderer up again whenever the level changed.
void script_draw(lua_State *L, uint8_t *fb, int fbw, int fbh);

// engine.<field> as a string ("" if unset), e.g. "message".
const char *script_string(lua_State *L, const char *field);
