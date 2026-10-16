#include "lzexe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}

int lzexe_is_packed(const uint8_t *exe, size_t len)
{
    return len >= 0x20 && exe[0] == 'M' && exe[1] == 'Z' &&
           (memcmp(exe + 0x1C, "LZ91", 4) == 0 || memcmp(exe + 0x1C, "LZ09", 4) == 0);
}

typedef struct {
    const uint8_t *data;
    size_t pos, end;
    unsigned buf;
    int count;
    int overrun;
} bits_t;

static unsigned next_byte(bits_t *b)
{
    if (b->pos >= b->end) {
        b->overrun = 1;
        return 0;
    }
    return b->data[b->pos++];
}

static unsigned next_word(bits_t *b)
{
    unsigned lo = next_byte(b);
    return lo | (next_byte(b) << 8);
}

/* Control bits: 16-bit little-endian words read LSB first, refilled as soon as one runs out. */
static unsigned next_bit(bits_t *b)
{
    unsigned bit = b->buf & 1;
    if (--b->count == 0) {
        b->buf = next_word(b);
        b->count = 16;
    } else {
        b->buf >>= 1;
    }
    return bit;
}

int lzexe_unpack(const uint8_t *exe, size_t len, uint8_t **image, size_t *image_len,
                 uint32_t *entry, char *err, size_t errlen)
{
    if (!lzexe_is_packed(exe, len)) {
        snprintf(err, errlen, "not an LZEXE-packed executable");
        return -1;
    }
    const size_t header = rd16(exe + 8) * 16u;
    const size_t loader = header + rd16(exe + 0x16) * 16u; /* CS of the decompressor */
    if (loader + 14 > len) {
        snprintf(err, errlen, "LZEXE loader is outside the file");
        return -1;
    }
    const unsigned ip = rd16(exe + loader), cs = rd16(exe + loader + 2);

    size_t cap = 256 * 1024, n = 0;
    uint8_t *out = malloc(cap);
    if (!out) {
        snprintf(err, errlen, "out of memory");
        return -1;
    }

    bits_t b = { exe, header, loader, 0, 16, 0 };
    b.buf = next_word(&b);
    for (;;) {
        unsigned length;
        int span;
        if (next_bit(&b)) {
            length = 1;
            span = 0; /* literal */
        } else if (!next_bit(&b)) {
            length = ((next_bit(&b) << 1) | next_bit(&b)) + 2;
            span = (int)(next_byte(&b) | 0xFF00) - 0x10000;
        } else {
            unsigned lo = next_byte(&b), hi = next_byte(&b);
            span = (int)(lo | ((hi & ~7u) << 5) | 0xE000) - 0x10000;
            length = (hi & 7) + 2;
            if (length == 2) {
                length = next_byte(&b);
                if (length == 0)
                    break;      /* end of stream */
                if (length == 1)
                    continue;   /* segment change marker */
                length++;
            }
        }
        if (b.overrun) {
            free(out);
            snprintf(err, errlen, "compressed data ran into the loader");
            return -1;
        }
        if (n + length > cap) {
            cap *= 2;
            if (cap > 4 * 1024 * 1024) {
                free(out);
                snprintf(err, errlen, "decompressed image is implausibly large");
                return -1;
            }
            uint8_t *grown = realloc(out, cap);
            if (!grown) {
                free(out);
                snprintf(err, errlen, "out of memory");
                return -1;
            }
            out = grown;
        }
        if (span == 0) {
            out[n++] = (uint8_t)next_byte(&b);
        } else {
            if ((size_t)-span > n) {
                free(out);
                snprintf(err, errlen, "back-reference before start of image");
                return -1;
            }
            for (unsigned i = 0; i < length; i++, n++)
                out[n] = out[n + span];
        }
    }

    if (b.overrun) {
        free(out);
        snprintf(err, errlen, "compressed data ran into the loader");
        return -1;
    }
    *image = out;
    *image_len = n;
    *entry = cs * 16u + ip;
    return 0;
}
