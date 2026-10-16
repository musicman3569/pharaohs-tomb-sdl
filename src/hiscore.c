#include "hiscore.h"

#include <stdio.h>
#include <string.h>

#include "tp.h"

hiscore_t hiscores[HISCORE_COUNT + 1];

#define RECORD_BYTES 0x106

static void hs_path(const gamedata_t *g, char *out, size_t len, int upper)
{
    char name[32];
    snprintf(name, sizeof name, "%s.hs", g->base_name);
    if (upper)
        for (char *c = name; *c; c++)
            if (*c >= 'a' && *c <= 'z')
                *c = (char)(*c - 32);
    snprintf(out, len, "%s%s", g->dir, name);
}

void hiscore_load(const gamedata_t *g)
{
    /* Defaults: scores 10000*i + 35000, names from the game unit (they read as a message). */
    static const unsigned name_off[HISCORE_COUNT + 1] = {0, 0x661A, 0x6605, 0x65F0, 0x65DB, 0x65C6};
    for (int i = 1; i <= HISCORE_COUNT; i++) {
        hiscores[i].score = 10000.0 * i + 35000.0;
        const size_t p = gamedata_addr(g, SEG_GAME, name_off[i]);
        const size_t n = p < g->image_len ? g->image[p] : 0;
        memcpy(hiscores[i].name, g->image + p + 1, n < 255 ? n : 255);
        hiscores[i].name[n < 255 ? n : 255] = '\0';
    }

    char path[600];
    FILE *f = NULL;
    for (int upper = 0; upper < 2 && !f; upper++) {
        hs_path(g, path, sizeof path, upper);
        f = fopen(path, "rb");
    }
    if (!f)
        return;
    unsigned char rec[RECORD_BYTES];
    hiscore_t loaded[HISCORE_COUNT + 1];
    int ok = 1;
    for (int i = 1; i <= HISCORE_COUNT && ok; i++) {
        if (fread(rec, 1, RECORD_BYTES, f) != RECORD_BYTES) {
            ok = 0;
            break;
        }
        loaded[i].score = tp_real48_decode(rec);
        memcpy(loaded[i].name, rec + 7, rec[6]);
        loaded[i].name[rec[6]] = '\0';
    }
    fclose(f);
    if (ok)
        memcpy(hiscores, loaded, sizeof hiscores);
}

void hiscore_save(const gamedata_t *g)
{
    char path[600];
    hs_path(g, path, sizeof path, 0);
    FILE *probe = fopen(path, "rb");
    if (!probe) { /* keep using an upper-case file if that's what exists */
        char upper[600];
        hs_path(g, upper, sizeof upper, 1);
        if ((probe = fopen(upper, "rb")) != NULL)
            snprintf(path, sizeof path, "%s", upper);
    }
    if (probe)
        fclose(probe);

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "ptomb: can't write %s\n", path);
        return;
    }
    for (int i = 1; i <= HISCORE_COUNT; i++) {
        unsigned char rec[RECORD_BYTES];
        memset(rec, 0, sizeof rec);
        tp_real48_encode(hiscores[i].score, rec);
        const size_t n = strlen(hiscores[i].name);
        rec[6] = (unsigned char)(n < 255 ? n : 255);
        memcpy(rec + 7, hiscores[i].name, rec[6]);
        fwrite(rec, 1, RECORD_BYTES, f);
    }
    fclose(f);
}

void hiscore_sort(void)
{
    for (int i = 1; i <= HISCORE_COUNT; i++)
        for (int j = HISCORE_COUNT; j > i; j--)
            if (hiscores[j - 1].score > hiscores[j].score) {
                const hiscore_t t = hiscores[j - 1];
                hiscores[j - 1] = hiscores[j];
                hiscores[j] = t;
            }
}
