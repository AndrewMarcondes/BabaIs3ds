#include "script.h"
#include "render.h"
#include "../lua/lauxlib.h"
#include "../lua/lualib.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run(lua_State *L, const char *path) {
    if (luaL_dofile(L, path) == LUA_OK) return 0;
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
        if (len > 4 && !strcmp(e->d_name + len - 4, ".lua")) names[n++] = strdup(e->d_name);
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

int script_start(lua_State *L) { return engine_call(L, "boot", NULL); }

const char *script_string(lua_State *L, const char *field) {
    lua_getglobal(L, "engine");
    lua_getfield(L, -1, field);
    const char *s = lua_tostring(L, -1); // engine keeps the string alive
    lua_pop(L, 2);
    return s ? s : "";
}

void script_draw(lua_State *L, uint8_t *fb, int fbw, int fbh, int wobble) {
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
        if (render_begin_level(script_string(L, "data"), script_string(L, "world"), script_string(L, "palette"), w, h, fbw, fbh))
            printf("cannot load palette %s\n", script_string(L, "palette"));
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
    if (script_call(L, 0, 1)) return;
    // each entry: { sprite, root, frame, x, y, c1, c2, zlayer }
    int n = lua_rawlen(L, -1);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        int v[5];
        for (int k = 0; k < 5; k++) { lua_rawgeti(L, -1, k + 3); v[k] = lua_tointeger(L, -1); lua_pop(L, 1); }
        lua_rawgeti(L, -1, 2);
        int root = lua_toboolean(L, -1);
        lua_rawgeti(L, -2, 1);
        render_sprite(fb, lua_tostring(L, -1), root, v[0], wobble, v[1], v[2], v[3], v[4]);
        lua_pop(L, 3);
    }
    lua_pop(L, 1);
}

int script_input(lua_State *L, const char *key) { return engine_call(L, "input", key); }

int script_tick(lua_State *L) { return engine_call(L, "tick", NULL); }
