// Host check: boots the game with the given save, plays keys, renders the 3DS top screen to a PPM.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../source/script.h"

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <Data dir> <out.ppm> [save.ini] [keys, =level, ~progress...]\n", argv[0]); return 2; }
    lua_State *L = script_open(argv[1], "romfs/shim.lua", argc > 3 ? argv[3] : "build-host/render_save.ini");
    if (!L || script_start(L)) return 1;
    float progress = 1;
    for (int i = 4; i < argc; i++) {
        if (argv[i][0] == '~') { progress = atof(argv[i] + 1); continue; } // "~0.5": draw halfway through the last move
        if (argv[i][0] == '=') { // "=100level": jump straight to a level
            lua_getglobal(L, "engine");
            lua_getfield(L, -1, "gotolevel");
            lua_remove(L, -2);
            lua_pushstring(L, argv[i] + 1);
            if (script_call(L, 1, 0)) return 1;
        } else if (script_input(L, argv[i])) return 1;
    }
    static uint8_t fb[400 * 240 * 3], first[400 * 240 * 3];
    script_draw(L, fb, 400, 240, 1, progress);
    memcpy(first, fb, sizeof fb);
    script_draw(L, fb, 400, 240, 1, progress); // second draw goes through the cached backdrop
    if (memcmp(first, fb, sizeof fb)) { fprintf(stderr, "redraw differs from first draw\n"); return 1; }
    printf("%s\n", script_string(L, "message"));
    FILE *f = fopen(argv[2], "wb");
    fprintf(f, "P6 400 240 255\n");
    fwrite(fb, 1, sizeof fb, f);
    fclose(f);
    return 0;
}
