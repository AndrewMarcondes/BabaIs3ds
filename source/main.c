#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include "script.h"
#include "audio.h"

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

// Starts the shim's chosen track when it changes: the world's Music folder first, then Data/Music.
static void update_music(lua_State *L) {
    static char last[64];
    const char *name = script_string(L, "music");
    if (!strcmp(name, last)) return;
    snprintf(last, sizeof last, "%s", name);
    char path[256];
    snprintf(path, sizeof path, DATA_DIR "/Worlds/%s/Music/%s.ogg", script_string(L, "world"), name);
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    else snprintf(path, sizeof path, DATA_DIR "/Music/%s.ogg", name);
    music_play(path);
}

static void play_sound(const char *name) {
    char path[160];
    snprintf(path, sizeof path, DATA_DIR "/Sounds/%s.ogg", name);
    sfx_play(path);
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
    int audio = !music_init();

    u64 t0 = osGetTime();
    lua_State *L = script_open(DATA_DIR, "romfs:/shim.lua", SAVE_FILE);
    u64 topen = osGetTime();
    int ok = L && !script_start(L);
    u64 t1 = osGetTime();
    if (!ok) printf("Copy the game's Data folder to\n" DATA_DIR "\n");
    else {
        script_draw(L, frame, 400, 240, 1);
        u64 t2 = osGetTime();
        script_draw(L, frame, 400, 240, 1); // second draw: sprites cached, the per-turn cost
        u64 t3 = osGetTime();
        present();
        show_message(L);
        printf("scripts %llums, start %llums, first draw %llums, draw %llums\n", topen - t0, t1 - topen, t2 - t1, t3 - t2);
        if (!audio) printf("no audio: needs sdmc:/3ds/dspfirm.cdc\n");
        else {
            FILE *f = fopen(DATA_DIR "/Sounds/pop1.ogg", "rb");
            if (f) fclose(f);
            else printf("no sound effects: run tools/extract_sounds.py\n");
        }
        update_music(L);
    }

    hidSetRepeatParameters(15, 6);
    int wobble = 1, vblanks = 0;
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown(), rep = hidKeysDownRepeat();
        if (down & KEY_START) break;
        if (ok) {
            // KEY_RIGHT etc. = D-pad or Circle Pad (Azahar maps the keyboard arrows to the Circle Pad)
            const char *key = rep & KEY_RIGHT ? "right" : rep & KEY_UP ? "up" : rep & KEY_LEFT ? "left"
                            : rep & KEY_DOWN ? "down" : rep & KEY_B ? "undo" : down & KEY_A ? "enter"
                            : down & KEY_X ? "restart" : down & KEY_SELECT ? "leave" : NULL;
            int redraw = 0;
            if (key) {
                script_input(L, key);
                show_message(L);
                update_music(L);
                script_sounds(L, play_sound);
                redraw = 1;
            }
            if (++vblanks >= 12) { // ~5 per second, like the original's sprite wobble
                vblanks = 0;
                wobble = wobble % 3 + 1;
                script_tick(L);
                redraw = 1;
            }
            if (redraw) {
                script_draw(L, frame, 400, 240, wobble);
                present();
            }
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    music_exit();
    if (L) lua_close(L);
    romfsExit();
    gfxExit();
    return 0;
}
