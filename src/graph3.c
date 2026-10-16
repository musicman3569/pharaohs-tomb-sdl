#include "graph3.h"

#include <stdlib.h>

#include "cga.h"

static int on_screen(int x, int y)
{
    return x >= 0 && x < CGA_W && y >= 0 && y < CGA_H;
}

static unsigned rd16(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}

static void wr16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

void g3_graph_color_mode(void)
{
    cga_clear();
}

void g3_put_pic(const uint8_t *pic, int x, int y)
{
    const int mode = (int)rd16(pic), w = (int)rd16(pic + 2), h = (int)rd16(pic + 4);
    if (mode != 2 || w <= 0 || h <= 0)
        return;
    const int stride = (w * 2 + 7) / 8;
    for (int r = 0; r < h; r++) {
        const int py = y - r; /* row 0 is the bottom row */
        if (py < 0 || py >= CGA_H)
            continue;
        const uint8_t *row = pic + 6 + r * stride;
        for (int c = 0; c < w; c++) {
            const int px = x + c;
            if (px >= 0 && px < CGA_W)
                cga_set(px, py, (row[c >> 2] >> (6 - 2 * (c & 3))) & 3);
        }
    }
}

size_t g3_pic_size(int w, int h)
{
    return 6 + (size_t)h * (size_t)((w * 2 + 7) / 8);
}

void g3_get_pic(uint8_t *pic, int x1, int y1, int x2, int y2)
{
    if (x2 < x1) { int t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { int t = y1; y1 = y2; y2 = t; }
    const int w = x2 - x1 + 1, h = y2 - y1 + 1, stride = (w * 2 + 7) / 8;
    wr16(pic, 2);
    wr16(pic + 2, (unsigned)w);
    wr16(pic + 4, (unsigned)h);
    for (int r = 0; r < h; r++) {
        uint8_t *row = pic + 6 + r * stride;
        for (int i = 0; i < stride; i++)
            row[i] = 0;
        for (int c = 0; c < w; c++) {
            const int px = x1 + c, py = y2 - r; /* bottom row first */
            if (on_screen(px, py))
                row[c >> 2] |= (uint8_t)(cga_get(px, py) << (6 - 2 * (c & 3)));
        }
    }
}

int g3_get_dot_color(int x, int y)
{
    return on_screen(x, y) ? cga_get(x, y) : -1;
}

void g3_plot(int x, int y, int color)
{
    if (on_screen(x, y))
        cga_set(x, y, color);
}

void g3_draw(int x1, int y1, int x2, int y2, int color)
{
    /* Bresenham; both end points are drawn. */
    const int dx = abs(x2 - x1), dy = -abs(y2 - y1);
    const int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        g3_plot(x1, y1, color);
        if (x1 == x2 && y1 == y2)
            break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}
