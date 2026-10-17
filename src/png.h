/* A small PNG reader for custom sprite art (render.c): non-interlaced PNGs of any colour type,
 * bit depths 1-16, with their own inflate (the port has no zlib). */
#ifndef PTOMB_PNG_H
#define PTOMB_PNG_H

#include <stddef.h>
#include <stdint.h>

/* Read a PNG as w x h pixels 0xAARRGGBB, top row first; *has_alpha says whether the file
 * carries transparency (an alpha channel or a tRNS chunk). Free the result with free().
 * Returns NULL with a message in err. */
uint32_t *png_read(const char *path, int *w, int *h, int *has_alpha, char *err, size_t errlen);

#endif
