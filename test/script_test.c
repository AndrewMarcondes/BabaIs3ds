// Host check: loads the game's Lua through our shim, starts level 1, walks Baba right, checks she moved.
#include <stdio.h>
#include "../lua/lauxlib.h"
#include "../source/script.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <Data dir>\n", argv[0]); return 2; }
    lua_State *L = script_open(argv[1], "romfs/shim.lua");
    if (!L) return 1;
    if (script_startlevel(L, argv[1], "baba", "0level", 400, 240)) return 1;
    int r = luaL_dostring(L,
        "local function baba() for _, u in ipairs(units) do if u.strings[UNITNAME] == 'baba' then return u end end end\n"
        "local b = baba(); local x0 = b.values[XPOS]\n"
        "print('units', #units, 'baba at', x0, b.values[YPOS])\n"
        "local rules = {} for _, f in ipairs(features) do rules[#rules+1] = table.concat(f[1], ' ') end\n"
        "print('rules: ' .. table.concat(rules, ', '))\n"
        "for i = 1, 3 do local ok, e = xpcall(engine.input, debug.traceback, 'right') if not ok then error(e, 0) end end\n"
        "print('after 3x right baba at', b.values[XPOS], b.values[YPOS])\n"
        "assert(b.values[XPOS] == x0 + 3, 'baba did not move')\n"
        "engine.input('undo')\n"
        "assert(b.values[XPOS] == x0 + 2, 'undo did not step back, at ' .. b.values[XPOS])\n"
        "local function rockat(x, y) for _, u in ipairs(units) do if u.strings[UNITNAME] == 'rock' and u.values[XPOS] == x and u.values[YPOS] == y then return u end end end\n"
        "local r = rockat(x0 + 4, 11); assert(r, 'no rock in front of baba')\n"
        "engine.input('right'); engine.input('right')\n"
        "assert(b.values[XPOS] == x0 + 4 and r.values[XPOS] == x0 + 5, 'rock not pushed')\n"
        "print('undo ok, rock pushed to', r.values[XPOS])\n"
        "engine.input('restart'); b = baba()\n"
        "assert(b.values[XPOS] == x0, 'restart did not reset baba')\n"
        "print('restart ok')\n"
        "for k, n in pairs(engine.missing) do io.write(k, '=', n, ' ') end print()\n");
    if (r) { printf("lua: %s\n", lua_tostring(L, -1)); return 1; }
    printf("PASS\n");
    lua_close(L);
}
