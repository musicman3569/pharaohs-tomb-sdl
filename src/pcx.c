#include "pcx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "tp.h"

int pcx_load(const char *path, pcx_image_t *img, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errlen, "can't open %s", path);
        return -1;
    }
    uint8_t buf[65536];
    const size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n < 128 || buf[0] != 0x0A || buf[2] != 1 || buf[3] != 2 || buf[65] != 1) {
        snprintf(err, errlen, "%s is not a 2 bpp RLE PCX file", path);
        return -1;
    }
    const int w = (buf[8] | buf[9] << 8) - (buf[4] | buf[5] << 8) + 1;
    const int h = (buf[10] | buf[11] << 8) - (buf[6] | buf[7] << 8) + 1;
    const int bpl = buf[66] | buf[67] << 8;
    if (w != CGA_W || h != CGA_H || bpl != CGA_STRIDE) {
        snprintf(err, errlen, "%s is %dx%d, expected 320x200", path, w, h);
        return -1;
    }
    size_t pos = 128, out = 0;
    while (out < sizeof img->pixels && pos < n) {
        const uint8_t b = buf[pos++];
        if (b >= 0xC0) {
            const int run = b & 0x3F;
            const uint8_t v = pos < n ? buf[pos++] : 0;
            for (int i = 0; i < run && out < sizeof img->pixels; i++)
                img->pixels[out++] = v;
        } else {
            img->pixels[out++] = b;
        }
    }
    if (out < sizeof img->pixels) {
        snprintf(err, errlen, "%s ends early", path);
        return -1;
    }
    return 0;
}

static int px(const pcx_image_t *img, int x, int y)
{
    return (img->pixels[y * CGA_STRIDE + (x >> 2)] >> (6 - 2 * (x & 3))) & 3;
}

void pcx_show_effect(const pcx_image_t *img, int variant, int delay, int type)
{
    /* Grain: how pixels are grouped as they appear. */
    const int cell = type == 1 || type == 6 ? 8 : type == 3 || type == 8 ? 2 : 1;
    const int steps = 40;
    const int ms_per_step = 8 + delay * 3; /* ~0.3 s at delay 0, ~0.8 s at delay 4 */

    for (int s = 1; s <= steps && !plat_quitting(); s++) {
        for (int y = 0; y < CGA_H; y++)
            for (int x = 0; x < CGA_W; x++) {
                const int cx = (x / cell) * cell, cy = (y / cell) * cell;
                int sum = 0, n = 0;
                if (variant & 1) { sum += cx * 1000 / CGA_W; n++; }
                if (variant & 2) { sum += (CGA_W - 1 - cx) * 1000 / CGA_W; n++; }
                if (variant & 4) { sum += cy * 1000 / CGA_H; n++; }
                if (variant & 8) { sum += (CGA_H - 1 - cy) * 1000 / CGA_H; n++; }
                const int order = n ? sum / n : 0;
                if (order < s * 1000 / steps)
                    cga_set(x, y, px(img, x, y));
            }
        plat_crt_delay(ms_per_step);
    }
    memcpy(cga_vram, img->pixels, sizeof img->pixels);
}

void pcx_show_random_effect(const pcx_image_t *img)
{
    static const signed char table[18][3] = { /* (variant, delay, type) as in 1000:04D1 */
        {4, 0, 3}, {8, 0, 3}, {2, 0, 3}, {1, 0, 3}, {1, 4, 0}, {2, 4, 0}, {4, 4, 0}, {8, 4, 0}, {2, 0, 8},
        {1, 0, 8}, {2, 4, 6}, {1, 4, 6}, {1, 4, 1}, {2, 4, 1}, {6, 0, 7}, {5, 0, 7}, {10, 0, 7}, {9, 0, 7},
    };
    const int r = tp_random(18);
    pcx_show_effect(img, table[r][0], table[r][1], table[r][2]);
}
