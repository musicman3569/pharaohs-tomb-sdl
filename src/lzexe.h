/* LZEXE 0.90 / 0.91 decompression, the C version of tools/unlzexe.py.
 *
 * Only the load image and the original entry point are recovered. The port never runs the DOS
 * code, so relocations aren't needed: segment values stay relative to the image, which is what
 * the asset scanners expect. */
#ifndef PTOMB_LZEXE_H
#define PTOMB_LZEXE_H

#include <stddef.h>
#include <stdint.h>

/* Returns 1 if `exe` is LZEXE-packed. */
int lzexe_is_packed(const uint8_t *exe, size_t len);

/* Decompress. On success returns 0, sets *image (malloc'd) / *image_len and the original
 * entry point as a linear offset into the image. On failure returns -1 with a message in err. */
int lzexe_unpack(const uint8_t *exe, size_t len, uint8_t **image, size_t *image_len,
                 uint32_t *entry, char *err, size_t errlen);

#endif
