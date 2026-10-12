// Host check: boots the game with the given save, plays keys, renders the 3DS top screen to a PPM.
#include <stdio.h>
#include "../source/script.h"

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <Data dir> <out.ppm> [save.ini] [keys...]\n", argv[0]); return 2; }
    lua_State *L = script_open(argv[1], "romfs/shim.lua", argc > 3 ? argv[3] : "build-host/render_save.ini");
    if (!L || script_start(L)) return 1;
    for (int i = 4; i < argc; i++)
        if (script_input(L, argv[i])) return 1;
    static uint8_t fb[400 * 240 * 3];
    script_draw(L, fb, 400, 240);
    printf("%s\n", script_string(L, "message"));
    FILE *f = fopen(argv[2], "wb");
    fprintf(f, "P6 400 240 255\n");
    fwrite(fb, 1, sizeof fb, f);
    fclose(f);
    return 0;
}
