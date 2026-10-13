#include "script.h"
#include "render.h"
#include "../lua/lauxlib.h"
#include "../lua/lualib.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// luaL_loadfile reads in BUFSIZ (1KB) pieces; one read per file is much cheaper on the SD card.
static int loadfile(lua_State *L, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return luaL_loadfile(L, path); // let Lua produce the usual error message
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = n > 0 ? malloc(n) : NULL;
    int ok = buf && fread(buf, 1, n, f) == (size_t)n;
    fclose(f);
    if (!ok) { free(buf); return luaL_loadfile(L, path); }
    char name[300];
    snprintf(name, sizeof name, "@%s", path);
    int r = luaL_loadbuffer(L, buf, n, name);
    free(buf);
    return r;
}

static int run(lua_State *L, const char *path) {
    if (loadfile(L, path) == LUA_OK && lua_pcall(L, 0, LUA_MULTRET, 0) == LUA_OK) return 0;
    printf("lua: %s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
    return -1;
}

static int cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

// Runs every .lua in dir: names in `first` (if present) first, then the rest alphabetically for a stable order.
static int run_dir(lua_State *L, const char *dir, const char **first, int nfirst) {
    char path[512], *names[128];
    int n = 0, err = 0;
    DIR *d = opendir(dir);
    if (!d) { printf("cannot open %s\n", dir); return -1; }
    struct dirent *e;
    while ((e = readdir(d)) && n < 128) {
        size_t len = strlen(e->d_name);
        // skip hidden files: macOS leaves "._name.lua" metadata files on SD cards / FTP uploads
        if (len > 4 && e->d_name[0] != '.' && !strcmp(e->d_name + len - 4, ".lua")) names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, n, sizeof *names, cmp);
    for (int f = 0; f < nfirst; f++) {
        snprintf(path, sizeof path, "%s/%s", dir, first[f]);
        err |= run(L, path);
    }
    for (int i = 0; i < n; i++) {
        int skip = 0;
        for (int f = 0; f < nfirst; f++) skip |= !strcmp(names[i], first[f]);
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        if (!skip) err |= run(L, path);
        free(names[i]);
    }
    return err;
}

// engine_readlevel(path) -> { w, h, layers = { { tiles = {...}, dirs = {...} } } } or nil
static int l_readlevel(lua_State *L) {
    Level lv;
    if (level_load(luaL_checkstring(L, 1), &lv)) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    lua_pushinteger(L, lv.layers[0].w);
    lua_setfield(L, -2, "w");
    lua_pushinteger(L, lv.layers[0].h);
    lua_setfield(L, -2, "h");
    lua_newtable(L);
    for (int l = 0; l < lv.nlayers; l++) {
        const Layer *ly = &lv.layers[l];
        lua_newtable(L);
        lua_createtable(L, ly->w * ly->h, 0);
        for (uint32_t i = 0; i < ly->w * ly->h; i++) { lua_pushinteger(L, ly->tiles[i]); lua_rawseti(L, -2, i + 1); }
        lua_setfield(L, -2, "tiles");
        if (ly->dirs) {
            lua_createtable(L, ly->w * ly->h, 0);
            for (uint32_t i = 0; i < ly->w * ly->h; i++) { lua_pushinteger(L, ly->dirs[i]); lua_rawseti(L, -2, i + 1); }
            lua_setfield(L, -2, "dirs");
        }
        lua_rawseti(L, -2, l + 1);
    }
    lua_setfield(L, -2, "layers");
    level_free(&lv);
    return 1;
}

lua_State *script_open(const char *data, const char *shim_path, const char *savefile) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    lua_register(L, "engine_readlevel", l_readlevel);
    // constants/values first: other files use their globals at load time
    const char *first[] = {"constants.lua", "values.lua"};
    char editor[512];
    snprintf(editor, sizeof editor, "%s/Editor", data);
    if (run(L, shim_path)) { lua_close(L); return NULL; }
    lua_getglobal(L, "engine");
    lua_pushstring(L, data);
    lua_setfield(L, -2, "data");
    lua_pushstring(L, savefile);
    lua_setfield(L, -2, "savefile");
    lua_pop(L, 1);
    if (run_dir(L, data, first, 2) || run_dir(L, editor, NULL, 0)) { lua_close(L); return NULL; }
    return L;
}

// pcall with a traceback in the error message; on failure prints it and leaves nothing on the stack.
int script_call(lua_State *L, int nargs, int nresults) {
    int base = lua_gettop(L) - nargs;
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_remove(L, -2);
    lua_insert(L, base);
    int r = lua_pcall(L, nargs, nresults, base);
    lua_remove(L, base);
    if (r) { printf("lua: %s\n", lua_tostring(L, -1)); lua_pop(L, 1); }
    return r;
}

static int engine_call(lua_State *L, const char *fn, const char *arg) {
    lua_getglobal(L, "engine");
    lua_getfield(L, -1, fn);
    lua_remove(L, -2);
    if (arg) lua_pushstring(L, arg);
    return script_call(L, arg ? 1 : 0, 0);
}

int script_start(lua_State *L) {
    lua_getglobal(L, "engine");
    lua_getfield(L, -1, "boot");
    lua_remove(L, -2);
    if (script_call(L, 0, 1)) return -1;
    int ok = lua_toboolean(L, -1); // boot returns false if the first level can't be loaded
    lua_pop(L, 1);
    return ok ? 0 : -1;
}

const char *script_string(lua_State *L, const char *field) {
    lua_getglobal(L, "engine");
    lua_getfield(L, -1, field);
    const char *s = lua_tostring(L, -1); // engine keeps the string alive
    lua_pop(L, 2);
    return s ? s : "";
}

// The draw list, kept in C so the frames of a slide can be drawn without calling into Lua.
typedef struct {
    char sprite[48];
    uint8_t root, part, c1, c2;
    int16_t frame, x, y, fx, fy; // x/y = where it is now, fx/fy = where the last move started (1/16 tiles)
} Item;
static Item items[4096];
static int nitems;

void script_refresh(lua_State *L, uint8_t *fb, int fbw, int fbh) {
    static int serial = -1;
    lua_getglobal(L, "engine");
    lua_getfield(L, -1, "levelserial");
    int now = lua_tointeger(L, -1);
    lua_pop(L, 2);
    if (now != serial) {
        serial = now;
        lua_getglobal(L, "engine");
        lua_getfield(L, -1, "roomw");
        lua_getfield(L, -2, "roomh");
        int w = lua_tointeger(L, -2), h = lua_tointeger(L, -1);
        lua_pop(L, 3);
        if (render_begin_level(script_string(L, "data"), script_string(L, "world"), script_string(L, "palette"), w, h, fbw, fbh)) {
            printf("cannot load palette %s, using default\n", script_string(L, "palette"));
            if (render_begin_level(script_string(L, "data"), script_string(L, "world"), "default.png", w, h, fbw, fbh)) {
                serial = -1; // try again next time; nothing can be drawn without a palette
                nitems = 0;
                return;
            }
        }
    }
    if (!render_backdrop_cached(fb)) {
        render_clear(fb);
        lua_getglobal(L, "engine");
        lua_getfield(L, -1, "images");
        for (int i = 1; lua_istable(L, -1) && i <= (int)lua_rawlen(L, -1); i++) {
            lua_rawgeti(L, -1, i);
            render_image(fb, lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 2);
        render_backdrop_save(fb);
    }
    lua_getglobal(L, "engine");
    lua_getfield(L, -1, "drawlist");
    lua_remove(L, -2);
    nitems = 0;
    if (script_call(L, 0, 1)) return;
    // each entry: { sprite, root, frame, x, y, c1, c2, zlayer, part, fromx, fromy }
    int n = lua_rawlen(L, -1);
    for (int i = 1; i <= n && nitems < (int)(sizeof items / sizeof *items); i++) {
        lua_rawgeti(L, -1, i);
        int v[11];
        for (int k = 2; k < 11; k++) { lua_rawgeti(L, -1, k + 1); v[k] = lua_tointeger(L, -1); lua_pop(L, 1); }
        Item *it = &items[nitems++];
        lua_rawgeti(L, -1, 2);
        it->root = lua_toboolean(L, -1);
        lua_rawgeti(L, -2, 1);
        snprintf(it->sprite, sizeof it->sprite, "%s", lua_tostring(L, -1));
        lua_pop(L, 3);
        it->frame = v[2];
        it->x = v[3];
        it->y = v[4];
        it->c1 = v[5];
        it->c2 = v[6];
        it->part = v[8];
        it->fx = v[9];
        it->fy = v[10];
    }
    lua_pop(L, 1);
}

void script_render(uint8_t *fb, int wobble, float progress) {
    if (!render_backdrop_cached(fb)) return;
    for (int i = 0; i < nitems; i++) {
        const Item *it = &items[i];
        int x = it->fx + (int)((it->x - it->fx) * progress), y = it->fy + (int)((it->y - it->fy) * progress);
        render_sprite(fb, it->sprite, it->root, it->frame, wobble, it->part, x, y, it->c1, it->c2);
    }
}

void script_draw(lua_State *L, uint8_t *fb, int fbw, int fbh, int wobble, float progress) {
    script_refresh(L, fb, fbw, fbh);
    script_render(fb, wobble, progress);
}

int script_input(lua_State *L, const char *key) { return engine_call(L, "input", key); }

int script_tick(lua_State *L) { return engine_call(L, "tick", NULL); }

int script_sounds(lua_State *L, void (*play)(const char *name)) {
    lua_getglobal(L, "engine");
    lua_getfield(L, -1, "takesounds");
    lua_remove(L, -2);
    if (script_call(L, 0, 1)) return 0;
    int n = lua_rawlen(L, -1);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        if (lua_isstring(L, -1)) play(lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return n;
}

int script_status(lua_State *L, char *out, int len) {
    lua_getglobal(L, "engine");
    lua_getfield(L, -1, "status");
    lua_remove(L, -2);
    out[0] = 0;
    if (script_call(L, 0, 1)) return -1;
    snprintf(out, len, "%s", lua_isstring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
    return 0;
}
