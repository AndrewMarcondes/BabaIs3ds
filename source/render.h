#pragma once
#include <stdint.h>

// Software renderer into an RGB8 row-major buffer (fbw x fbh). One level at a time.
// data = game Data dir, world = e.g. "baba", palette = path inside data, e.g. "Palettes/default.png". 0 on success.
int render_begin_level(const char *data, const char *world, const char *palette, int roomw, int roomh, int fbw, int fbh);
void render_end_level(void);
void render_clear(uint8_t *fb);
// x16, y16 = position in 1/16 room tiles (so moving units can sit between tiles); c1, c2 = palette cell; root = sprite lives in Data/Sprites rather than the world's folder;
// wobble = 1..3, the hand-drawn frame every sprite cycles through; part 0 = whole tile, 1/2 = squeezed into the left/right half.
// sprite "#box" draws a solid tile and "#cross" an X instead; "@name" loads Sprites/name.png exactly (no frame suffix).
void render_sprite(uint8_t *fb, const char *sprite, int root, int frame, int wobble, int part, int x16, int y16, int c1, int c2);
// Full-colour background image Worlds/<world>/Images/<name>_1.png stretched over the play area.
void render_image(uint8_t *fb, const char *name);
// The level backdrop (clear + images) never changes within a level: compose it once, then copy it.
// render_backdrop_cached copies it into fb and returns 1 if it was saved; render_backdrop_save stores fb as it.
int render_backdrop_cached(uint8_t *fb);
void render_backdrop_save(const uint8_t *fb);
