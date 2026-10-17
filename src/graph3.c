#include "graph3.h"

#include <stdlib.h>
#include <string.h>

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

/* --- display tags (see graph3.h) --- */

static const uint8_t *sprite_base;
static size_t sprite_set_bytes, sprite_frame_bytes;
static int sprite_sets, sprite_frames;

void g3_tag_sprites(const uint8_t *base, size_t set_bytes, size_t frame_bytes, int sets, int frames)
{
    sprite_base = base;
    sprite_set_bytes = set_bytes;
    sprite_frame_bytes = frame_bytes;
    sprite_sets = sets;
    sprite_frames = frames;
}

int g3_pic_tag(const uint8_t *pic)
{
    if (!sprite_base || pic < sprite_base || pic >= sprite_base + sprite_set_bytes * (size_t)sprite_sets)
        return TAG_NONE;
    const size_t off = (size_t)(pic - sprite_base);
    const size_t set = off / sprite_set_bytes, frame = off % sprite_set_bytes / sprite_frame_bytes;
    return frame < (size_t)sprite_frames ? 1 + (int)(set * (size_t)sprite_frames + frame) : TAG_NONE;
}

/* The tags of the last few GetPic buffers, for PutPic of the same buffer (the exit doors). */
#define TAG_COPIES 4
#define TAG_COPY_MAX (64 * 64)
static struct {
    const uint8_t *buf;
    int w, h;
    uint8_t tags[TAG_COPY_MAX]; /* top row first */
    uint16_t pos[TAG_COPY_MAX];
} copies[TAG_COPIES];
static int copy_next;

static int copied_tags(const uint8_t *pic, int w, int h)
{
    for (int i = 0; i < TAG_COPIES; i++)
        if (copies[i].buf == pic && copies[i].w == w && copies[i].h == h)
            return i;
    return -1;
}

void g3_plot_pic_pixel(const uint8_t *pic, int col, int row, int x, int y, int color)
{
    const uint8_t old = cga_cur_tag;
    const uint16_t old_pos = cga_cur_pos;
    cga_cur_tag = (uint8_t)g3_pic_tag(pic);
    cga_cur_pos = (uint16_t)(col | row << 8);
    g3_plot(x, y, color);
    cga_cur_tag = old;
    cga_cur_pos = old_pos;
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
    const uint8_t old_tag = cga_cur_tag;
    const uint16_t old_pos = cga_cur_pos;
    const int copy = copied_tags(pic, w, h);
    cga_cur_tag = (uint8_t)g3_pic_tag(pic);
    for (int r = 0; r < h; r++) {
        const int py = y - r; /* row 0 is the bottom row */
        if (py < 0 || py >= CGA_H)
            continue;
        const uint8_t *row = pic + 6 + r * stride;
        for (int c = 0; c < w; c++) {
            const int px = x + c;
            if (px >= 0 && px < CGA_W) {
                if (copy >= 0) {
                    cga_cur_tag = copies[copy].tags[(h - 1 - r) * w + c];
                    cga_cur_pos = copies[copy].pos[(h - 1 - r) * w + c];
                } else {
                    cga_cur_pos = (uint16_t)(c | (h - 1 - r) << 8);
                }
                cga_set(px, py, (row[c >> 2] >> (6 - 2 * (c & 3))) & 3);
            }
        }
    }
    cga_cur_tag = old_tag;
    cga_cur_pos = old_pos;
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
    uint8_t *tags = NULL;
    uint16_t *pos = NULL;
    if (w * h <= TAG_COPY_MAX) {
        copies[copy_next].buf = pic;
        copies[copy_next].w = w;
        copies[copy_next].h = h;
        tags = copies[copy_next].tags;
        pos = copies[copy_next].pos;
        memset(tags, TAG_NONE, (size_t)(w * h));
        for (int i = 0; i < TAG_COPIES; i++) /* one entry per buffer */
            if (i != copy_next && copies[i].buf == pic)
                copies[i].buf = NULL;
        copy_next = (copy_next + 1) % TAG_COPIES;
    }
    for (int r = 0; r < h; r++) {
        uint8_t *row = pic + 6 + r * stride;
        for (int i = 0; i < stride; i++)
            row[i] = 0;
        for (int c = 0; c < w; c++) {
            const int px = x1 + c, py = y2 - r; /* bottom row first */
            if (on_screen(px, py)) {
                row[c >> 2] |= (uint8_t)(cga_get(px, py) << (6 - 2 * (c & 3)));
                if (tags) {
                    tags[(h - 1 - r) * w + c] = cga_tag[py * CGA_W + px];
                    pos[(h - 1 - r) * w + c] = cga_tagpos[py * CGA_W + px];
                }
            }
        }
    }
}

int g3_get_dot_color(int x, int y)
{
    return on_screen(x, y) ? cga_get(x, y) : -1;
}

void g3_plot(int x, int y, int color)
{
    if (on_screen(x, y)) {
        const uint8_t old = cga_cur_tag;
        if (old == TAG_NONE) /* a plain Plot / Draw; g3_plot_tagged sets its own */
            cga_cur_tag = TAG_LINE;
        cga_set(x, y, color);
        cga_cur_tag = old;
    }
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
