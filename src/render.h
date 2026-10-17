/* The display layer (port enhancements display.smooth and display.sprites).
 *
 * The game draws into the 2 bpp framebuffer (cga.h), and its collision code reads it back, so
 * that buffer stays exactly what the original would have on screen. Everything here only
 * changes what the window shows, built from that buffer at every frame:
 *
 *   smooth    Things move on the 18.2 Hz timer, 1 to 4 px at a time. The game reports where
 *             each moving thing was last drawn (a scene), the background is the framebuffer
 *             with those pictures taken out, and they're drawn on top of it at positions
 *             interpolated between their last two places, at the monitor's refresh rate.
 *   sprites   Custom art: PNGs laid out like the ones tools/extract_gfx.py writes (one per
 *             sprite set, 4 frames in 24x24 cells with a 1-px grid, any whole zoom). Every
 *             framebuffer pixel knows which sprite drew it and where in it (cga_tag /
 *             cga_tagpos), so it's shown from that sprite's custom picture instead, at the
 *             art's resolution. Sets without a file keep the CGA colours.
 *
 * The scene is a list of what moves (render_scene_fn), from game.c. With smooth off, or no
 * scene, the window shows the framebuffer as it is. */
#ifndef PTOMB_RENDER_H
#define PTOMB_RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One moving thing, as last drawn into the framebuffer. */
typedef struct {
    int id;             /* stable while it exists: 0 the player, 1..10 enemies, 11.. shots */
    const uint8_t *pic; /* a picture put at x, y (y = its bottom row, as PutPic), or NULL for */
    int x, y;
    int w, color;       /* ...a horizontal line of w pixels in `color` starting at x, y */
} scene_obj_t;

/* Fills `out` with up to `max` objects in drawing order (last on top); returns how many.
 * Returns 0 whenever the screen isn't showing a level (menus, the level title, game over). */
typedef int (*render_scene_fn)(scene_obj_t *out, int max);
void render_set_scene(render_scene_fn fn);

/* Forget where things were, so the next positions are taken as they are (a level started). */
void render_scene_reset(void);

/* Load custom sprites from `dir` (relative to `base`, the folder of ptomb.exe, unless absolute),
 * preferring its vol<volume> subfolder. Returns how many sets were loaded; problems with single
 * files are printed on stderr. */
int render_load_sprites(const char *dir, const char *base, int volume);

/* Are custom sprites loaded and switched on (display.sprites)? */
bool render_custom(void);

/* What to draw over the background, in order. Positions are in 320x200 screen pixels. */
typedef enum { ITEM_PIC, ITEM_LINE, ITEM_STRIP } item_kind_t;
typedef struct {
    item_kind_t kind;
    const uint8_t *pic; /* ITEM_PIC */
    int color;          /* ITEM_LINE: CGA colour */
    int sx, sy, sw;     /* ITEM_STRIP: background pixels sx..sx+sw-1 of row sy, stretched over */
    float x, y, w, h;   /* the rectangle drawn */
} render_item_t;

#define RENDER_MAX_ITEMS 64

typedef struct {
    const uint32_t *bg; /* the background, 0xAARRGGBB */
    int scale;          /* bg is 320 * scale x 200 * scale */
    bool bg_changed;    /* since the last render_frame */
    render_item_t items[RENDER_MAX_ITEMS];
    int n_items;
} render_out_t;

/* Build one frame at time `now` (seconds). */
const render_out_t *render_frame(double now);

/* An ITEM_PIC's picture in the current colours: *w x *h pixels (its custom art's resolution, or
 * the picture's own), transparent where it shows the background. Valid until the next call. */
const uint32_t *render_pic_argb(const uint8_t *pic, int *w, int *h);

/* A line's colour (spear, darts). */
uint32_t render_line_argb(int color);

/* Changes whenever the colours do (palette, intensity, custom sprites), so cached pictures can
 * be rebuilt. */
unsigned render_style(void);

/* The frame as the window shows it, for screenshots: a malloc'd *w x *h image. */
uint32_t *render_compose(double now, int *w, int *h);

#endif
