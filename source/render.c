#include "render.h"
#include "level.h"
#include "png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int id, tiling;
    char sprite[64];
    int col[2], act[2]; // palette cells; act[0] < 0 when the object has no active colour
} Obj;

typedef struct {
    char path[512];
    uint8_t *px; // ts*ts RGBA, pre-scaled
} Sprite;

static char *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc(n + 1);
    if (s && fread(s, 1, n, f) == (size_t)n) s[n] = 0; else { free(s); s = NULL; }
    fclose(f);
    return s;
}

// shortcut: scans values.lua tileslist as text and ignores per-level overrides in .ld; replace once Lua loads values.lua itself
static int load_objects(const char *values_lua, Obj *objs, int max) {
    char *src = read_text(values_lua);
    if (!src) return 0;
    int n = 0;
    char *p = strstr(src, "tileslist =");
    char *end = p ? strstr(p, "\n}") : NULL;
    while (p && end && n < max && (p = strstr(p + 1, "\n\tobject")) && p < end) {
        char *next = strstr(p + 1, "\n\tobject");
        if (!next || next > end) next = end;
        char save = *next;
        *next = 0;
        Obj *o = &objs[n];
        char *f;
        int tx, ty;
        memset(o, 0, sizeof *o);
        o->act[0] = -1;
        o->tiling = -1;
        if ((f = strstr(p, "sprite = \""))) sscanf(f + 10, "%63[^\"]", o->sprite);
        if ((f = strstr(p, "tiling = "))) sscanf(f + 9, "%d", &o->tiling);
        if ((f = strstr(p, "colour = {"))) sscanf(f + 10, "%d, %d", &o->col[0], &o->col[1]);
        if ((f = strstr(p, "active = {"))) sscanf(f + 10, "%d, %d", &o->act[0], &o->act[1]);
        if ((f = strstr(p, "tile = {")) && sscanf(f + 8, "%d, %d", &tx, &ty) == 2 && o->sprite[0]) {
            o->id = tx + ty * 256;
            n++;
        }
        *next = save;
    }
    free(src);
    return n;
}

// Box-filter a sprite down (or up) to ts*ts so 24px art stays readable at 12px.
static uint8_t *scale(const uint8_t *src, int sw, int sh, int ts) {
    uint8_t *out = calloc(ts * ts, 4);
    for (int dy = 0; dy < ts; dy++)
        for (int dx = 0; dx < ts; dx++) {
            int x0 = dx * sw / ts, x1 = (dx + 1) * sw / ts, y0 = dy * sh / ts, y1 = (dy + 1) * sh / ts;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            unsigned r = 0, g = 0, b = 0, a = 0, cnt = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++, cnt++) {
                    const uint8_t *s = src + (y * sw + x) * 4;
                    r += s[0] * s[3]; g += s[1] * s[3]; b += s[2] * s[3]; a += s[3];
                }
            uint8_t *o = out + (dy * ts + dx) * 4;
            if (a) { o[0] = r / a; o[1] = g / a; o[2] = b / a; }
            o[3] = a / cnt;
        }
    return out;
}

static const uint8_t *get_sprite(Sprite *cache, int *ncache, int max, const char *path, int ts) {
    for (int i = 0; i < *ncache; i++)
        if (!strcmp(cache[i].path, path)) return cache[i].px;
    int w, h;
    uint8_t *raw = png_load(path, &w, &h);
    if (!raw) fprintf(stderr, "missing sprite %s\n", path);
    uint8_t *px = raw ? scale(raw, w, h, ts) : NULL;
    free(raw);
    if (*ncache < max) { snprintf(cache[*ncache].path, sizeof cache[0].path, "%s", path); cache[(*ncache)++].px = px; }
    return px;
}

static void fill(uint8_t *fb, int fbw, int x, int y, int w, int h, const uint8_t *c) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) memcpy(fb + (j * fbw + i) * 3, c, 3);
}

int render_level(const char *data, const char *level, uint8_t *fb, int fbw, int fbh) {
    char path[512];
    static Obj objs[512];
    snprintf(path, sizeof path, "%s/values.lua", data);
    int nobj = load_objects(path, objs, 512);
    if (!nobj) return -1;

    char palname[64] = "default.png";
    snprintf(path, sizeof path, "%s/%s.ld", data, level);
    char *ld = read_text(path), *pl;
    if (ld && (pl = strstr(ld, "\npalette="))) sscanf(pl + 9, "%63[^\r\n]", palname);
    free(ld);

    int pw, ph;
    snprintf(path, sizeof path, "%s/Palettes/%s", data, palname);
    uint8_t *pal = png_load(path, &pw, &ph);
    if (!pal) return -2;
#define PAL(x, y) (pal + (((y) % ph) * pw + ((x) % pw)) * 4)

    Level lv;
    snprintf(path, sizeof path, "%s/%s.l", data, level);
    if (level_load(path, &lv)) { free(pal); return -3; }

    // Outer ring of the room is the invisible edge; only the inside is shown.
    int W = lv.layers[0].w, H = lv.layers[0].h, vw = W - 2, vh = H - 2;
    int ts = fbw / vw < fbh / vh ? fbw / vw : fbh / vh;
    int ox = (fbw - vw * ts) / 2, oy = (fbh - vh * ts) / 2;
    fill(fb, fbw, 0, 0, fbw, fbh, PAL(1, 0));
    fill(fb, fbw, ox, oy, vw * ts, vh * ts, PAL(0, 4));

    static Sprite cache[256];
    int ncache = 0;
    for (int l = 0; l < lv.nlayers; l++) {
        Layer *L = &lv.layers[l];
        for (int y = 1; y < H - 1; y++)
            for (int x = 1; x < W - 1; x++) {
                int id = L->tiles[y * W + x];
                if (id == TILE_EMPTY) continue;
                Obj *o = NULL;
                for (int i = 0; i < nobj && !o; i++) if (objs[i].id == id) o = &objs[i];
                if (!o) continue;

                int frame = 0;
                if (o->tiling == 1) { // walls etc: bit per same-object neighbour (or room edge), right/up/left/down
                    static const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, -1, 0, 1};
                    for (int d = 0; d < 4; d++) {
                        int nx = x + dx[d], ny = y + dy[d];
                        if (nx <= 0 || ny <= 0 || nx >= W - 1 || ny >= H - 1 || L->tiles[ny * W + nx] == id) frame |= 1 << d;
                    }
                } else if ((o->tiling == 0 || o->tiling == 2) && L->dirs) frame = L->dirs[y * W + x] * 8;

                // shortcut: only root sprites (sprite_in_root = true); world-specific sprites live in Worlds/<world>/Sprites
                snprintf(path, sizeof path, "%s/Sprites/%s_%d_1.png", data, o->sprite, frame);
                const uint8_t *spr = get_sprite(cache, &ncache, 256, path, ts);
                if (!spr) continue;
                // shortcut: text always uses its active colour; real colour depends on whether its rule is live
                const uint8_t *c = o->act[0] >= 0 ? PAL(o->act[0], o->act[1]) : PAL(o->col[0], o->col[1]);
                int px0 = ox + (x - 1) * ts, py0 = oy + (y - 1) * ts;
                for (int j = 0; j < ts; j++)
                    for (int i = 0; i < ts; i++) {
                        const uint8_t *s = spr + (j * ts + i) * 4;
                        uint8_t *d = fb + ((py0 + j) * fbw + px0 + i) * 3;
                        for (int k = 0; k < 3; k++) d[k] = (d[k] * (255 - s[3]) + s[k] * c[k] / 255 * s[3]) / 255;
                    }
            }
    }
    for (int i = 0; i < ncache; i++) free(cache[i].px);
    level_free(&lv);
    free(pal);
    return 0;
}
