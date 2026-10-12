#pragma once
#include <stdint.h>

// Draws a level into an RGB8 row-major buffer (fbw x fbh).
// data = game Data dir, level = path inside it without extension, e.g. "Worlds/baba/0level".
int render_level(const char *data, const char *level, uint8_t *fb, int fbw, int fbh);
