#include "render.h"
#include "png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __3DS__
#include <citro2d.h>
#endif

#define CACHE_SIZE 2048 // power of two; open addressing, cleared when 3/4 full

typedef struct {
    char *path;  // NULL = empty slot
    uint8_t *px; // scaled RGBA; NULL if the file is missing
#ifdef __3DS__
    C3D_Tex *tex; // px on the GPU, uploaded the first time it's drawn there
#endif
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

#ifdef __3DS__
static void retire(C3D_Tex *t);
#endif

static void cache_clear(void) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        free(R.cache[i].path);
        free(R.cache[i].px);
        R.cache[i].path = NULL;
        R.cache[i].px = NULL;
#ifdef __3DS__
        retire(R.cache[i].tex);
        R.cache[i].tex = NULL;
#endif
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

// Loads and scales an image to w*h, cached by path. Missing files are cached with px NULL so each is reported once.
// The entry is only valid until the next call (a full cache is cleared).
static Sprite *get_entry(const char *path, int dw, int dh) {
    char key[600]; // the same file can be cached at several sizes
    snprintf(key, sizeof key, "%s|%dx%d", path, dw, dh);
    unsigned i = hash(key) & (CACHE_SIZE - 1);
    for (; R.cache[i].path; i = (i + 1) & (CACHE_SIZE - 1))
        if (!strcmp(R.cache[i].path, key)) return &R.cache[i];
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
    return &R.cache[i];
}

static const uint8_t *get_image(const char *path, int dw, int dh) { return get_entry(path, dw, dh)->px; }

#ifdef __3DS__
static int backdrop_dirty; // R.backdrop changed since it was last uploaded
static void gpu_sprite(Sprite *e, int w, int h, int x, int y, const uint8_t *c);
static void make_tex(Sprite *e, int w, int h);
static void gpu_backdrop(void);
#endif

static void fill(uint8_t *fb, int x, int y, int w, int h, const uint8_t *c) {
#ifdef __3DS__
    if (!fb) {
        C2D_DrawRectSolid(x, y, 0.5f, w, h, C2D_Color32(c[0], c[1], c[2], 255));
        return;
    }
#endif
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
#ifdef __3DS__
    if (!fb) {
        gpu_backdrop();
        return 1;
    }
#endif
    memcpy(fb, R.backdrop, R.fbw * R.fbh * 3);
    return 1;
}

void render_backdrop_save(const uint8_t *fb) {
    if (!R.backdrop) R.backdrop = malloc(R.fbw * R.fbh * 3);
    if (R.backdrop) memcpy(R.backdrop, fb, R.fbw * R.fbh * 3);
#ifdef __3DS__
    backdrop_dirty = 1;
#endif
}

int render_begin_level(const char *data, const char *world, const char *palette, int roomw, int roomh, int fbw, int fbh) {
    render_end_level();
    char path[512];
    snprintf(R.data, sizeof R.data, "%s", data);
    snprintf(R.world, sizeof R.world, "%s", world);
    snprintf(path, sizeof path, "%s/%s", data, palette);
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

// Colour c darkened (shade < 0) or lightened (shade > 0) by |shade| percent, into out.
static const uint8_t *shaded(const uint8_t *c, int shade, uint8_t *out) {
    if (!shade) return c;
    for (int k = 0; k < 3; k++) out[k] = shade < 0 ? c[k] * (100 + shade) / 100 : c[k] + (255 - c[k]) * shade / 100;
    return out;
}

void render_sprite(uint8_t *fb, const char *sprite, int root, int frame, int wobble, int part, int x16, int y16, int c1, int c2, int shade) {
    uint8_t tmp[3];
    if (x16 < 16 || y16 < 16 || x16 > R.vw * 16 || y16 > R.vh * 16) return;
    int px = R.ox + (x16 - 16) * R.ts / 16, py = R.oy + (y16 - 16) * R.ts / 16; // top-left pixel
    if (!strcmp(sprite, "#box")) { // solid tile with a 1px gap, for level icons the engine draws itself
        fill(fb, px + 1, py + 1, R.ts - 2, R.ts - 2, shaded(PAL(c1, c2), shade, tmp));
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
    Sprite *e = get_entry(path, w, R.ts);
    const uint8_t *spr = e->px;
    if (!spr) return;
    const uint8_t *c = shaded(PAL(c1, c2), shade, tmp);
    int px0 = px + (part == 2 ? R.ts - w : 0), py0 = py;
#ifdef __3DS__
    if (!fb) {
        gpu_sprite(e, w, R.ts, px0, py0, c);
        return;
    }
#endif
    for (int j = 0; j < R.ts; j++)
        for (int i = 0; i < w; i++) {
            const uint8_t *s = spr + (j * w + i) * 4;
            uint8_t *d = fb + ((py0 + j) * R.fbw + px0 + i) * 3;
            for (int k = 0; k < 3; k++) d[k] = (d[k] * (255 - s[3]) + s[k] * c[k] / 255 * s[3]) / 255;
        }
}

int render_preload(const char *sprite, int root) {
    if (!P.tried && R.data[0]) pack_open();
    if (!P.f || !R.pal) return 0;
    char prefix[160];
    snprintf(prefix, sizeof prefix, "%s%s%sSprites/%s_", root ? "" : "Worlds/", root ? "" : R.world, root ? "" : "/", sprite);
    size_t len = strlen(prefix);
    int lo = 0, hi = P.n; // first pack name >= prefix (names are sorted)
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (strcmp(P.name[mid], prefix) < 0) lo = mid + 1;
        else hi = mid;
    }
    int n = 0;
    // up to half the cache, so preloading never makes it clear itself
    for (int i = lo; i < P.n && !strncmp(P.name[i], prefix, len) && R.ncache < CACHE_SIZE / 2; i++) {
        int frame, wobble;
        char png;
        if (sscanf(P.name[i] + len, "%d_%d.pn%c", &frame, &wobble, &png) != 3) continue; // another sprite, e.g. "fire_alt"
        char path[512];
        snprintf(path, sizeof path, "%s/%s", R.data, P.name[i]);
        Sprite *e = get_entry(path, R.ts, R.ts);
#ifdef __3DS__
        if (e->px && !e->tex) make_tex(e, R.ts, R.ts);
#else
        (void)e;
#endif
        n++;
    }
    return n;
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

// The level's name in the game's letter sprites, centred on black, wrapped to the room width.
void render_card(uint8_t *fb, const char *name, int wobble) {
    int vw = R.vw, vh = R.vh;
    memset(fb, 0, R.fbw * R.fbh * 3);
    char lines[4][64];
    int n = 0;
    lines[0][0] = 0;
    for (const char *w = name; *w && n < 4;) { // greedy word wrap
        int len = strcspn(w, " ");
        int cur = strlen(lines[n]);
        if (cur && cur + 1 + len > vw) { if (++n == 4) break; lines[n][0] = 0; cur = 0; }
        if (cur) strcat(lines[n], " ");
        strncat(lines[n], w, len < 60 - cur ? len : 60 - cur);
        w += len;
        while (*w == ' ') w++;
    }
    int rows = n < 4 ? n + 1 : 4;
    for (int r = 0; r < rows; r++) {
        int len = strlen(lines[r]), x = 1 + (vw - len) / 2, y = 1 + (vh - rows) / 2 + r;
        for (int i = 0; i < len; i++) {
            char c = lines[r][i], sprite[16];
            if (c >= 'A' && c <= 'Z') c += 32;
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) continue; // no sprites for punctuation
            snprintf(sprite, sizeof sprite, "text_%c", c);
            render_sprite(fb, sprite, 1, 0, wobble, 0, (x + i) * 16, y * 16, 0, 3, 0);
        }
    }
}

#ifdef __3DS__
//---------------------------------------------------------------------------- GPU (3DS)
// Each frame is drawn by the GPU: the backdrop as one texture, sprites as small textures multiplied by their palette
// colour, boxes as rectangles. The CPU renderer above still composes what is drawn once (a level's backdrop, the title
// picture, the level card); those are uploaded as they are.

static C3D_RenderTarget *top;
static C3D_Tex backdrop_tex, canvas_tex; // 512x256: the backdrop, and a full-screen picture
static C3D_Tex *dead[CACHE_SIZE * 2];   // textures the frame in flight may still use, freed when the next one begins
static int ndead;

static void retire(C3D_Tex *t) {
    if (!t) return;
    if (ndead < (int)(sizeof dead / sizeof *dead)) {
        dead[ndead++] = t;
        return;
    }
    C3D_TexDelete(t); // the list only fills up if the whole cache is cleared twice between two frames
    free(t);
}

static void free_dead(void) {
    while (ndead) {
        C3D_Tex *t = dead[--ndead];
        C3D_TexDelete(t);
        free(t);
    }
}

static int pow2(int n) {
    int p = 8; // the GPU's smallest texture side
    while (p < n) p <<= 1;
    return p;
}

// Rows of RGBA (bpp 4) or opaque RGB (bpp 3) pixels into the top-left corner of a tiled RGBA8 texture.
static void upload(C3D_Tex *t, const uint8_t *px, int w, int h, int bpp) {
    uint32_t *dst = t->data;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t *s = px + (y * w + x) * bpp;
            // 8x8 tiles, Morton order inside each tile
            uint32_t pos = (((y >> 3) * (t->width >> 3) + (x >> 3)) << 6) |
                           (x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3;
            dst[pos] = (uint32_t)s[0] << 24 | s[1] << 16 | s[2] << 8 | (bpp == 4 ? s[3] : 255);
        }
    C3D_TexFlush(t);
}

static void draw_tex(C3D_Tex *t, int w, int h, int x, int y, const C2D_ImageTint *tint) {
    Tex3DS_SubTexture sub = {w, h, 0.0f, 1.0f, (float)w / t->width, 1.0f - (float)h / t->height};
    C2D_Image img = {t, &sub};
    C2D_DrawImageAt(img, x, y, 0.5f, tint, 1.0f, 1.0f);
}

static void make_tex(Sprite *e, int w, int h) {
    C3D_Tex *t = malloc(sizeof *t);
    if (!t || !C3D_TexInit(t, pow2(w), pow2(h), GPU_RGBA8)) {
        free(t);
        return;
    }
    C3D_TexSetFilter(t, GPU_NEAREST, GPU_NEAREST);
    memset(t->data, 0, t->size);
    upload(t, e->px, w, h, 4);
    e->tex = t;
}

static void gpu_sprite(Sprite *e, int w, int h, int x, int y, const uint8_t *c) {
    if (!e->tex) make_tex(e, w, h);
    if (!e->tex) return;
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, C2D_Color32(c[0], c[1], c[2], 255), 1.0f);
    draw_tex(e->tex, w, h, x, y, &tint);
}

static void gpu_backdrop(void) {
    if (backdrop_dirty) {
        upload(&backdrop_tex, R.backdrop, R.fbw, R.fbh, 3);
        backdrop_dirty = 0;
    }
    draw_tex(&backdrop_tex, R.fbw, R.fbh, 0, 0, NULL);
}

int render_gpu_init(void) {
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE) || !C2D_Init(8192)) return -1;
    C2D_Prepare();
    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    if (!top || !C3D_TexInit(&backdrop_tex, 512, 256, GPU_RGBA8) || !C3D_TexInit(&canvas_tex, 512, 256, GPU_RGBA8)) return -1;
    C3D_TexSetFilter(&backdrop_tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetFilter(&canvas_tex, GPU_NEAREST, GPU_NEAREST);
    return 0;
}

void render_gpu_exit(void) {
    cache_clear();
    free_dead();
    C3D_TexDelete(&backdrop_tex);
    C3D_TexDelete(&canvas_tex);
    C2D_Fini();
    C3D_Fini();
}

void render_gpu_begin(void) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW); // returns once the previous frame is done with its textures
    free_dead();
    C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(top);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR); // later draws go on top, like the CPU renderer
    C2D_SetTintMode(C2D_TintMult);
    C2D_Fade(0);
}

void render_gpu_fade(int k) { C2D_Fade(C2D_Color32(0, 0, 0, k >= 256 ? 0 : 255 - k)); }

void render_gpu_canvas(const uint8_t *rgb) {
    upload(&canvas_tex, rgb, 400, 240, 3);
    draw_tex(&canvas_tex, 400, 240, 0, 0, NULL);
}

void render_gpu_end(void) { C3D_FrameEnd(0); }
#endif
