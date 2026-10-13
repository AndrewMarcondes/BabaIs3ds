#pragma once
#include <stdint.h>

// Returns malloc'd RGBA8 pixels or NULL. Caller frees.
uint8_t *png_load(const char *path, int *w, int *h);
// Same, from a PNG already in memory (b is not freed).
uint8_t *png_decode(const uint8_t *b, long len, int *w, int *h);
