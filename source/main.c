#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include "script.h"
#include "audio.h"

#define DATA_DIR "sdmc:/3ds/baba/Data"
#define SAVE_FILE "sdmc:/3ds/baba/save.ini"

#define SLIDE_FRAMES 5 // a move slides over ~80ms, like the original

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

static char notice[160];    // startup timings and warnings, shown under the status
static char shown[2048];    // what the bottom screen shows now; cleared to force a redraw

// Redraws the bottom screen when the shim's status text (level, rules, controls) or the notice changes.
static void show_status(lua_State *L) {
    char text[2048];
    if (script_status(L, text, sizeof text)) return;
    snprintf(text + strlen(text), sizeof text - strlen(text), "\n\x1b[37;2m%s\x1b[0m", notice);
    if (!strcmp(text, shown)) return;
    snprintf(shown, sizeof shown, "%s", text);
    printf("\x1b[2J\x1b[H%s", text);
}

static const char *menu[] = {"Resume", "Restart level", "Return to map", "Quit game"};
#define MENU_ITEMS 4

static void show_menu(int sel) {
    shown[0] = 0;
    printf("\x1b[2J\x1b[H\x1b[33mPaused\x1b[0m\n\n");
    for (int i = 0; i < MENU_ITEMS; i++) printf("%s %s\x1b[0m\n", i == sel ? "\x1b[33m>" : " ", menu[i]);
    printf("\n\x1b[37;2mUp/Down choose   A select   B/START back\x1b[0m");
}

int main(void) {
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, false);
    consoleInit(GFX_BOTTOM, NULL);
    romfsInit();
    printf("baba3ds: loading...\n");
    int audio = !music_init();

    u64 t0 = osGetTime();
    lua_State *L = script_open(DATA_DIR, "romfs:/shim.lua", SAVE_FILE);
    u64 topen = osGetTime();
    int ok = L && !script_start(L);
    u64 t1 = osGetTime();
    if (!ok) printf("Copy the game's Data folder to\n" DATA_DIR "\nSTART to quit\n");
    else {
        script_draw(L, frame, 400, 240, 1, 1);
        u64 t2 = osGetTime();
        script_draw(L, frame, 400, 240, 1, 1); // second draw: sprites cached, the per-turn cost
        u64 t3 = osGetTime();
        present();
        int n = snprintf(notice, sizeof notice, "boot %llu+%llums, draw %llu/%llums\n", topen - t0, t1 - topen, t2 - t1, t3 - t2);
        FILE *f = fopen(DATA_DIR "/Sounds/pop1.ogg", "rb");
        if (f) fclose(f);
        if (!audio) snprintf(notice + n, sizeof notice - n, "no audio: needs sdmc:/3ds/dspfirm.cdc");
        else if (!f) snprintf(notice + n, sizeof notice - n, "no sound effects: see README");
        show_status(L);
        update_music(L);
    }

    hidSetRepeatParameters(15, 6);
    int wobble = 1, vblanks = 0, paused = 0, sel = 0;
    int slide = SLIDE_FRAMES; // frames into the last move's slide animation
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown(), rep = hidKeysDownRepeat();
        if (!ok) {
            if (down & KEY_START) break;
        } else {
            const char *key = NULL;
            if (paused) {
                if (rep & KEY_UP) sel = (sel + MENU_ITEMS - 1) % MENU_ITEMS;
                if (rep & KEY_DOWN) sel = (sel + 1) % MENU_ITEMS;
                if (rep & (KEY_UP | KEY_DOWN)) show_menu(sel);
                if (down & KEY_A) {
                    if (sel == 3) break;
                    key = sel == 1 ? "restart" : sel == 2 ? "leave" : NULL;
                    paused = 0;
                } else if (down & (KEY_B | KEY_START)) paused = 0;
                if (!paused) show_status(L);
            } else if (down & KEY_START) {
                paused = 1;
                sel = 0;
                show_menu(sel);
            } else {
                // KEY_RIGHT etc. = D-pad or Circle Pad (Azahar maps the keyboard arrows to the Circle Pad)
                key = rep & KEY_RIGHT ? "right" : rep & KEY_UP ? "up" : rep & KEY_LEFT ? "left"
                    : rep & KEY_DOWN ? "down" : rep & KEY_B ? "undo" : down & KEY_A ? "enter"
                    : down & KEY_X ? "restart" : down & KEY_SELECT ? "leave" : NULL;
            }
            int redraw = 0;
            if (key) {
                script_input(L, key);
                show_status(L);
                update_music(L);
                script_sounds(L, play_sound);
                slide = 0;
            }
            if (slide < SLIDE_FRAMES) {
                slide++;
                redraw = 1;
            }
            if (++vblanks >= 12) { // ~5 per second, like the original's sprite wobble
                vblanks = 0;
                wobble = wobble % 3 + 1;
                script_tick(L);
                redraw = 1;
            }
            if (redraw) {
                script_draw(L, frame, 400, 240, wobble, (float)slide / SLIDE_FRAMES);
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
