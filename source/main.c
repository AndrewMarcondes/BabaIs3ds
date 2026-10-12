#include <3ds.h>
#include <stdio.h>
#include "script.h"

#define DATA_DIR "sdmc:/3ds/baba/Data"

#define HELP "baba3ds\nD-pad/Circle move  B undo  A wait\nX restart  START quit\n\n"

static uint8_t frame[400 * 240 * 3];

// Top framebuffer is 240x400 column-major, bottom-to-top, BGR8.
static void present(void) {
    u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 400; x++) {
            const u8 *s = frame + (y * 400 + x) * 3;
            u8 *d = fb + (x * 240 + (239 - y)) * 3;
            d[0] = s[2]; d[1] = s[1]; d[2] = s[0];
        }
}

int main(void) {
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, false);
    consoleInit(GFX_BOTTOM, NULL);
    romfsInit();
    printf(HELP);

    lua_State *L = script_open(DATA_DIR, "romfs:/shim.lua");
    int ok = L && !script_startlevel(L, DATA_DIR, "baba", "0level", 400, 240);
    if (!ok) printf("Copy the game's Data folder to\n" DATA_DIR "\n");
    else { script_draw(L, frame); present(); }

    hidSetRepeatParameters(15, 6);
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown(), rep = hidKeysDownRepeat();
        if (down & KEY_START) break;
        if (ok) {
            // KEY_RIGHT etc. = D-pad or Circle Pad (Azahar maps the keyboard arrows to the Circle Pad)
            const char *key = rep & KEY_RIGHT ? "right" : rep & KEY_UP ? "up" : rep & KEY_LEFT ? "left"
                            : rep & KEY_DOWN ? "down" : rep & KEY_B ? "undo" : rep & KEY_A ? "idle"
                            : down & KEY_X ? "restart" : NULL;
            if (key) {
                int was_won = script_won(L);
                script_input(L, key);
                script_draw(L, frame);
                present();
                if (!was_won && script_won(L)) printf("Congratulations!\nX to play again\n");
                else if (was_won && !script_won(L)) printf("\x1b[2J\x1b[H" HELP);
            }
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    if (L) lua_close(L);
    romfsExit();
    gfxExit();
    return 0;
}
