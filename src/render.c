#include "render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cga.h"
#include "gamedata.h"
#include "graph3.h"
#include "png.h"
#include "settings.h"
#include "timer.h"

/* ------------------------------------------------------------------------------------------ */
/* Custom sprites                                                                             */

/* The layout tools/extract_gfx.py writes: per set, 4 frames side by side in 24x24 cells with a
 * 1-px grid round them (101 x 26), times the zoom. A frame's picture starts at the cell's top
 * left. */
#define CELL 24
#define STRIP_W (SPRITE_FRAMES * (CELL + 1) + 1)
#define STRIP_H (CELL + 2)
#define MAX_ZOOM 8

static struct {
    uint32_t *px; /* NULL if this set has no custom art */
    int w;        /* the image's width (stride) */
    int zoom;     /* image pixels per game pixel */
    bool alpha;   /* the file has transparency; otherwise black is see-through on moving things */
} art[SPRITE_SETS];
static int art_sets, art_zoom; /* sets loaded, the largest zoom */
static unsigned art_generation;

static bool absolute_path(const char *p)
{
    return p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':');
}

/* Load set `set` from `path` if it exists. Returns 1 if loaded. */
static int load_set(int set, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    char err[300];
    int w, h, alpha;
    uint32_t *px = png_read(path, &w, &h, &alpha, err, sizeof err);
    if (!px) {
        fprintf(stderr, "ptomb: %s\n", err);
        return 0;
    }
    const int zoom = w / STRIP_W;
    if (zoom < 1 || zoom > MAX_ZOOM || w != STRIP_W * zoom || h != STRIP_H * zoom) {
        fprintf(stderr, "ptomb: %s is %dx%d; a sprite set is %dx%d times a zoom of 1 to %d\n", path, w, h,
                STRIP_W, STRIP_H, MAX_ZOOM);
        free(px);
        return 0;
    }
    free(art[set].px);
    art[set].px = px;
    art[set].w = w;
    art[set].zoom = zoom;
    art[set].alpha = alpha != 0;
    return 1;
}

int render_load_sprites(const char *dir, const char *base, int volume)
{
    char root[600];
    if (!dir[0])
        return 0;
    snprintf(root, sizeof root, "%s%s", absolute_path(dir) ? "" : base, dir);
    art_sets = art_zoom = 0;
    for (int s = 0; s < SPRITE_SETS; s++) {
        free(art[s].px);
        art[s].px = NULL;
        /* volN/ first, then the folder itself; setNN.png or extract_gfx.py's setNN_dsXXXX.png */
        char names[4][700];
        snprintf(names[0], sizeof names[0], "%s/vol%d/set%02d.png", root, volume, s);
        snprintf(names[1], sizeof names[1], "%s/vol%d/set%02d_ds%04x.png", root, volume, s, s * SPRITE_SET_BYTES);
        snprintf(names[2], sizeof names[2], "%s/set%02d.png", root, s);
        snprintf(names[3], sizeof names[3], "%s/set%02d_ds%04x.png", root, s, s * SPRITE_SET_BYTES);
        for (int i = 0; i < 4; i++)
            if (load_set(s, names[i])) {
                art_sets++;
                if (art[s].zoom > art_zoom)
                    art_zoom = art[s].zoom;
                break;
            }
    }
    art_generation++;
    if (art_sets)
        printf("custom sprites: %d of %d sets from %s\n", art_sets, SPRITE_SETS, root);
    return art_sets;
}

bool render_custom(void)
{
    return settings.sprites && art_sets > 0;
}

/* The custom picture for sprite tag `tag`, or NULL. *x0, *y0: its frame's top left. */
static const uint32_t *art_frame(int tag, int *stride, int *zoom, int *x0, int *y0)
{
    if (!render_custom() || tag < 1 || tag > SPRITE_SETS * SPRITE_FRAMES)
        return NULL;
    const int set = (tag - 1) / SPRITE_FRAMES, frame = (tag - 1) % SPRITE_FRAMES;
    if (!art[set].px)
        return NULL;
    *stride = art[set].w;
    *zoom = art[set].zoom;
    *x0 = art[set].zoom * (1 + frame * (CELL + 1));
    *y0 = art[set].zoom;
    return art[set].px;
}

/* ------------------------------------------------------------------------------------------ */
/* Colours                                                                                    */

static uint32_t cga_argb(int c)
{
    const rgb_t p = cga_palette(settings.palette, settings.bright)[c & 3];
    return 0xFF000000u | (uint32_t)p.r << 16 | (uint32_t)p.g << 8 | p.b;
}

unsigned render_style(void)
{
    return art_generation << 3 | (unsigned)render_custom() << 2 | (unsigned)(settings.palette & 1) << 1 |
           (settings.bright ? 1u : 0u);
}

uint32_t render_line_argb(int color)
{
    return cga_argb(color);
}

static int pic_w(const uint8_t *pic)
{
    return pic[2] | pic[3] << 8;
}

static int pic_h(const uint8_t *pic)
{
    return pic[4] | pic[5] << 8;
}

/* Colour of a picture's pixel at column c, row r counted from the TOP; 0 outside. */
static int pic_px(const uint8_t *pic, int c, int r)
{
    const int w = pic_w(pic), h = pic_h(pic);
    if (c < 0 || c >= w || r < 0 || r >= h)
        return 0;
    const int stored = h - 1 - r; /* bottom row first */
    return pic[6 + stored * ((w + 3) / 4) + c / 4] >> (6 - 2 * (c % 4)) & 3;
}

const uint32_t *render_pic_argb(const uint8_t *pic, int *w_out, int *h_out)
{
    static uint32_t out[CELL * MAX_ZOOM * CELL * MAX_ZOOM];
    const int w = pic_w(pic), h = pic_h(pic);
    int stride, zoom, x0, y0;
    const uint32_t *a = w <= CELL && h <= CELL ? art_frame(g3_pic_tag(pic), &stride, &zoom, &x0, &y0) : NULL;
    if (a) {
        const bool alpha = art[(g3_pic_tag(pic) - 1) / SPRITE_FRAMES].alpha;
        for (int y = 0; y < h * zoom; y++)
            for (int x = 0; x < w * zoom; x++) {
                const uint32_t c = a[(y0 + y) * stride + x0 + x];
                const bool clear = alpha ? c >> 24 < 0x80 : (c & 0xFFFFFFu) == 0;
                out[y * w * zoom + x] = clear ? 0 : c | 0xFF000000u;
            }
        *w_out = w * zoom;
        *h_out = h * zoom;
        return out;
    }
    for (int r = 0; r < h && r < CELL; r++)
        for (int c = 0; c < w && c < CELL; c++) {
            const int k = pic_px(pic, c, r);
            out[r * w + c] = k ? cga_argb(k) : 0;
        }
    *w_out = w;
    *h_out = h;
    return out;
}

/* ------------------------------------------------------------------------------------------ */
/* The scene: what moves, taken out of the background and drawn where it's gliding to         */

static render_scene_fn scene_fn;

/* How long a move takes to show: a little more than one timer tick, so that steady movement
 * (one step per tick, arriving up to a game pass late) never stops between steps. */
#define GLIDE (1.15 / TIMER_HZ)
/* A jump bigger than this (a restart, a teleport) isn't animated. */
#define SNAP_PX 12
#define MAX_IDS 64

typedef struct {
    bool live;
    float sx, sy; /* where the glide started */
    int tx, ty;   /* where it's going: where the game last drew it */
    double t0;
} tracker_t;
static tracker_t trk[MAX_IDS];

void render_set_scene(render_scene_fn fn)
{
    scene_fn = fn;
}

void render_scene_reset(void)
{
    for (int i = 0; i < MAX_IDS; i++)
        trk[i].live = false;
}

static float glide_pos(float from, int to, double a)
{
    return (float)(from + (to - from) * a);
}

/* Where object `o` shows at time `now`. */
static void track(const scene_obj_t *o, double now, float *x, float *y)
{
    if (o->id < 0 || o->id >= MAX_IDS) {
        *x = (float)o->x;
        *y = (float)o->y;
        return;
    }
    tracker_t *t = &trk[o->id];
    double a = (now - t->t0) / GLIDE;
    a = a < 0 ? 0 : a > 1 ? 1 : a;
    if (!t->live || abs(o->x - t->tx) > SNAP_PX || abs(o->y - t->ty) > SNAP_PX) {
        t->live = true;
        t->sx = (float)o->x;
        t->sy = (float)o->y;
        t->tx = o->x;
        t->ty = o->y;
        t->t0 = now - GLIDE;
        a = 1;
    } else if (o->x != t->tx || o->y != t->ty) {
        t->sx = glide_pos(t->sx, t->tx, a); /* carry on from where it shows now */
        t->sy = glide_pos(t->sy, t->ty, a);
        t->tx = o->x;
        t->ty = o->y;
        t->t0 = now;
        a = 0;
    }
    *x = glide_pos(t->sx, t->tx, a);
    *y = glide_pos(t->sy, t->ty, a);
}

/* The framebuffer as the background will show it: colour, tag and place in the picture. */
static uint8_t idx[CGA_H][CGA_W], tags[CGA_H][CGA_W];
static uint16_t tpos[CGA_H][CGA_W];

static int at(int x, int y)
{
    return x < 0 || x >= CGA_W || y < 0 || y >= CGA_H ? -1 : idx[y][x];
}

/* Is pixel (c, r) of object `o` (put at x, y) what's on screen there? A sprite's pixels are
 * recognised by their tag and place, black ones included; a line by its colour. */
static bool is_objects(const scene_obj_t *o, int tag, int c, int r, int x, int y)
{
    if (tag != TAG_NONE)
        return tags[y][x] == tag && tpos[y][x] == (uint16_t)(c | r << 8);
    const int k = o->pic ? pic_px(o->pic, c, r) : o->color;
    return k != 0 && idx[y][x] == k;
}

/* If at least 3/4 of `o` is on screen as the game drew it, take it out of the background and
 * return true. Something reported but not there (not drawn yet, drawn over) is left to the
 * background as it is. */
static bool take_out(const scene_obj_t *o)
{
    const int w = o->pic ? pic_w(o->pic) : o->w, h = o->pic ? pic_h(o->pic) : 1;
    const int tag = o->pic ? g3_pic_tag(o->pic) : TAG_NONE;
    int total = 0, found = 0;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            const int x = o->x + c, y = o->y - (h - 1) + r;
            if (x < 0 || x >= CGA_W || y < 0 || y >= CGA_H || (tag == TAG_NONE && !(o->pic ? pic_px(o->pic, c, r) : 1)))
                continue;
            total++;
            found += is_objects(o, tag, c, r, x, y);
        }
    if (total == 0 || found * 4 < total * 3)
        return false;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            const int x = o->x + c, y = o->y - (h - 1) + r;
            if (x >= 0 && x < CGA_W && y >= 0 && y < CGA_H && is_objects(o, tag, c, r, x, y)) {
                idx[y][x] = 0;
                tags[y][x] = TAG_NONE;
                tpos[y][x] = 0;
            }
        }
    return true;
}

static render_out_t out;

static render_item_t *add_item(item_kind_t kind)
{
    if (out.n_items == RENDER_MAX_ITEMS)
        return NULL;
    render_item_t *it = &out.items[out.n_items++];
    memset(it, 0, sizeof *it);
    it->kind = kind;
    return it;
}

/* Things that hang from a pole or stand on one (the vertical hazards, the elevator) leave their
 * edge row behind as the pole when they move. Shown lagging behind the game's position, they'd
 * come away from it: stretch the pole's last row over the gap, under the picture. */
static void bridge_poles(const scene_obj_t *o, float shown_y)
{
    const int w = pic_w(o->pic), h = pic_h(o->pic);
    const float gap = shown_y - (float)o->y; /* > 0: shown lower than drawn */
    if (fabsf(gap) < 0.01f)
        return;
    const int edge_r = gap > 0 ? 0 : h - 1;               /* the picture's row that faces the pole */
    const int pole_y = gap > 0 ? o->y - h : o->y + 1;     /* the pole's end, next to that row */
    const float top = gap > 0 ? (float)(o->y - h + 1) : shown_y + 1;
    for (int c = 0; c < w;) {
        const int x = o->x + c;
        const int k = pic_px(o->pic, c, edge_r);
        if (!k || at(x, pole_y) != k) {
            c++;
            continue;
        }
        int run = 1;
        while (c + run < w && pic_px(o->pic, c + run, edge_r) && at(x + run, pole_y) == pic_px(o->pic, c + run, edge_r))
            run++;
        render_item_t *it = add_item(ITEM_STRIP);
        if (it) {
            it->sx = x;
            it->sy = pole_y;
            it->sw = run;
            it->x = (float)x;
            it->y = top;
            it->w = (float)run;
            it->h = fabsf(gap);
        }
        c += run;
    }
}

/* The background at `scale`, from idx / tags / tpos. */
static void build_background(uint32_t *bg, int scale)
{
    const int W = CGA_W * scale;
    uint32_t lut[4];
    for (int c = 0; c < 4; c++)
        lut[c] = cga_argb(c);
    for (int y = 0; y < CGA_H; y++)
        for (int x = 0; x < CGA_W; x++) {
            uint32_t *o = bg + (size_t)y * scale * W + (size_t)x * scale;
            int stride, zoom, x0, y0;
            const uint32_t *a = art_frame(tags[y][x], &stride, &zoom, &x0, &y0);
            const int col = tpos[y][x] & 0xFF, row = tpos[y][x] >> 8;
            if (a && col < CELL && row < CELL) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++) {
                        const uint32_t c = a[(y0 + row * zoom + sy * zoom / scale) * stride + x0 + col * zoom + sx * zoom / scale];
                        o[sy * W + sx] = c >> 24 < 0x80 ? 0xFF000000u : c | 0xFF000000u;
                    }
            } else {
                const uint32_t c = lut[idx[y][x]];
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        o[sy * W + sx] = c;
            }
        }
}

const render_out_t *render_frame(double now)
{
    static uint32_t *bg;
    static int bg_scale;
    static uint8_t prev_idx[CGA_H][CGA_W], prev_tags[CGA_H][CGA_W];
    static uint16_t prev_pos[CGA_H][CGA_W];
    static unsigned prev_style = ~0u;

    for (int y = 0; y < CGA_H; y++) {
        for (int x = 0; x < CGA_W; x++)
            idx[y][x] = (uint8_t)cga_get(x, y);
        memcpy(tags[y], cga_tag + y * CGA_W, CGA_W);
        memcpy(tpos[y], cga_tagpos + y * CGA_W, CGA_W * sizeof(uint16_t));
    }

    out.n_items = 0;
    bool seen[MAX_IDS] = {false};
    if (settings.smooth && scene_fn) {
        scene_obj_t objs[RENDER_MAX_ITEMS / 2];
        const int n = scene_fn(objs, RENDER_MAX_ITEMS / 2);
        for (int i = 0; i < n; i++) {
            const scene_obj_t *o = &objs[i];
            if (!take_out(o))
                continue;
            float x, y;
            track(o, now, &x, &y);
            if (o->id >= 0 && o->id < MAX_IDS)
                seen[o->id] = true;
            if (o->pic) {
                bridge_poles(o, y);
                render_item_t *it = add_item(ITEM_PIC);
                if (it) {
                    it->pic = o->pic;
                    it->w = (float)pic_w(o->pic);
                    it->h = (float)pic_h(o->pic);
                    it->x = x;
                    it->y = y - it->h + 1;
                }
            } else {
                render_item_t *it = add_item(ITEM_LINE);
                if (it) {
                    it->color = o->color;
                    it->x = x;
                    it->y = y;
                    it->w = (float)o->w;
                    it->h = 1;
                }
            }
        }
    }
    for (int i = 0; i < MAX_IDS; i++)
        if (!seen[i])
            trk[i].live = false;

    /* The background only changes when the screen or the colours do. */
    const int scale = render_custom() ? art_zoom : 1;
    const unsigned style = render_style();
    out.bg_changed = !bg || scale != bg_scale || style != prev_style || memcmp(idx, prev_idx, sizeof idx) ||
                     memcmp(tags, prev_tags, sizeof tags) || memcmp(tpos, prev_pos, sizeof tpos);
    if (out.bg_changed) {
        if (scale != bg_scale || !bg) {
            free(bg);
            bg = malloc((size_t)CGA_W * scale * CGA_H * scale * sizeof *bg);
            bg_scale = scale;
        }
        if (bg)
            build_background(bg, scale);
        memcpy(prev_idx, idx, sizeof idx);
        memcpy(prev_tags, tags, sizeof tags);
        memcpy(prev_pos, tpos, sizeof tpos);
        prev_style = style;
    }
    out.bg = bg;
    out.scale = bg_scale;
    return &out;
}

/* ------------------------------------------------------------------------------------------ */
/* Software composition, for screenshots                                                      */

uint32_t *render_compose(double now, int *w_out, int *h_out)
{
    const render_out_t *f = render_frame(now);
    if (!f->bg)
        return NULL;
    const int S = f->scale, s = S < 2 ? 2 : S, W = CGA_W * s, H = CGA_H * s;
    uint32_t *img = malloc((size_t)W * H * sizeof *img);
    if (!img)
        return NULL;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            img[y * W + x] = f->bg[(size_t)(y * S / s) * CGA_W * S + x * S / s];
    for (int i = 0; i < f->n_items; i++) {
        const render_item_t *it = &f->items[i];
        const int x0 = (int)lroundf(it->x * (float)s), y0 = (int)lroundf(it->y * (float)s);
        const int dw = (int)lroundf(it->w * (float)s), dh = (int)lroundf(it->h * (float)s);
        int pw = 1, ph = 1;
        const uint32_t *p = it->kind == ITEM_PIC ? render_pic_argb(it->pic, &pw, &ph) : NULL;
        for (int y = 0; y < dh; y++)
            for (int x = 0; x < dw; x++) {
                const int ox = x0 + x, oy = y0 + y;
                if (ox < 0 || ox >= W || oy < 0 || oy >= H)
                    continue;
                uint32_t c;
                if (it->kind == ITEM_PIC)
                    c = p[(y * ph / dh) * pw + x * pw / dw];
                else if (it->kind == ITEM_LINE)
                    c = render_line_argb(it->color);
                else
                    c = f->bg[(size_t)(it->sy * S + S - 1) * CGA_W * S + it->sx * S + x * it->sw * S / dw];
                if (c >> 24)
                    img[oy * W + ox] = c;
            }
    }
    *w_out = W;
    *h_out = H;
    return img;
}
