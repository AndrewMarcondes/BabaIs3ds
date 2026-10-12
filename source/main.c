#include <3ds.h>
#include <stdio.h>
#include "render.h"

#define DATA_DIR "sdmc:/3ds/baba/Data"

static uint8_t frame[400 * 240 * 3];

int main(void) {
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, false);
    consoleInit(GFX_BOTTOM, NULL);
    printf("baba3ds\nloading level from " DATA_DIR "\n");

    int r = render_level(DATA_DIR, "Worlds/baba/0level", frame, 400, 240);
    if (r) printf("render_level failed: %d\nCopy the game's Data folder to sdmc:/3ds/baba/Data\n", r);
    else {
        // Top framebuffer is 240x400 column-major, bottom-to-top, BGR8.
        u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        for (int y = 0; y < 240; y++)
            for (int x = 0; x < 400; x++) {
                const u8 *s = frame + (y * 400 + x) * 3;
                u8 *d = fb + (x * 240 + (239 - y)) * 3;
                d[0] = s[2]; d[1] = s[1]; d[2] = s[0];
            }
        printf("ok\n");
    }
    printf("START to exit\n");

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    gfxExit();
    return 0;
}
