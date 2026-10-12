#include "render.h"
#include "png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_SIZE 2048 // power of two; open addressing, cleared when 3/4 full

typedef struct {
    char *path;  // NULL = empty slot
    uint8_t *px; // scaled RGBA; NULL if the file is missing
} Sprite;

static struct {
    char data[256], world[64];
    uint8_t *pal;
    int pw, ph, fbw, fbh, ts, ox, oy, vw, vh;
    Sprite cache[CACHE_SIZE];
    int ncache;
} R;

static unsigned hash(const char *s) {
    unsigned h = 2166136261u;
    while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

static void cache_clear(void) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        free(R.cache[i].path);
        free(R.cache[i].px);
        R.cache[i].path = NULL;
        R.cache[i].px = NULL;
    }
    R.ncache = 0;
}

#define PAL(x, y) (R.pal + (((y) % R.ph) * R.pw + ((x) % R.pw)) * 4)

// Box-filter an image down (or up) to dw*dh so 24px art stays readable at 12px.
static uint8_t *scale(const uint8_t *src, int sw, int sh, int dw, int dh) {
    uint8_t *out = calloc(dw * dh, 4);
    if (!out) return NULL;
    for (int dy = 0; dy < dh; dy++)
        for (int dx = 0; dx < dw; dx++) {
            int x0 = dx * sw / dw, x1 = (dx + 1) * sw / dw, y0 = dy * sh / dh, y1 = (dy + 1) * sh / dh;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            unsigned r = 0, g = 0, b = 0, a = 0, cnt = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++, cnt++) {
                    const uint8_t *s = src + (y * sw + x) * 4;
                    r += s[0] * s[3]; g += s[1] * s[3]; b += s[2] * s[3]; a += s[3];
                }
            uint8_t *o = out + (dy * dw + dx) * 4;
            if (a) { o[0] = r / a; o[1] = g / a; o[2] = b / a; }
            o[3] = a / cnt;
        }
    return out;
}

// Loads and scales an image to w*h, cached by path. Missing files are cached as NULL so each is reported once.
// The returned pixels are only valid until the next call (a full cache is cleared).
static const uint8_t *get_image(const char *path, int dw, int dh) {
    unsigned i = hash(path) & (CACHE_SIZE - 1);
    for (; R.cache[i].path; i = (i + 1) & (CACHE_SIZE - 1))
        if (!strcmp(R.cache[i].path, path)) return R.cache[i].px;
    if (R.ncache >= CACHE_SIZE * 3 / 4) {
        cache_clear();
        i = hash(path) & (CACHE_SIZE - 1);
    }
    int w, h;
    uint8_t *raw = png_load(path, &w, &h);
    if (!raw) printf("missing sprite %s\n", path);
    uint8_t *px = raw ? scale(raw, w, h, dw, dh) : NULL;
    free(raw);
    R.cache[i].path = strdup(path);
    R.cache[i].px = px;
    R.ncache++;
    return px;
}

static void fill(uint8_t *fb, int x, int y, int w, int h, const uint8_t *c) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) memcpy(fb + (j * R.fbw + i) * 3, c, 3);
}

void render_end_level(void) {
    cache_clear();
    free(R.pal);
    R.pal = NULL;
}

int render_begin_level(const char *data, const char *world, const char *palette, int roomw, int roomh, int fbw, int fbh) {
    render_end_level();
    char path[512];
    snprintf(R.data, sizeof R.data, "%s", data);
    snprintf(R.world, sizeof R.world, "%s", world);
    snprintf(path, sizeof path, "%s/Palettes/%s", data, palette);
    if (!(R.pal = png_load(path, &R.pw, &R.ph))) return -1;
    // Outer ring of the room is the invisible edge; only the inside is shown.
    R.fbw = fbw;
    R.fbh = fbh;
    R.vw = roomw - 2;
    R.vh = roomh - 2;
    R.ts = fbw / R.vw < fbh / R.vh ? fbw / R.vw : fbh / R.vh;
    R.ox = (fbw - R.vw * R.ts) / 2;
    R.oy = (fbh - R.vh * R.ts) / 2;
    return 0;
}

void render_clear(uint8_t *fb) {
    fill(fb, 0, 0, R.fbw, R.fbh, PAL(1, 0));
    fill(fb, R.ox, R.oy, R.vw * R.ts, R.vh * R.ts, PAL(0, 4));
}

void render_sprite(uint8_t *fb, const char *sprite, int root, int frame, int wobble, int x, int y, int c1, int c2) {
    if (x < 1 || y < 1 || x > R.vw || y > R.vh) return;
    if (sprite[0] == '#') { // "#box": solid tile with a 1px gap, for level icons the engine draws itself
        fill(fb, R.ox + (x - 1) * R.ts + 1, R.oy + (y - 1) * R.ts + 1, R.ts - 2, R.ts - 2, PAL(c1, c2));
        return;
    }
    char path[512];
    const char *dir = root ? "" : "Worlds/", *world = root ? "" : R.world, *slash = root ? "" : "/";
    if (sprite[0] == '@') // "@name": exact file name, e.g. map icons "icon_lake_1"
        snprintf(path, sizeof path, "%s/%s%s%sSprites/%s.png", R.data, dir, world, slash, sprite + 1);
    else
        snprintf(path, sizeof path, "%s/%s%s%sSprites/%s_%d_%d.png", R.data, dir, world, slash, sprite, frame, wobble);
    const uint8_t *spr = get_image(path, R.ts, R.ts);
    if (!spr) return;
    const uint8_t *c = PAL(c1, c2);
    int px0 = R.ox + (x - 1) * R.ts, py0 = R.oy + (y - 1) * R.ts;
    for (int j = 0; j < R.ts; j++)
        for (int i = 0; i < R.ts; i++) {
            const uint8_t *s = spr + (j * R.ts + i) * 4;
            uint8_t *d = fb + ((py0 + j) * R.fbw + px0 + i) * 3;
            for (int k = 0; k < 3; k++) d[k] = (d[k] * (255 - s[3]) + s[k] * c[k] / 255 * s[3]) / 255;
        }
}

void render_image(uint8_t *fb, const char *name) {
    char path[512];
    int w = R.vw * R.ts, h = R.vh * R.ts;
    snprintf(path, sizeof path, "%s/Worlds/%s/Images/%s_1.png", R.data, R.world, name);
    const uint8_t *img = get_image(path, w, h);
    if (!img) return;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            const uint8_t *s = img + (j * w + i) * 4;
            uint8_t *d = fb + ((R.oy + j) * R.fbw + R.ox + i) * 3;
            for (int k = 0; k < 3; k++) d[k] = (d[k] * (255 - s[3]) + s[k] * s[3]) / 255;
        }
}
