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
    uint8_t *backdrop; // background colours + images, composed once per level
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

// Optional Data/sprites.pak from tools/prepare_sd.py: all sprite PNGs in one file, so the SD card isn't hit with
// thousands of tiny files. Format: "BPK1", u32 count, count x (u32 offset, u32 size, u16 len, name), sorted by name.
static struct {
    FILE *f;
    int n, tried;
    char **name;
    uint32_t *off, *size;
} P;

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void pack_open(void) {
    P.tried = 1;
    char path[300];
    snprintf(path, sizeof path, "%s/sprites.pak", R.data);
    FILE *f = fopen(path, "rb");
    uint8_t h[10];
    if (!f) return;
    if (fread(h, 1, 8, f) != 8 || memcmp(h, "BPK1", 4) || (P.n = le32(h + 4)) <= 0 || P.n > 100000) { fclose(f); P.n = 0; return; }
    P.name = calloc(P.n, sizeof *P.name);
    P.off = malloc(P.n * sizeof *P.off);
    P.size = malloc(P.n * sizeof *P.size);
    for (int i = 0; i < P.n; i++) {
        if (fread(h, 1, 10, f) != 10) { P.n = i; break; }
        int len = h[8] | h[9] << 8;
        P.off[i] = le32(h);
        P.size[i] = le32(h + 4);
        P.name[i] = malloc(len + 1);
        if (fread(P.name[i], 1, len, f) != (size_t)len) { free(P.name[i]); P.n = i; break; }
        P.name[i][len] = 0;
    }
    P.f = f;
}

// PNG bytes for a path inside the Data dir, or NULL if there's no pack or it doesn't have the file.
static uint8_t *pack_read(const char *path, long *len) {
    size_t dl = strlen(R.data);
    if (!P.tried && R.data[0]) pack_open();
    if (!P.f || strncmp(path, R.data, dl) || path[dl] != '/') return NULL;
    const char *rel = path + dl + 1;
    int lo = 0, hi = P.n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = strcmp(P.name[mid], rel);
        if (c < 0) lo = mid + 1;
        else if (c > 0) hi = mid - 1;
        else {
            uint8_t *b = malloc(P.size[mid]);
            if (b && !fseek(P.f, P.off[mid], SEEK_SET) && fread(b, 1, P.size[mid], P.f) == P.size[mid]) { *len = P.size[mid]; return b; }
            free(b);
            return NULL;
        }
    }
    return NULL;
}

// Loads and scales an image to w*h, cached by path. Missing files are cached as NULL so each is reported once.
// The returned pixels are only valid until the next call (a full cache is cleared).
static const uint8_t *get_image(const char *path, int dw, int dh) {
    char key[600]; // the same file can be cached at several sizes
    snprintf(key, sizeof key, "%s|%dx%d", path, dw, dh);
    unsigned i = hash(key) & (CACHE_SIZE - 1);
    for (; R.cache[i].path; i = (i + 1) & (CACHE_SIZE - 1))
        if (!strcmp(R.cache[i].path, key)) return R.cache[i].px;
    if (R.ncache >= CACHE_SIZE * 3 / 4) {
        cache_clear();
        i = hash(key) & (CACHE_SIZE - 1);
    }
    int w, h;
    long plen;
    uint8_t *packed = pack_read(path, &plen), *raw;
    if (packed) {
        raw = png_decode(packed, plen, &w, &h);
        free(packed);
    } else raw = png_load(path, &w, &h);
    if (!raw) printf("missing sprite %s\n", path);
    uint8_t *px = raw ? scale(raw, w, h, dw, dh) : NULL;
    free(raw);
    R.cache[i].path = strdup(key);
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
    free(R.backdrop);
    R.pal = NULL;
    R.backdrop = NULL;
}

int render_backdrop_cached(uint8_t *fb) {
    if (!R.backdrop) return 0;
    memcpy(fb, R.backdrop, R.fbw * R.fbh * 3);
    return 1;
}

void render_backdrop_save(const uint8_t *fb) {
    if (!R.backdrop) R.backdrop = malloc(R.fbw * R.fbh * 3);
    if (R.backdrop) memcpy(R.backdrop, fb, R.fbw * R.fbh * 3);
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

void render_sprite(uint8_t *fb, const char *sprite, int root, int frame, int wobble, int part, int x16, int y16, int c1, int c2) {
    if (x16 < 16 || y16 < 16 || x16 > R.vw * 16 || y16 > R.vh * 16) return;
    int px = R.ox + (x16 - 16) * R.ts / 16, py = R.oy + (y16 - 16) * R.ts / 16; // top-left pixel
    if (!strcmp(sprite, "#box")) { // solid tile with a 1px gap, for level icons the engine draws itself
        fill(fb, px + 1, py + 1, R.ts - 2, R.ts - 2, PAL(c1, c2));
        return;
    }
    if (!strcmp(sprite, "#cross")) { // 2px X over the text of a disabled rule
        for (int i = 1; i < R.ts - 1; i++) {
            fill(fb, px + i, py + i, 2 - (i == R.ts - 2), 1, PAL(c1, c2));
            fill(fb, px + R.ts - 1 - i - (i < R.ts - 2), py + i, 2 - (i == R.ts - 2), 1, PAL(c1, c2));
        }
        return;
    }
    char path[512];
    const char *dir = root ? "" : "Worlds/", *world = root ? "" : R.world, *slash = root ? "" : "/";
    if (sprite[0] == '@') // "@name": exact file name, e.g. map icons "icon_lake_1"
        snprintf(path, sizeof path, "%s/%s%s%sSprites/%s.png", R.data, dir, world, slash, sprite + 1);
    else
        snprintf(path, sizeof path, "%s/%s%s%sSprites/%s_%d_%d.png", R.data, dir, world, slash, sprite, frame, wobble);
    int w = part ? R.ts / 2 : R.ts;
    const uint8_t *spr = get_image(path, w, R.ts);
    if (!spr) return;
    const uint8_t *c = PAL(c1, c2);
    int px0 = px + (part == 2 ? R.ts - w : 0), py0 = py;
    for (int j = 0; j < R.ts; j++)
        for (int i = 0; i < w; i++) {
            const uint8_t *s = spr + (j * w + i) * 4;
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
