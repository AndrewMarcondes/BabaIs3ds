#pragma once
#include <stdint.h>

#define LEVEL_MAX_LAYERS 3
#define TILE_EMPTY 0xFFFF

typedef struct {
    uint32_t w, h;
    uint16_t *tiles; // w*h, id = tile[0] + tile[1]*256 from values.lua tileslist
    uint8_t *dirs;   // w*h facing direction, NULL if layer has no DATA chunk
} Layer;

typedef struct {
    uint16_t version;
    int nlayers;
    Layer layers[LEVEL_MAX_LAYERS];
} Level;

// Returns 0 on success. Format notes in NOTES.md.
int level_load(const char *path, Level *out);
void level_free(Level *lv);

// Reads palette= from a level's .ld file into out (default.png if absent). -1 if the file can't be opened.
int level_palette(const char *ld_path, char *out, int outlen);
