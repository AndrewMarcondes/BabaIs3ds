// Minimal PNG decoder: 8-bit, non-interlaced, colour types 2/3/6 (all the game uses). Output RGBA8.
#include "png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

uint8_t *png_load(const char *path, int *ow, int *oh) {
    *ow = *oh = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = len > 0 ? malloc(len) : NULL, *out = NULL;
    if (b && fread(b, 1, len, f) == (size_t)len) out = png_decode(b, len, ow, oh);
    fclose(f);
    free(b);
    return out;
}

uint8_t *png_decode(const uint8_t *b, long len, int *ow, int *oh) {
    *ow = *oh = 0;
    uint8_t *idat = NULL, *raw = NULL, *out = NULL;
    if (len < 33 || memcmp(b, "\x89PNG\r\n\x1a\n", 8)) goto fail;

    uint32_t w = be32(b + 16), h = be32(b + 20);
    int depth = b[24], ctype = b[25], interlace = b[28];
    int bpp = ctype == 2 ? 3 : ctype == 6 ? 4 : ctype == 3 ? 1 : 0;
    if (depth != 8 || !bpp || interlace || !w || !h || w > 4096 || h > 4096) goto fail;

    uint8_t pal[256][4];
    memset(pal, 255, sizeof pal);
    size_t idlen = 0;
    if (!(idat = malloc(len))) goto fail;
    for (long p = 8; p + 12 <= len;) {
        uint32_t n = be32(b + p);
        const uint8_t *type = b + p + 4, *d = b + p + 8;
        if (n > (uint32_t)(len - p - 12)) goto fail;
        if (!memcmp(type, "IDAT", 4)) { memcpy(idat + idlen, d, n); idlen += n; }
        else if (!memcmp(type, "PLTE", 4)) for (uint32_t i = 0; i < n / 3 && i < 256; i++) memcpy(pal[i], d + i * 3, 3);
        else if (!memcmp(type, "tRNS", 4) && ctype == 3) for (uint32_t i = 0; i < n && i < 256; i++) pal[i][3] = d[i];
        else if (!memcmp(type, "IEND", 4)) break;
        p += 12 + n;
    }

    size_t stride = w * bpp;
    uLongf rawlen = (stride + 1) * h;
    raw = malloc(rawlen);
    if (!raw || uncompress(raw, &rawlen, idat, idlen) != Z_OK || rawlen != (stride + 1) * h) goto fail;

    // Undo per-row filters in place; row y's data starts at raw + y*(stride+1) + 1.
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = raw + y * (stride + 1) + 1, *up = y ? row - stride - 1 : NULL;
        int ft = row[-1];
        for (size_t x = 0; x < stride; x++) {
            int a = x >= (size_t)bpp ? row[x - bpp] : 0, u = up ? up[x] : 0, c = up && x >= (size_t)bpp ? up[x - bpp] : 0;
            row[x] += ft == 1 ? a : ft == 2 ? u : ft == 3 ? (a + u) / 2 : ft == 4 ? paeth(a, u, c) : 0;
        }
    }

    if (!(out = malloc(w * h * 4))) goto fail;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *row = raw + y * (stride + 1) + 1;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *o = out + (y * w + x) * 4;
            if (ctype == 3) memcpy(o, pal[row[x]], 4);
            else {
                memcpy(o, row + x * bpp, 3);
                // RGB sprites have no alpha: black is the transparent colour key
                o[3] = ctype == 6 ? row[x * 4 + 3] : (o[0] | o[1] | o[2]) ? 255 : 0;
            }
        }
    }
    *ow = w;
    *oh = h;
fail:
    free(idat);
    free(raw);
    if (out && !*ow) { free(out); out = NULL; }
    return out;
}
