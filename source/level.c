#include "level.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// Find 4-byte tag at or after pos; -1 if absent.
static long find(const uint8_t *b, long len, long pos, const char *tag) {
    for (; pos + 4 <= len; pos++)
        if (!memcmp(b + pos, tag, 4)) return pos;
    return -1;
}

static void *inflate_to(const uint8_t *src, uint32_t srclen, uLongf want) {
    uint8_t *dst = malloc(want);
    uLongf got = want;
    if (dst && uncompress(dst, &got, src, srclen) == Z_OK && got == want) return dst;
    free(dst);
    return NULL;
}

int level_load(const char *path, Level *out) {
    memset(out, 0, sizeof *out);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(len);
    if (!b || fread(b, 1, len, f) != (size_t)len) { fclose(f); free(b); return -1; }
    fclose(f);

    int err = -2;
    if (len < 10 || memcmp(b, "ACHTUNG!", 8)) goto done;
    out->version = b[8] | b[9] << 8;
    long i = find(b, len, 0, "LAYR");
    if (i < 0 || i + 10 > len) goto done;
    int n = b[i + 8] | b[i + 9] << 8;
    if (n > LEVEL_MAX_LAYERS) goto done;
    long pos = i + 10;
    for (int l = 0; l < n; l++) {
        Layer *L = &out->layers[l];
        if (pos + 8 > len) goto done;
        L->w = rd32(b + pos);
        L->h = rd32(b + pos + 4);
        uint32_t cells = L->w * L->h;
        if (!cells || cells > 256 * 256) goto done;
        // shortcut: skips per-layer header (scroll/zoom floats) by searching for MAIN; decode it if rendering needs them
        long m = find(b, len, pos, "MAIN");
        if (m < 0 || m + 8 > len) goto done;
        uint32_t size = rd32(b + m + 4);
        if (m + 8 + size > (uint32_t)len) goto done;
        uint8_t *raw = inflate_to(b + m + 8, size, cells * 2);
        if (!raw) goto done;
        L->tiles = malloc(cells * 2);
        for (uint32_t c = 0; c < cells; c++) L->tiles[c] = raw[c * 2] | raw[c * 2 + 1] << 8;
        free(raw);
        out->nlayers = l + 1;
        pos = m + 8 + size;
        if (pos + 5 > len || memcmp(b + pos, "DATA", 4)) continue; // last layer may omit DATA
        // DATA: u8 count, then per entry u32 id + u32 size + zlib (id 0 = direction)
        int count = b[pos + 4];
        pos += 5;
        for (int e = 0; e < count; e++) {
            if (pos + 8 > len) goto done;
            uint32_t id = rd32(b + pos), dsize = rd32(b + pos + 4);
            if (pos + 8 + dsize > (uint32_t)len) goto done;
            uint8_t *d = inflate_to(b + pos + 8, dsize, cells);
            if (!d) goto done;
            if (id == 0 && !L->dirs) L->dirs = d; else free(d);
            pos += 8 + dsize;
        }
    }
    err = 0;
done:
    free(b);
    if (err) level_free(out);
    return err;
}

void level_free(Level *lv) {
    for (int l = 0; l < LEVEL_MAX_LAYERS; l++) {
        free(lv->layers[l].tiles);
        free(lv->layers[l].dirs);
    }
    memset(lv, 0, sizeof *lv);
}

int level_palette(const char *ld_path, char *out, int outlen) {
    snprintf(out, outlen, "default.png");
    FILE *f = fopen(ld_path, "r");
    if (!f) return -1;
    char line[256];
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "palette=", 8)) {
            line[strcspn(line, "\r\n")] = 0;
            if (line[8]) snprintf(out, outlen, "%s", line + 8);
            break;
        }
    fclose(f);
    return 0;
}
