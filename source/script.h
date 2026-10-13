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

// Fetches what to draw from the game (after input, animation ticks or level changes) and keeps it in C;
// sets the renderer up again whenever the level changed. fb is used to compose a new level's backdrop.
void script_refresh(lua_State *L, uint8_t *fb, int fbw, int fbh);
// Draws the kept list: wobble = sprite frame 1..3, progress = 0..1 through the last move's slide. No Lua.
void script_render(uint8_t *fb, int wobble, float progress);
// Both of the above.
void script_draw(lua_State *L, uint8_t *fb, int fbw, int fbh, int wobble, float progress);

// Animation timer step (~5 per second): advances animated objects.
int script_tick(lua_State *L);

// engine.<field> as a string ("" if unset), e.g. "message".
const char *script_string(lua_State *L, const char *field);

// Calls play(name) for each sound the game queued since the last call (names are files in Data/Sounds, no extension).
int script_sounds(lua_State *L, void (*play)(const char *name));

// Bottom-screen text from engine.status() (level name, active rules, controls) into out. 0 on success.
int script_status(lua_State *L, char *out, int len);
