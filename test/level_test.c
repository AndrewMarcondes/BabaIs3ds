// Host check: prints a fingerprint per level; test/level_test.sh diffs it against tools/baba_level.py.
#include <stdio.h>
#include "../source/level.h"

int main(int argc, char **argv) {
    for (int a = 1; a < argc; a++) {
        Level lv;
        if (level_load(argv[a], &lv)) { printf("%s FAIL\n", argv[a]); continue; }
        printf("%s v%d", argv[a], lv.version);
        for (int l = 0; l < lv.nlayers; l++) {
            Layer *L = &lv.layers[l];
            unsigned long ts = 0, ds = 0;
            for (uint32_t c = 0; c < L->w * L->h; c++) { ts += L->tiles[c] * (c + 1); if (L->dirs) ds += L->dirs[c] * (c + 1); }
            printf(" %ux%u:%lu:%lu", L->w, L->h, ts, ds);
        }
        printf("\n");
        level_free(&lv);
    }
}
