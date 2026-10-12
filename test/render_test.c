// Host check: renders a level at 3DS top-screen size to a PPM.
#include <stdio.h>
#include "../source/render.h"

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s <Data dir> <Worlds/baba/0level> <out.ppm>\n", argv[0]); return 2; }
    static uint8_t fb[400 * 240 * 3];
    int r = render_level(argv[1], argv[2], fb, 400, 240);
    if (r) { fprintf(stderr, "render_level failed: %d\n", r); return 1; }
    FILE *f = fopen(argv[3], "wb");
    fprintf(f, "P6 400 240 255\n");
    fwrite(fb, 1, sizeof fb, f);
    fclose(f);
}
