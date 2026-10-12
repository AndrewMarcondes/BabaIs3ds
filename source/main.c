#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include "script.h"

#define DATA_DIR "sdmc:/3ds/baba/Data"
#define SAVE_FILE "sdmc:/3ds/baba/save.ini"

#define HELP "baba3ds\nD-pad/Circle move  B undo  X restart\nA wait / enter level  SELECT back to map\nSTART quit\n\n"

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

// Reprints the help plus the shim's message (level name, "Cleared: ...") when the message changes.
static void show_message(lua_State *L) {
    static char last[256] = "\x01";
    const char *msg = script_string(L, "message");
    if (!strcmp(msg, last)) return;
    snprintf(last, sizeof last, "%s", msg);
    printf("\x1b[2J\x1b[H" HELP "%s\n", last);
}

int main(void) {
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, false);
    consoleInit(GFX_BOTTOM, NULL);
    romfsInit();
    printf(HELP);

    lua_State *L = script_open(DATA_DIR, "romfs:/shim.lua", SAVE_FILE);
    int ok = L && !script_start(L);
    if (!ok) printf("Copy the game's Data folder to\n" DATA_DIR "\n");
    else { script_draw(L, frame, 400, 240); present(); show_message(L); }

    hidSetRepeatParameters(15, 6);
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown(), rep = hidKeysDownRepeat();
        if (down & KEY_START) break;
        if (ok) {
            // KEY_RIGHT etc. = D-pad or Circle Pad (Azahar maps the keyboard arrows to the Circle Pad)
            const char *key = rep & KEY_RIGHT ? "right" : rep & KEY_UP ? "up" : rep & KEY_LEFT ? "left"
                            : rep & KEY_DOWN ? "down" : rep & KEY_B ? "undo" : down & KEY_A ? "enter"
                            : down & KEY_X ? "restart" : down & KEY_SELECT ? "leave" : NULL;
            if (key) {
                script_input(L, key);
                script_draw(L, frame, 400, 240);
                present();
                show_message(L);
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
