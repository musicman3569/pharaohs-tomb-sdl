#include "gamedata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lzexe.h"

#define SET_BYTES 0x270
#define SLOT_BYTES 0x9C

static unsigned rd16(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    uint8_t *buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n > 0 && n < 16 * 1024 * 1024 && fseek(f, 0, SEEK_SET) == 0) {
            buf = malloc((size_t)n);
            if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
                free(buf);
                buf = NULL;
            }
            *len = (size_t)n;
        }
    }
    fclose(f);
    return buf;
}

/* Every Turbo Pascal `interrupt` procedure starts by saving registers and loading DS:
 *   push ax bx cx dx si di ds es bp / mov bp,sp / [sub sp,n] / mov ax,DSEG / mov ds,ax */
static int find_data_segment(const uint8_t *img, size_t len, unsigned *seg)
{
    static const uint8_t prologue[] = {0x50, 0x53, 0x51, 0x52, 0x56, 0x57, 0x1E, 0x06, 0x55, 0x89, 0xE5};
    int found = 0;
    for (size_t i = 0; i + sizeof prologue + 8 <= len; i++) {
        if (memcmp(img + i, prologue, sizeof prologue) != 0)
            continue;
        size_t p = i + sizeof prologue;
        if (img[p] == 0x83 && img[p + 1] == 0xEC)
            p += 3;
        if (img[p] != 0xB8 || img[p + 3] != 0x8E || img[p + 4] != 0xD8)
            continue;
        unsigned s = rd16(img + p + 1);
        if (found && s != *seg)
            return -1; /* ambiguous */
        *seg = s;
        found = 1;
    }
    return found ? 0 : -1;
}

typedef struct {
    unsigned target, row, str;
} row_call_t;

/* StartLevel draws each row with   mov ax,ROW / push ax / mov di,STR / push cs / push di /
 * call DrawLevelRow   and the strings are string[20] constants in the main code segment. */
static int find_levels(gamedata_t *g, char *err, size_t errlen)
{
    const uint8_t *img = g->image;
    size_t scan_end = g->entry + 0x200 < g->image_len ? g->entry + 0x200 : g->image_len;
    if (scan_end > 0x10000)
        scan_end = 0x10000; /* main program is segment 0 */

    row_call_t *calls = malloc(sizeof *calls * 1024);
    size_t ncalls = 0;
    if (!calls) {
        snprintf(err, errlen, "out of memory");
        return -1;
    }
    for (size_t i = 0; i + 12 <= scan_end && ncalls < 1024; i++) {
        if (img[i] == 0xB8 && img[i + 2] == 0x00 && img[i + 3] == 0x50 && img[i + 4] == 0xBF &&
            img[i + 7] == 0x0E && img[i + 8] == 0x57 && img[i + 9] == 0xE8) {
            int rel = (int)rd16(img + i + 10);
            if (rel >= 0x8000)
                rel -= 0x10000;
            calls[ncalls].target = (unsigned)((int)(i + 12) + rel) & 0xFFFF;
            calls[ncalls].row = img[i + 1];
            calls[ncalls].str = rd16(img + i + 5);
            ncalls++;
        }
    }

    /* DrawLevelRow is the most common target. */
    unsigned draw_row = 0;
    size_t best = 0;
    for (size_t i = 0; i < ncalls; i++) {
        size_t n = 0;
        for (size_t j = 0; j < ncalls; j++)
            n += calls[j].target == calls[i].target;
        if (n > best) {
            best = n;
            draw_row = calls[i].target;
        }
    }

    int level = -1;
    unsigned expect = 1;
    for (size_t i = 0; i < ncalls; i++) {
        if (calls[i].target != draw_row)
            continue;
        if (calls[i].row == 1) {
            if ((level >= 0 && expect != LEVEL_ROWS + 1) || level + 1 >= MAX_LEVELS)
                break;
            level++;
            expect = 1;
        }
        if (level < 0 || calls[i].row != expect || calls[i].str >= g->image_len)
            break;
        char *dst = g->rows[level][expect - 1];
        unsigned n = img[calls[i].str];
        memset(dst, ' ', LEVEL_COLS);
        dst[LEVEL_COLS] = '\0';
        for (unsigned c = 0; c < n && c < LEVEL_COLS && calls[i].str + 1 + c < g->image_len; c++)
            dst[c] = (char)img[calls[i].str + 1 + c];
        expect++;
    }
    free(calls);

    g->num_levels = level + 1;
    if (g->num_levels > 0 && expect != LEVEL_ROWS + 1)
        g->num_levels--; /* last level incomplete */
    if (g->num_levels <= 0) {
        snprintf(err, errlen, "no level data found (is this a Pharaoh's Tomb EXE?)");
        return -1;
    }
    return 0;
}

int gamedata_load(gamedata_t *g, const char *path, char *err, size_t errlen)
{
    memset(g, 0, sizeof *g);
    size_t len = 0;
    uint8_t *file = read_file(path, &len);
    if (!file) {
        snprintf(err, errlen, "can't read %s", path);
        return -1;
    }
    if (len < 0x20 || file[0] != 'M' || file[1] != 'Z') {
        free(file);
        snprintf(err, errlen, "%s is not a DOS executable", path);
        return -1;
    }

    if (lzexe_is_packed(file, len)) {
        char lzerr[128];
        if (lzexe_unpack(file, len, &g->image, &g->image_len, &g->entry, lzerr, sizeof lzerr) != 0) {
            free(file);
            snprintf(err, errlen, "%s: %s", path, lzerr);
            return -1;
        }
        free(file);
    } else {
        size_t header = rd16(file + 8) * 16u;
        if (header >= len) {
            free(file);
            snprintf(err, errlen, "%s: bad MZ header", path);
            return -1;
        }
        g->image_len = len - header;
        g->image = malloc(g->image_len);
        if (!g->image) {
            free(file);
            snprintf(err, errlen, "out of memory");
            return -1;
        }
        memcpy(g->image, file + header, g->image_len);
        g->entry = rd16(file + 0x16) * 16u + rd16(file + 0x14);
        free(file);
    }

    if (find_data_segment(g->image, g->image_len, &g->ds_seg) != 0 ||
        g->ds_seg * 16u + SPRITE_SETS * SET_BYTES > g->image_len) {
        snprintf(err, errlen, "%s: can't locate the data segment", path);
        gamedata_free(g);
        return -1;
    }
    g->ds = g->image + g->ds_seg * 16u;
    g->ds_len = g->image_len - g->ds_seg * 16u;

    if (find_levels(g, err, errlen) != 0) {
        gamedata_free(g);
        return -1;
    }
    return 0;
}

void gamedata_free(gamedata_t *g)
{
    free(g->image);
    memset(g, 0, sizeof *g);
}

const uint8_t *gamedata_sprite(const gamedata_t *g, int set, int frame)
{
    if (set < 0 || set >= SPRITE_SETS || frame < 0 || frame >= SPRITE_FRAMES)
        return NULL;
    const uint8_t *p = g->ds + set * SET_BYTES + frame * SLOT_BYTES;
    return rd16(p) == 2 ? p : NULL;
}
