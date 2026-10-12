#include "script.h"
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

lua_State *script_open(const char *data, const char *shim_path) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    // constants/values first: other files use their globals at load time
    const char *first[] = {"constants.lua", "values.lua"};
    char editor[512];
    snprintf(editor, sizeof editor, "%s/Editor", data);
    if (run(L, shim_path) || run_dir(L, data, first, 2) || run_dir(L, editor, NULL, 0)) { lua_close(L); return NULL; }
    return L;
}
