#include <3ds.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdarg.h>
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

static uint8_t frame[400 * 240 * 3]; // CPU-composed pictures: title, level card, a new level's backdrop

//---------------------------------------------------------------------------- timing readout

// Pause menu > Timing: a readout under the status, and one line per turn in TIMING_FILE.
#define TIMING_FILE "sdmc:/3ds/baba/timing.txt"

static struct {
    int on;
    FILE *log;
    u64 start;                         // start of the current one-second window (us)
    int frames, ticks;                 // top-screen frames drawn and animation ticks in this window
    u64 list, draw, wait, tick;        // summed us in this window: draw list from Lua, GPU commands, waiting, ticks
    u64 turn, turn_list, turn_draw;    // the last turn: Lua, its draw list, its frame
    int logged;                        // the last turn's line has been written
    int mhz;                           // CPU speed the last turn ran at (fast mode can change between turns)
    char key[8], line[160];            // the last turn's key; the readout
} T;

static int fast = 1, n3ds; // New 3DS 804MHz mode (pause menu > Fast mode)

static u64 now_us(void) { return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000000); }

static void timing_set(int on) {
    T.on = on;
    T.line[0] = 0;
    if (on && !T.log && (T.log = fopen(TIMING_FILE, "a"))) {
        setvbuf(T.log, NULL, _IOFBF, 16384); // one SD write now and then, not one per line
        fprintf(T.log, "# baba3ds v%s on a%s 3DS; per turn: level key cpu_mhz lua_ms list_ms draw_ms\n", VERSION,
                n3ds ? " New" : "n Old");
    }
    if (!on && T.log) {
        fclose(T.log);
        T.log = NULL;
    }
    T.start = now_us();
    T.frames = T.ticks = 0;
    T.list = T.draw = T.wait = T.tick = 0;
    T.logged = 1; // nothing to log until the next turn
}

//---------------------------------------------------------------------------- top screen (GPU)

// A full-screen picture from frame (title, level card).
static void show_picture(void) {
    render_gpu_begin();
    render_gpu_canvas(frame);
    render_gpu_end();
}

// The level as last fetched by script_refresh; fade = brightness 0..256. Returns the us spent building the frame.
static u64 draw_level(int wobble, float progress, int fade) {
    u64 t0 = now_us();
    render_gpu_begin(); // waits for the previous frame
    u64 t1 = now_us();
    if (fade < 256) render_gpu_fade(fade);
    script_render(NULL, wobble, progress);
    render_gpu_end();
    u64 t2 = now_us();
    T.frames++;
    T.wait += t1 - t0;
    T.draw += t2 - t1;
    return t2 - t1;
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
    if (T.on) snprintf(text + strlen(text), sizeof text - strlen(text), "\n\x1b[36m%s\x1b[0m", T.line);
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
    show_picture();
}

// Returns the level pack to play, or NULL to quit.
static const char *title_screen(void) {
    static const char *items[] = {"Start game", "Level packs & mods", "Exit"};
    int sel = 0, packs = 0, psel = 0;
    show_title_art();
    play_music("baba", "menu");
    show_list("baba3ds v" VERSION, items, 3, sel, "Up/Down choose   A select");
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown(), rep = hidKeysDownRepeat();
        if (packs) {
            if (list_input(rep, &psel, npacks)) show_list("Level packs & mods", pack_items, npacks, psel, "A play   B back");
            if (down & KEY_A) return pack_dir[psel];
            if (down & KEY_B) {
                packs = 0;
                show_list("baba3ds v" VERSION, items, 3, sel, "Up/Down choose   A select");
            }
        } else {
            if (list_input(rep, &sel, 3)) show_list("baba3ds v" VERSION, items, 3, sel, "Up/Down choose   A select");
            if (down & KEY_A) {
                if (sel == 0) return "baba";
                if (sel == 2) return NULL;
                find_packs();
                if (npacks) {
                    packs = 1;
                    psel = 0;
                    show_list("Level packs & mods", pack_items, npacks, psel, "A play   B back");
                } else show_list("baba3ds v" VERSION, items, 3, sel, "No level packs in " DATA_DIR "/Worlds\nUp/Down choose   A select");
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
    u64 end = osGetTime() + CARD_MS;
    if (*name) {
        render_card(frame, name, wobble);
        show_picture();
    }
    u64 t0 = now_us();
    int n = script_preload(); // behind the card, which stays up at least CARD_MS
    if (T.log) fprintf(T.log, "# %s: loaded %d sprite frames in %.0fms\n", script_string(L, "shown"), n, (now_us() - t0) / 1000.0f);
    if (*name) {
        for (; osGetTime() < end && aptMainLoop();) {
            wait_frame();
            hidScanInput();
            if (hidKeysDown()) break;
        }
    }
    for (u64 start = osGetTime(), t; (t = osGetTime() - start) < FADE_MS && aptMainLoop();) {
        draw_level(wobble, 1, t * 256 / FADE_MS);
        wait_frame();
    }
    draw_level(wobble, 1, 256);
}

enum { P_RESUME, P_RESTART, P_LEAVE, P_FAST, P_TIMING, P_BENCH, P_TITLE, P_QUIT, PAUSE_ITEMS };

static void show_pause(int sel) {
    static char fast_item[32], timing_item[48];
    snprintf(fast_item, sizeof fast_item, "Fast mode: %s", !n3ds ? "n/a (Old 3DS)" : fast ? "on (804MHz)" : "off (268MHz)");
    snprintf(timing_item, sizeof timing_item, "Timing: %s", T.on ? "on (logs /3ds/baba/timing.txt)" : "off");
    const char *items[PAUSE_ITEMS] = {"Resume", "Restart level", "Return to map", fast_item, timing_item,
                                      "Benchmark (1-2 min, restarts level)", "Title screen", "Quit game"};
    show_list("Paused", items, PAUSE_ITEMS, sel, "Up/Down choose   A select   B/START back");
}

enum { QUIT, TITLE };

// Writes the last turn's line, once.
static void log_turn(lua_State *L) {
    if (!T.log || T.logged) return;
    fprintf(T.log, "%s %s %d %.1f %.1f %.1f\n", script_string(L, "shown"), T.key, T.mhz, T.turn / 1000.0f,
            T.turn_list / 1000.0f, T.turn_draw / 1000.0f);
    T.logged = 1;
}

// Ends the readout's one-second window: averages per drawn frame, the last turn, and the CPU mode.
static void timing_window(lua_State *L, u64 now) {
    u64 span = now - T.start;
    if (span < 1000000) return;
    int f = T.frames ? T.frames : 1;
    snprintf(T.line, sizeof T.line,
             "last turn %.1fms Lua, list %.1f, draw %.1f\n"
             "%d fps: list %.1fms draw %.1f wait %.1f\n"
             "ticks %.1fms each, fast mode %s",
             T.turn / 1000.0f, T.turn_list / 1000.0f, T.turn_draw / 1000.0f,
             (int)(T.frames * 1000000ull / span), T.list / 1000.0f / f, T.draw / 1000.0f / f, T.wait / 1000.0f / f,
             T.ticks ? T.tick / 1000.0f / T.ticks : 0.0f, !n3ds ? "n/a" : fast ? "on" : "off");
    T.start = now;
    T.frames = T.ticks = 0;
    T.list = T.draw = T.wait = T.tick = 0;
    show_status(L);
    static int windows;
    if (T.log && ++windows % 10 == 0) fflush(T.log); // so closing the app from the HOME menu loses at most 10s
}

//---------------------------------------------------------------------------- garbage collection

// Lua's garbage collector is stopped and run here instead, between moves: its steps inside a turn made some moves much
// slower (benchmark, wall is you at 268MHz: worst move 533ms with them, 308ms without). Each loop pass without input
// gets up to GC_BUDGET_US of steps while there's garbage. Before a move, a full collection if the heap has grown by
// GC_SLACK_KB since the last finished cycle (holding a direction leaves few passes without input); the live heap is
// only ~3MB, so that's quick.
#define GC_BUDGET_US 3000
#define GC_SLACK_KB (12 * 1024)
static int gc_live; // heap KB after the last finished cycle

static void gc_full(lua_State *L) {
    lua_gc(L, LUA_GCCOLLECT, 0);
    gc_live = lua_gc(L, LUA_GCCOUNT, 0);
}

static void gc_init(lua_State *L) {
    lua_gc(L, LUA_GCSTOP, 0); // lua_gc STEP and COLLECT still work; Lua also collects if an allocation fails
    gc_full(L);
}

static void gc_idle(lua_State *L) {
    if (lua_gc(L, LUA_GCCOUNT, 0) < gc_live + 256) return; // little garbage yet
    u64 end = now_us() + GC_BUDGET_US;
    while (now_us() < end)
        if (lua_gc(L, LUA_GCSTEP, 0)) { // a cycle finished
            gc_live = lua_gc(L, LUA_GCCOUNT, 0);
            break;
        }
}

static void gc_before_move(lua_State *L) {
    if (lua_gc(L, LUA_GCCOUNT, 0) > gc_live + GC_SLACK_KB) gc_full(L);
}

//---------------------------------------------------------------------------- benchmark

// Pause menu > Benchmark: plays each scenario of engine.benchmarks (shim) BENCH_REPS times with the garbage collector
// as in play (BENCH_IDLE passes without input after each move, see gc_idle), then BENCH_REPS_GC times without it (to
// see its share), undoing the moves after every round. Per-move times go to TIMING_FILE and a summary to the bottom
// screen. B stops it after the current round.
#define BENCH_REPS 6
#define BENCH_REPS_GC 3
#define BENCH_IDLE 3
#define BENCH_MOVES 16

static int cmp_float(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static float median(float *v, int n) {
    qsort(v, n, sizeof *v, cmp_float);
    return n ? (n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2) : 0;
}

static float mean(const float *v, int n) {
    float s = 0;
    for (int i = 0; i < n; i++) s += v[i];
    return n ? s / n : 0;
}

// engine.<fn>(i): name and moves of benchmark scenario i (from 1); returns the number of moves, 0 after the last.
static int bench_setup(lua_State *L, int i, char *name, int len, char moves[][12]) {
    lua_getglobal(L, "engine");
    lua_getfield(L, -1, "benchsetup");
    lua_remove(L, -2);
    lua_pushinteger(L, i);
    if (script_call(L, 1, 2)) return 0;
    int n = 0;
    if (lua_isstring(L, -2) && lua_istable(L, -1)) {
        snprintf(name, len, "%s", lua_tostring(L, -2));
        for (; n < BENCH_MOVES; n++) {
            lua_rawgeti(L, -1, n + 1);
            int ok = lua_isstring(L, -1);
            if (ok) snprintf(moves[n], 12, "%s", lua_tostring(L, -1));
            lua_pop(L, 1);
            if (!ok) break;
        }
    }
    lua_pop(L, 2);
    return n;
}

static void engine_do(lua_State *L, const char *fn) {
    lua_getglobal(L, "engine");
    lua_getfield(L, -1, fn);
    lua_remove(L, -2);
    script_call(L, 0, 0);
}

//---------------------------------------------------------------------------- benchmark upload

// If UPLOAD_FILE holds a URL (tools/bench_receiver.py on your own network), the benchmark report is also posted there
// when Wi-Fi is connected; it's always written to TIMING_FILE. A plain HTTP/1.0 POST over a socket, every step with a
// timeout, so a missing receiver or a bad network can't hang the game. Returns a line for the summary, or NULL if not
// set up.
#define UPLOAD_FILE "sdmc:/3ds/baba/upload.txt"
#define UPLOAD_TIMEOUT_MS 3000
#define SOC_SIZE 0x40000

// Connects a non-blocking socket within UPLOAD_TIMEOUT_MS; 0 if connected. A connect in progress may report
// EINPROGRESS, EALREADY or EAGAIN (Azahar), and poll's flags vary, so once poll says something happened, connect is
// asked again: 0 or EISCONN means connected.
static int connect_timeout(int fd, const struct sockaddr_in *addr) {
    if (connect(fd, (const struct sockaddr *)addr, sizeof *addr) == 0) return 0;
    if (errno != EINPROGRESS && errno != EALREADY && errno != EAGAIN) return -1;
    struct pollfd pfd = {fd, POLLOUT, 0};
    if (poll(&pfd, 1, UPLOAD_TIMEOUT_MS) != 1) return -1;
    if (connect(fd, (const struct sockaddr *)addr, sizeof *addr) == 0 || errno == EISCONN) return 0;
    return -1;
}

// Waits until fd is ready for events; 0 if it is, -1 on timeout or error.
static int wait_fd(int fd, short events) {
    struct pollfd pfd = {fd, events, 0};
    return poll(&pfd, 1, UPLOAD_TIMEOUT_MS) == 1 && (pfd.revents & events) ? 0 : -1;
}

// POSTs body to http://host[:port]/path; returns the HTTP status, or -1 if there was no answer.
static int http_post(const char *url, const char *body, int n) {
    char host[128], path[128] = "/";
    int port = 80;
    const char *h = url + 7, *slash = strchr(h, '/');
    int hl = slash ? slash - h : (int)strlen(h);
    if (hl <= 0 || hl >= (int)sizeof host) return -1;
    memcpy(host, h, hl);
    host[hl] = 0;
    if (slash) snprintf(path, sizeof path, "%s", slash);
    char *colon = strchr(host, ':');
    if (colon) {
        *colon = 0;
        port = atoi(colon + 1);
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (!inet_aton(host, &addr.sin_addr)) {
        struct hostent *he = gethostbyname(host);
        if (!he || !he->h_addr_list[0]) return -1;
        memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof addr.sin_addr);
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0), status = -1;
    if (fd < 0) return -1;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    if (!connect_timeout(fd, &addr)) {
        char head[384];
        int hn = snprintf(head, sizeof head,
                          "POST %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: baba3ds/" VERSION "\r\nContent-Type: text/plain\r\n"
                          "Content-Length: %d\r\nConnection: close\r\n\r\n", path, host, n);
        const char *parts[2] = {head, body};
        int lens[2] = {hn, n}, ok = 1;
        for (int p = 0; p < 2 && ok; p++)
            for (int sent = 0; sent < lens[p] && ok;) {
                int r = send(fd, parts[p] + sent, lens[p] - sent, 0);
                if (r > 0) sent += r;
                else ok = r < 0 && errno == EAGAIN && !wait_fd(fd, POLLOUT);
            }
        char reply[64] = "";
        int got = 0;
        while (ok && got < (int)sizeof reply - 1 && !strchr(reply, '\n')) { // just the status line
            reply[got] = 0;
            if (wait_fd(fd, POLLIN)) break;
            int r = recv(fd, reply + got, sizeof reply - 1 - got, 0);
            if (r <= 0) break;
            got += r;
            reply[got] = 0;
        }
        if (got > 9 && !strncmp(reply, "HTTP/1.", 7)) status = atoi(reply + 9);
    }
    close(fd);
    return status;
}

static const char *upload_report(const char *text, char *status, int len) {
    char url[256];
    FILE *f = fopen(UPLOAD_FILE, "r");
    if (!f) return NULL;
    if (!fgets(url, sizeof url, f)) url[0] = 0;
    fclose(f);
    url[strcspn(url, " \r\n")] = 0;
    if (strncmp(url, "http://", 7)) {
        snprintf(status, len, "Not uploaded: upload.txt needs http://");
        return status;
    }
    u32 wifi = 0;
    if (R_SUCCEEDED(acInit())) {
        ACU_GetWifiStatus(&wifi);
        acExit();
    }
    if (!wifi) {
        snprintf(status, len, "Not uploaded: Wi-Fi is off");
        return status;
    }
    u32 *soc = memalign(0x1000, SOC_SIZE);
    if (!soc || R_FAILED(socInit(soc, SOC_SIZE))) {
        free(soc);
        snprintf(status, len, "Not uploaded: no network service");
        return status;
    }
    int code = http_post(url, text, strlen(text));
    socExit();
    free(soc);
    int hl = strcspn(url + 7, "/"); // host[:port], for the message
    if (hl > 30) hl = 30;
    if (code < 0) snprintf(status, len, "Upload failed: no answer from %.*s", hl, url + 7);
    else if (code != 200) snprintf(status, len, "Upload failed: HTTP %d from %.*s", code, hl, url + 7);
    else snprintf(status, len, "Uploaded to %.*s", hl, url + 7);
    return status;
}

static char report[4096]; // the benchmark's lines, for TIMING_FILE and the upload
static int nreport;

static void report_add(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(report + nreport, sizeof report - nreport, fmt, ap);
    va_end(ap);
    if (n > 0) nreport += n < (int)sizeof report - nreport ? n : (int)sizeof report - 1 - nreport;
}

static void benchmark(lua_State *L) {
    int mhz = n3ds && fast ? 804 : 268, stop = 0;
    nreport = 0;
    report[0] = 0;
    report_add("# benchmark v%s on a%s 3DS at %dMHz: per move lua_ms (mean median max), list_ms, draw_ms\n", VERSION,
               n3ds ? " New" : "n Old", mhz);
    char summary[1024];
    int sn = snprintf(summary, sizeof summary, "\x1b[33mBenchmark at %dMHz\x1b[0m (ms of Lua per move,\nmedian: GC between moves / none)\n\n", mhz);
    engine_do(L, "benchbegin");
    char name[48], moves[BENCH_MOVES][12];
    int n;
    for (int s = 1; !stop && (n = bench_setup(L, s, name, sizeof name, moves)) > 0; s++) {
        float med[2] = {0, 0};
        script_refresh(L, frame, 400, 240); // sets up the level's backdrop, not part of any move
        draw_level(1, 1, 256);
        for (int gc = 0; gc < 2 && !stop; gc++) {
            int reps = gc ? BENCH_REPS_GC : BENCH_REPS, k = 0;
            float lua[BENCH_REPS * BENCH_MOVES], list[BENCH_REPS * BENCH_MOVES], draw[BENCH_REPS * BENCH_MOVES];
            float kb = 0;
            if (gc) gc_full(L);
            for (int r = 0; r < reps && !stop; r++) {
                printf("\x1b[2J\x1b[HBenchmark: %s\nGC %s, round %d of %d\n\nB stops after this round", name,
                       gc ? "none" : "between moves", r + 1, reps);
                for (int m = 0; m < n && aptMainLoop(); m++, k++) {
                    int kb0 = lua_gc(L, LUA_GCCOUNT, 0);
                    u64 t0 = now_us();
                    if (!gc) gc_before_move(L);
                    script_input(L, moves[m]);
                    u64 t1 = now_us();
                    script_refresh(L, frame, 400, 240);
                    u64 t2 = now_us();
                    kb += lua_gc(L, LUA_GCCOUNT, 0) - kb0;
                    lua[k] = (t1 - t0) / 1000.0f;
                    list[k] = (t2 - t1) / 1000.0f;
                    draw[k] = draw_level(1, 1, 256) / 1000.0f;
                    for (int i = 0; !gc && i < BENCH_IDLE; i++) gc_idle(L);
                }
                for (int m = 0; m < n; m++) script_input(L, "undo");
                script_refresh(L, frame, 400, 240);
                for (int i = 0; !gc && i < BENCH_IDLE; i++) gc_idle(L);
                hidScanInput();
                if ((hidKeysHeld() & KEY_B) || !aptMainLoop()) stop = 1;
            }
            if (gc) gc_full(L);
            if (!k) break;
            float lmean = mean(lua, k), lmax = 0;
            for (int i = 0; i < k; i++) if (lua[i] > lmax) lmax = lua[i];
            float lmed = median(lua, k);
            med[gc] = lmed;
            report_add("bench \"%s\" GC %s, %d moves: lua %.1f %.1f %.1f, list %.1f, draw %.1f", name,
                       gc ? "none" : "between moves", k, lmean, lmed, lmax, mean(list, k), mean(draw, k));
            if (gc) report_add(", allocates %.0fKB per move", kb / k);
            report_add("\n");
        }
        sn += snprintf(summary + sn, sizeof summary - sn, "%s\n  %.1f / %.1f\n", name, med[0], med[1]);
    }
    engine_do(L, "benchend");
    FILE *log = T.log ? T.log : fopen(TIMING_FILE, "a");
    if (log) {
        fputs(report, log);
        if (log != T.log) fclose(log);
        else fflush(log);
    }
    printf("\x1b[2J\x1b[H%s\n%sUploading...", summary, stop ? "(stopped early)\n" : "");
    char status[96];
    const char *up = upload_report(report, status, sizeof status);
    shown[0] = 0;
    printf("\x1b[2J\x1b[H%s\n%sWritten to /3ds/baba/timing.txt\n%s%s\nPress A", summary, stop ? "(stopped early)\n" : "",
           up ? up : "", up ? "\n" : "");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & (KEY_A | KEY_B | KEY_START)) break;
        wait_frame();
    }
}

// Plays until the player quits or goes back to the title screen.
static int play(lua_State *L) {
    int wobble = 1, paused = 0, sel = 0, sliding = 0, benched = 0;
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
            if (list_input(rep, &sel, PAUSE_ITEMS)) show_pause(sel);
            if (down & KEY_A) {
                if (sel == P_TITLE) return TITLE;
                if (sel == P_QUIT) return QUIT;
                if (sel == P_FAST || sel == P_TIMING) { // toggles: stay in the menu
                    if (sel == P_FAST && n3ds) {
                        fast = !fast;
                        osSetSpeedupEnable(fast);
                        if (T.log) fprintf(T.log, "# fast mode %s\n", fast ? "on" : "off");
                    }
                    if (sel == P_TIMING) timing_set(!T.on);
                    show_pause(sel);
                } else if (sel == P_BENCH) {
                    benchmark(L);
                    paused = 0;
                    benched = 1; // it left the player at the start of their level: draw it again
                } else {
                    key = sel == P_RESTART ? "restart" : sel == P_LEAVE ? "leave" : NULL;
                    paused = 0;
                }
            } else if (down & (KEY_B | KEY_START)) paused = 0;
            if (!paused) {
                show_status(L);
                last_tick = now; // the win delay and animations were frozen while paused
                T.start = now_us(); // nor does the pause count towards the readout
            }
        } else if (down & KEY_START) {
            paused = 1;
            sel = 0;
            show_pause(sel);
            if (T.log) fflush(T.log);
        } else {
            // KEY_RIGHT etc. = D-pad or Circle Pad (Azahar maps the keyboard arrows to the Circle Pad)
            key = rep & KEY_RIGHT ? "right" : rep & KEY_UP ? "up" : rep & KEY_LEFT ? "left"
                : rep & KEY_DOWN ? "down" : rep & KEY_B ? "undo" : down & KEY_A ? "enter"
                : down & KEY_X ? "restart" : down & KEY_SELECT ? "leave" : NULL;
        }
        // Animation runs on the clock, not on loop passes: a slow frame on hardware must not stall the wobble.
        int redraw = 0, refresh = benched;
        benched = 0;
        if (key) {
            if (T.on) log_turn(L); // the previous turn, if no frame came between (e.g. a level change)
            u64 t = now_us();
            gc_before_move(L);
            script_input(L, key);
            T.turn = now_us() - t;
            T.turn_list = T.turn_draw = 0;
            T.logged = 0;
            T.mhz = n3ds && fast ? 804 : 268;
            snprintf(T.key, sizeof T.key, "%s", key);
            show_status(L);
            update_music(L);
            script_sounds(L, play_sound);
            refresh = sliding = 1;
            slide_start = now = osGetTime(); // the turn itself can take a while on hardware
        }
        if (!paused && now - last_tick >= TICK_MS) { // sprite wobble + animated objects, ~5 per second
            last_tick = now;
            wobble = wobble % 3 + 1;
            u64 t = now_us();
            if (script_tick(L)) refresh = 1; // can change level (leaving after a win)
            T.tick += now_us() - t;
            T.ticks++;
            show_status(L);
            update_music(L);
            script_sounds(L, play_sound);
            redraw = 1; // the wobble moves on even when nothing else changed
        }
        float progress = 1;
        if (sliding) {
            progress = (float)(now - slide_start) / SLIDE_MS;
            if (progress >= 1) progress = 1, sliding = 0;
            redraw = 1;
        }
        if (refresh) {
            u64 t = now_us();
            script_refresh(L, frame, 400, 240);
            u64 d = now_us() - t;
            T.list += d;
            if (key) T.turn_list = d;
            if (strcmp(level, script_string(L, "levelserial"))) {
                snprintf(level, sizeof level, "%s", script_string(L, "levelserial"));
                transition(L, wobble);
                last_tick = osGetTime();
                sliding = redraw = refresh = 0;
            }
        }
        if (refresh || redraw) {
            u64 d = draw_level(wobble, progress, 256);
            if (key) T.turn_draw = d;
        }
        if (T.on) {
            if (!key) log_turn(L); // the turn and its first frame are done
            if (!paused) timing_window(L, now_us());
        }
        if (!key) gc_idle(L);
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
    bool is_new = false;
    APT_CheckNew3DS(&is_new);
    n3ds = is_new;
    osSetSpeedupEnable(fast); // New 3DS: 804MHz + L2 cache instead of 268MHz (homebrew must ask); no-op on old models
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, false); // frames are only drawn when something changes; the last one stays up
    consoleInit(GFX_BOTTOM, NULL);
    if (render_gpu_init()) {
        message("Could not start the GPU.");
        gfxExit();
        return 0;
    }
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
        gc_init(L); // collect only between moves from here on
        u64 t2 = osGetTime();
        script_refresh(L, frame, 400, 240);
        draw_level(1, 1, 256);
        u64 t3 = osGetTime();
        int n = snprintf(notice, sizeof notice, "scripts %llums, start %llums, draw %llums\n", topen - t0, t2 - t1, t3 - t2);
        FILE *f = fopen(DATA_DIR "/Sounds/pop1.ogg", "rb");
        if (f) fclose(f);
        if (!audio) snprintf(notice + n, sizeof notice - n, "no audio: needs sdmc:/3ds/dspfirm.cdc");
        else if (!f) snprintf(notice + n, sizeof notice - n, "no sound effects: see README");
        show_status(L);
        update_music(L);
        int next = play(L);
        timing_set(0); // closes the log; the readout starts again when it's switched on
        lua_close(L);
        if (next == QUIT) break;
    }
    music_exit();
    romfsExit();
    render_gpu_exit();
    gfxExit();
    return 0;
}
