// Host check: starts a level through the game's scripts, plays keys, renders the 3DS top screen to a PPM.
#include <stdio.h>
#include "../source/script.h"

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s <Data dir> <world> <level> <out.ppm> [keys...]\n", argv[0]); return 2; }
    lua_State *L = script_open(argv[1], "romfs/shim.lua");
    if (!L || script_startlevel(L, argv[1], argv[2], argv[3], 400, 240)) return 1;
    for (int i = 5; i < argc; i++)
        if (script_input(L, argv[i])) return 1;
    static uint8_t fb[400 * 240 * 3];
    script_draw(L, fb);
    FILE *f = fopen(argv[4], "wb");
    fprintf(f, "P6 400 240 255\n");
    fwrite(fb, 1, sizeof fb, f);
    fclose(f);
    return 0;
}
