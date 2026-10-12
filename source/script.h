#pragma once
#include "../lua/lua.h"
#include "level.h"

// Opens Lua, runs our shim, then the game's scripts from data/*.lua. NULL on failure (message printed).
lua_State *script_open(const char *data, const char *shim_path);

// Hands a parsed level to engine.loadlevel in the shim. 0 on success.
int script_loadlevel(lua_State *L, const Level *lv);

// Calls the function below nargs args on the stack, with traceback on error. 0 on success (nresults left on stack).
int script_call(lua_State *L, int nargs, int nresults);

// Draws the current level state (engine.drawlist) with the renderer.
void script_draw(lua_State *L, uint8_t *fb);

// Loads Worlds/<world>/<level>.l into the scripts and sets up the renderer for it. 0 on success.
int script_startlevel(lua_State *L, const char *data, const char *world, const char *level, int fbw, int fbh);

// One turn of input; key is from the game's keys table: right/up/left/down/idle/undo/restart.
int script_input(lua_State *L, const char *key);
