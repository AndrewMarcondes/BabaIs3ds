// Host check: boots the game's Lua through our shim with a fresh save, then runs a Lua test (default test/play_test.lua).
#include <stdio.h>
#include "../lua/lauxlib.h"
#include "../source/script.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <Data dir> [test.lua]\n", argv[0]); return 2; }
    remove("build-host/test_save.ini");
    remove("build-host/test_save.ini.a");
    remove("build-host/test_save.ini.b");
    lua_State *L = script_open(argv[1], "romfs/shim.lua", "build-host/test_save.ini");
    if (!L || script_start(L)) return 1;
    const char *test = argc > 2 ? argv[2] : "test/play_test.lua";
    if (luaL_loadfile(L, test) || script_call(L, 0, 0)) {
        if (lua_isstring(L, -1)) printf("%s\n", lua_tostring(L, -1));
        return 1;
    }
    printf("PASS\n");
    lua_close(L);
    return 0;
}
