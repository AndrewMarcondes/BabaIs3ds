#pragma once
#include <stdint.h>

// Software renderer into an RGB8 row-major buffer (fbw x fbh). One level at a time.
// data = game Data dir, world = e.g. "baba", palette = file in Data/Palettes. 0 on success.
int render_begin_level(const char *data, const char *world, const char *palette, int roomw, int roomh, int fbw, int fbh);
void render_end_level(void);
void render_clear(uint8_t *fb);
// x, y in room tiles; c1, c2 = palette cell; root = sprite lives in Data/Sprites rather than the world's folder.
// sprite "#box" draws a solid tile instead.
void render_sprite(uint8_t *fb, const char *sprite, int root, int frame, int x, int y, int c1, int c2);
// Full-colour background image Worlds/<world>/Images/<name>_1.png stretched over the play area.
void render_image(uint8_t *fb, const char *name);
