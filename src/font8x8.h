/* The port's own 8x8 font for the lower half of the character set (stands in for the BIOS ROM
 * font). Rows top first, MSB = leftmost pixel. */
#ifndef PTOMB_FONT8X8_H
#define PTOMB_FONT8X8_H

#include <stdint.h>

const uint8_t *font8x8_glyph(int ch); /* ch 00h-7Fh */

#endif
