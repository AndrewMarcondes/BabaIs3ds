// Host check: loads the game's Lua through our shim, calls init(), lists MF_* the scripts hit that the shim lacks.
#include <stdio.h>
#include "../lua/lauxlib.h"
#include "../source/script.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <Data dir>\n", argv[0]); return 2; }
    lua_State *L = script_open(argv[1], "romfs/shim.lua");
    if (!L) return 1;
    printf("scripts loaded\n");
    int r = luaL_dostring(L,
        "init(1, 35, 20, 24, 0, 0, 2, 3, 4, 5, 6, 7, 8, 400, 240)\n"
        "print('init ok')\n"
        "for k, n in pairs(engine.missing) do print('missing', k, n) end");
    if (r) { printf("lua: %s\n", lua_tostring(L, -1)); return 1; }
    lua_close(L);
}
