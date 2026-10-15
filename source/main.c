#include <3ds.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "png.h"
#include "render.h"
#include "script.h"
#include "audio.h"

#define DATA_DIR "sdmc:/3ds/baba/Data"
#define SAVE_FILE "sdmc:/3ds/baba/save.ini"

#define SLIDE_MS 80 // a move slides over ~80ms, like the original
#define TICK_MS 200 // animation tick: sprite wobble and animated objects
#define CARD_MS 1000 // level start card with the level's name
#define FADE_MS 300  // fade in after a level change

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

static void wait_frame(void) {
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

static char playing[64]; // current music track name

static void play_music(const char *world, const char *name) {
    if (!strcmp(name, playing)) return;
    snprintf(playing, sizeof playing, "%s", name);
    char path[sizeof DATA_DIR + 160];
    snprintf(path, sizeof path, DATA_DIR "/Worlds/%s/Music/%s.ogg", world, name); // the pack's own music first
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    else snprintf(path, sizeof path, DATA_DIR "/Music/%s.ogg", name);
    music_play(path);
}

// Starts the shim's chosen track when it changes.
static void update_music(lua_State *L) { play_music(script_string(L, "world"), script_string(L, "music")); }

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

// A bottom-screen menu: title, items with the selection marked, and a help line.
static void show_list(const char *title, const char **items, int n, int sel, const char *help) {
    shown[0] = 0;
    printf("\x1b[2J\x1b[H\x1b[33m%s\x1b[0m\n\n", title);
    int first = sel > 20 ? sel - 20 : 0; // about 21 rows fit; scroll long lists
    for (int i = first; i < n && i < first + 21; i++) printf("%s %s\x1b[0m\n", i == sel ? "\x1b[33m>" : " ", items[i]);
    printf("\n\x1b[37;2m%s\x1b[0m", help);
}

// Up/Down move sel through n items; returns 1 if it changed.
static int list_input(u32 rep, int *sel, int n) {
    int old = *sel;
    if (rep & KEY_UP) *sel = (*sel + n - 1) % n;
    if (rep & KEY_DOWN) *sel = (*sel + 1) % n;
    return *sel != old;
}

//---------------------------------------------------------------------------- level packs

#define MAX_PACKS 64
static char pack_dir[MAX_PACKS][64], pack_name[MAX_PACKS][48];
static const char *pack_items[MAX_PACKS];
static int npacks;

// Level packs = folders in Data/Worlds with a world_data.txt (the main game first). Mods are packs with Lua.
static void find_packs(void) {
    npacks = 0;
    DIR *d = opendir(DATA_DIR "/Worlds");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && npacks < MAX_PACKS) {
        if (e->d_name[0] == '.' || !strcmp(e->d_name, "debug")) continue; // debug = the developer's test pack
        if (strlen(e->d_name) >= sizeof pack_dir[0]) continue;                // a cut-off name would point elsewhere
        char path[sizeof DATA_DIR + 96], line[128];
        snprintf(path, sizeof path, DATA_DIR "/Worlds/%s/world_data.txt", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        snprintf(pack_name[npacks], sizeof pack_name[0], "%.47s", e->d_name);
        while (fgets(line, sizeof line, f))
            if (!strncmp(line, "name=", 5)) {
                line[strcspn(line, "\r\n")] = 0;
                if (line[5]) snprintf(pack_name[npacks], sizeof pack_name[0], "%.47s", line + 5); // display only
                break;
            }
        fclose(f);
        snprintf(pack_dir[npacks], sizeof pack_dir[0], "%s", e->d_name);
        npacks++;
    }
    closedir(d);
    for (int i = 1; i < npacks; i++) // main game first, then by name
        for (int j = i; j > 0; j--) {
            int a_main = !strcmp(pack_dir[j - 1], "baba"), b_main = !strcmp(pack_dir[j], "baba");
            if (a_main || (!b_main && strcasecmp(pack_name[j - 1], pack_name[j]) <= 0)) break;
            char t[64];
            memcpy(t, pack_dir[j], sizeof t); memcpy(pack_dir[j], pack_dir[j - 1], sizeof t); memcpy(pack_dir[j - 1], t, sizeof t);
            memcpy(t, pack_name[j], sizeof pack_name[0]); memcpy(pack_name[j], pack_name[j - 1], sizeof pack_name[0]);
            memcpy(pack_name[j - 1], t, sizeof pack_name[0]);
        }
    for (int i = 0; i < npacks; i++) pack_items[i] = pack_name[i];
}

//---------------------------------------------------------------------------- title screen

static void show_title_art(void) {
    int w, h;
    uint8_t *img = png_load("romfs:/title.png", &w, &h);
    memset(frame, 0, sizeof frame);
    if (img && w == 400 && h == 240)
        for (int i = 0; i < 400 * 240; i++) memcpy(frame + i * 3, img + i * 4, 3);
    free(img);
    present();
}

// Returns the level pack to play, or NULL to quit.
static const char *title_screen(void) {
    static const char *items[] = {"Start game", "Level packs & mods", "Exit"};
    int sel = 0, packs = 0, psel = 0;
    show_title_art();
    play_music("baba", "menu");
    show_list("baba3ds", items, 3, sel, "Up/Down choose   A select");
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown(), rep = hidKeysDownRepeat();
        if (packs) {
            if (list_input(rep, &psel, npacks)) show_list("Level packs & mods", pack_items, npacks, psel, "A play   B back");
            if (down & KEY_A) return pack_dir[psel];
            if (down & KEY_B) {
                packs = 0;
                show_list("baba3ds", items, 3, sel, "Up/Down choose   A select");
            }
        } else {
            if (list_input(rep, &sel, 3)) show_list("baba3ds", items, 3, sel, "Up/Down choose   A select");
            if (down & KEY_A) {
                if (sel == 0) return "baba";
                if (sel == 2) return NULL;
                find_packs();
                if (npacks) {
                    packs = 1;
                    psel = 0;
                    show_list("Level packs & mods", pack_items, npacks, psel, "A play   B back");
                } else show_list("baba3ds", items, 3, sel, "No level packs in " DATA_DIR "/Worlds\nUp/Down choose   A select");
            }
        }
        wait_frame();
    }
    return NULL;
}

//---------------------------------------------------------------------------- game

//---------------------------------------------------------------------------- level transitions

// Card (when entering a level) then a fade into the new level; any button skips the card.
static void transition(lua_State *L, int wobble) {
    const char *name = script_string(L, "cardname");
    if (*name) {
        render_card(frame, name, wobble);
        present();
        for (u64 end = osGetTime() + CARD_MS; osGetTime() < end && aptMainLoop();) {
            wait_frame();
            hidScanInput();
            if (hidKeysDown()) break;
        }
    }
    for (u64 start = osGetTime(), t; (t = osGetTime() - start) < FADE_MS && aptMainLoop();) {
        script_render(frame, wobble, 1);
        int k = t * 256 / FADE_MS;
        for (size_t i = 0; i < sizeof frame; i++) frame[i] = frame[i] * k >> 8;
        present();
        wait_frame();
    }
    script_render(frame, wobble, 1);
    present();
}

static const char *pause_items[] = {"Resume", "Restart level", "Return to map", "Title screen", "Quit game"};
#define PAUSE_ITEMS 5

enum { QUIT, TITLE };

// Plays until the player quits or goes back to the title screen.
static int play(lua_State *L) {
    int wobble = 1, paused = 0, sel = 0, sliding = 0;
    char level[16];
    snprintf(level, sizeof level, "%s", script_string(L, "levelserial")); // changes whenever a new level loads
    transition(L, wobble);
    u64 last_tick = osGetTime(), slide_start = 0;
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown(), rep = hidKeysDownRepeat();
        u64 now = osGetTime();
        const char *key = NULL;
        if (paused) {
            if (list_input(rep, &sel, PAUSE_ITEMS)) show_list("Paused", pause_items, PAUSE_ITEMS, sel, "Up/Down choose   A select   B/START back");
            if (down & KEY_A) {
                if (sel == 3) return TITLE;
                if (sel == 4) return QUIT;
                key = sel == 1 ? "restart" : sel == 2 ? "leave" : NULL;
                paused = 0;
            } else if (down & (KEY_B | KEY_START)) paused = 0;
            if (!paused) {
                show_status(L);
                last_tick = now; // the win delay and animations were frozen while paused
            }
        } else if (down & KEY_START) {
            paused = 1;
            sel = 0;
            show_list("Paused", pause_items, PAUSE_ITEMS, sel, "Up/Down choose   A select   B/START back");
        } else {
            // KEY_RIGHT etc. = D-pad or Circle Pad (Azahar maps the keyboard arrows to the Circle Pad)
            key = rep & KEY_RIGHT ? "right" : rep & KEY_UP ? "up" : rep & KEY_LEFT ? "left"
                : rep & KEY_DOWN ? "down" : rep & KEY_B ? "undo" : down & KEY_A ? "enter"
                : down & KEY_X ? "restart" : down & KEY_SELECT ? "leave" : NULL;
        }
        // Animation runs on the clock, not on loop passes: a slow frame on hardware must not stall the wobble.
        int redraw = 0, refresh = 0;
        if (key) {
            script_input(L, key);
            show_status(L);
            update_music(L);
            script_sounds(L, play_sound);
            refresh = sliding = 1;
            slide_start = now = osGetTime(); // the turn itself can take a while on hardware
        }
        if (!paused && now - last_tick >= TICK_MS) { // sprite wobble + animated objects, ~5 per second
            last_tick = now;
            wobble = wobble % 3 + 1;
            script_tick(L); // can change level (leaving after a win)
            show_status(L);
            update_music(L);
            script_sounds(L, play_sound);
            refresh = 1;
        }
        float progress = 1;
        if (sliding) {
            progress = (float)(now - slide_start) / SLIDE_MS;
            if (progress >= 1) progress = 1, sliding = 0;
            redraw = 1;
        }
        if (refresh) {
            script_refresh(L, frame, 400, 240);
            if (strcmp(level, script_string(L, "levelserial"))) {
                snprintf(level, sizeof level, "%s", script_string(L, "levelserial"));
                transition(L, wobble);
                last_tick = osGetTime();
                sliding = redraw = refresh = 0;
            }
        }
        if (refresh || redraw) {
            script_render(frame, wobble, progress);
            present();
        }
        wait_frame();
    }
    return QUIT;
}

// Shows a message on the bottom screen until a button is pressed.
static void message(const char *text) {
    shown[0] = 0;
    printf("\x1b[2J\x1b[H%s\n\nPress A", text);
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & (KEY_A | KEY_B | KEY_START)) break;
        wait_frame();
    }
}

int main(void) {
    osSetSpeedupEnable(true); // New 3DS: 804MHz + L2 cache instead of 268MHz (homebrew must ask); no-op on old models
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, false);
    consoleInit(GFX_BOTTOM, NULL);
    romfsInit();
    int audio = !music_init();
    hidSetRepeatParameters(15, 6);

    for (;;) {
        // Lua is loaded fresh for each pack: mods replace the game's own functions.
        show_title_art();
        printf("\x1b[2J\x1b[Hbaba3ds: loading...\n");
        u64 t0 = osGetTime();
        lua_State *L = script_open(DATA_DIR, "romfs:/shim.lua", SAVE_FILE);
        u64 topen = osGetTime();
        if (!L) {
            message("Could not load the game.\nCopy the game's data to\n" DATA_DIR "\n(see README: tools/prepare_sd.py)");
            break;
        }
        const char *world = title_screen();
        if (!world) { lua_close(L); break; }
        printf("\x1b[2J\x1b[Hloading %s...\n", world);
        u64 t1 = osGetTime();
        if (script_start(L, world)) {
            char text[256];
            snprintf(text, sizeof text, "Could not start this level pack:\n%s", script_string(L, "message"));
            lua_close(L);
            message(text);
            continue;
        }
        u64 t2 = osGetTime();
        script_draw(L, frame, 400, 240, 1, 1);
        present();
        u64 t3 = osGetTime();
        int n = snprintf(notice, sizeof notice, "scripts %llums, start %llums, draw %llums\n", topen - t0, t2 - t1, t3 - t2);
        FILE *f = fopen(DATA_DIR "/Sounds/pop1.ogg", "rb");
        if (f) fclose(f);
        if (!audio) snprintf(notice + n, sizeof notice - n, "no audio: needs sdmc:/3ds/dspfirm.cdc");
        else if (!f) snprintf(notice + n, sizeof notice - n, "no sound effects: see README");
        show_status(L);
        update_music(L);
        int next = play(L);
        lua_close(L);
        if (next == QUIT) break;
    }
    music_exit();
    romfsExit();
    gfxExit();
    return 0;
}
