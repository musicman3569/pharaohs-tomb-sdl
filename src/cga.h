/* CGA 320x200 4-colour framebuffer.
 *
 * Pixels are stored packed, 2 bits each, 4 to a byte, leftmost pixel in the top bits, 80 bytes
 * per row. That's the same layout as CGA mode 4 and as Graph3 GetPic buffers, but linear rather
 * than split into even/odd banks; nothing in the game depends on the banking. */
#ifndef PTOMB_CGA_H
#define PTOMB_CGA_H

#include <stdint.h>

#define CGA_W 320
#define CGA_H 200
#define CGA_STRIDE (CGA_W / 4)

typedef struct { uint8_t r, g, b; } rgb_t;

extern uint8_t cga_vram[CGA_H * CGA_STRIDE];

/* Raw pixel access; callers must keep x, y on screen. */
static inline int cga_get(int x, int y)
{
    return (cga_vram[y * CGA_STRIDE + (x >> 2)] >> (6 - 2 * (x & 3))) & 3;
}

static inline void cga_set(int x, int y, int c)
{
    uint8_t *p = &cga_vram[y * CGA_STRIDE + (x >> 2)];
    int shift = 6 - 2 * (x & 3);
    *p = (uint8_t)((*p & ~(3 << shift)) | ((c & 3) << shift));
}

void cga_clear(void);

/* palette 0 = green/red/brown, 1 = cyan/magenta/white (the game's own); bright = intensity bit. */
const rgb_t *cga_palette(int palette, int bright);

/* Expand the framebuffer to 0xAARRGGBB pixels; pitch is in pixels. */
void cga_to_argb(uint32_t *out, int pitch, const rgb_t *pal);

/* Write the framebuffer as an RGB PNG, each pixel scaled up `scale` times. Returns 0 on success. */
int cga_write_png(const char *path, const rgb_t *pal, int scale);

#endif
