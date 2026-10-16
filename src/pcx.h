/* The title picture (PTOMBn.MNU, a PCX file) and how it's put on screen.
 *
 * The game loads it once with the Genus PCX Toolkit into an off-screen buffer (LoadPcxScreen,
 * 1000:027E) and shows it with a random transition from the Genus PCX Effects library
 * (1000:04D1 -> 1000:048D -> 20FF:01FB) every time the title or main menu is drawn. */
#ifndef PTOMB_PCX_H
#define PTOMB_PCX_H

#include <stddef.h>
#include <stdint.h>

#include "cga.h"

typedef struct {
    uint8_t pixels[CGA_H * CGA_STRIDE]; /* same packed 2 bpp layout as the framebuffer */
} pcx_image_t;

/* Decode a 320x200 2 bpp RLE PCX file. Returns 0, or -1 with a message in err. */
int pcx_load(const char *path, pcx_image_t *img, char *err, size_t errlen);

/* 1000:04D1: pick one of the game's 18 effect settings with Random(18) and show `img`. */
void pcx_show_random_effect(const pcx_image_t *img);

/* 1000:048D ShowPcxEffect(variant, delay, type), approximated: the Genus effect routines aren't
 * ported. `variant` is treated as a direction mask (1 left-to-right, 2 right-to-left, 4 top-down,
 * 8 bottom-up; two bits = diagonal), `type` picks the grain, `delay` the speed. */
void pcx_show_effect(const pcx_image_t *img, int variant, int delay, int type);

#endif
