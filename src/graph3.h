/* Turbo Pascal 3 Graph3 unit, as the game uses it, on top of the CGA framebuffer.
 *
 * Coordinates are screen pixels; the window is the full screen (0..319, 0..199) and
 * everything clips to it.
 *
 * Picture buffers use Graph3's GetPic format, which is also how the game stores its sprites:
 *   uint16 mode (2 = 320x200 4-colour), uint16 width, uint16 height,
 *   then `height` rows of 2-bit pixels, BOTTOM row first, ceil(width/4) bytes per row.
 * (Bottom-first matches PutPic's Y being the bottom row; the score pop-up sprites read
 * "10K 15K 20K", as the bonus code awards, only this way up.) */
#ifndef PTOMB_GRAPH3_H
#define PTOMB_GRAPH3_H

#include <stddef.h>
#include <stdint.h>

/* GraphColorMode: 320x200 4-colour mode, screen cleared. (The palette is a display setting.) */
void g3_graph_color_mode(void);

/* PutPic(Buffer, X, Y). Note that Y is the BOTTOM row of the picture, not the top: the
 * picture covers y = Y-height+1 .. Y. Pixels are copied opaquely, colour 0 included. */
void g3_put_pic(const uint8_t *pic, int x, int y);

/* GetPic(Buffer, X1, Y1, X2, Y2): copy a screen rectangle into `pic`, which must hold
 * g3_pic_size(w, h) bytes. Off-screen pixels read as 0. */
void g3_get_pic(uint8_t *pic, int x1, int y1, int x2, int y2);
size_t g3_pic_size(int w, int h);

/* GetDotColor(X, Y): colour 0..3, or -1 off screen. The game's collision code relies on this. */
int g3_get_dot_color(int x, int y);

void g3_plot(int x, int y, int color);
void g3_draw(int x1, int y1, int x2, int y2, int color);

#endif
