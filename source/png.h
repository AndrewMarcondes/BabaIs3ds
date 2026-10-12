#pragma once
#include <stdint.h>

// Returns malloc'd RGBA8 pixels or NULL. Caller frees.
uint8_t *png_load(const char *path, int *w, int *h);
