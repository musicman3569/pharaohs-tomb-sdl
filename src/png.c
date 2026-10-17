#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------ */
/* Inflate (RFC 1951): stored, fixed and dynamic Huffman blocks                               */

typedef struct {
    const uint8_t *in;
    size_t in_len, in_pos;
    unsigned bitbuf, bitcnt;
    uint8_t *out;
    size_t out_len, out_pos;
    int error;
} inflate_t;

static int bits(inflate_t *s, int n)
{
    unsigned v = s->bitbuf;
    while (s->bitcnt < (unsigned)n) {
        if (s->in_pos >= s->in_len) {
            s->error = 1;
            return 0;
        }
        v |= (unsigned)s->in[s->in_pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> n;
    s->bitcnt -= (unsigned)n;
    return (int)(v & ((1u << n) - 1));
}

typedef struct {
    short count[16];  /* codes of each length */
    short symbol[320];
} huffman_t;

static int decode(inflate_t *s, const huffman_t *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= bits(s, 1);
        if (s->error)
            return -1;
        const int count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    s->error = 1;
    return -1;
}

static int construct(huffman_t *h, const short *length, int n)
{
    short offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++)
        h->count[length[i]]++;
    if (h->count[0] == n)
        return 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return -1;
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++)
        offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int i = 0; i < n; i++)
        if (length[i])
            h->symbol[offs[length[i]]++] = (short)i;
    return left;
}

static void put(inflate_t *s, uint8_t b)
{
    if (s->out_pos < s->out_len)
        s->out[s->out_pos++] = b;
    else
        s->error = 1;
}

static int codes(inflate_t *s, const huffman_t *lencode, const huffman_t *distcode)
{
    static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                    67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
                                   12, 12, 13, 13};
    for (;;) {
        int sym = decode(s, lencode);
        if (sym < 0)
            return -1;
        if (sym < 256) {
            put(s, (uint8_t)sym);
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29)
                return -1;
            const int len = lbase[sym] + bits(s, lext[sym]);
            const int dsym = decode(s, distcode);
            if (dsym < 0 || dsym >= 30)
                return -1;
            const size_t dist = (size_t)(dbase[dsym] + bits(s, dext[dsym]));
            if (dist > s->out_pos)
                return -1;
            for (int i = 0; i < len; i++)
                put(s, s->out[s->out_pos - dist]);
        }
        if (s->error)
            return -1;
    }
}

static int inflate_block_stored(inflate_t *s)
{
    s->bitbuf = s->bitcnt = 0; /* to a byte boundary */
    if (s->in_pos + 4 > s->in_len)
        return -1;
    const unsigned len = s->in[s->in_pos] | s->in[s->in_pos + 1] << 8;
    const unsigned nlen = s->in[s->in_pos + 2] | s->in[s->in_pos + 3] << 8;
    s->in_pos += 4;
    if (len != (~nlen & 0xFFFF) || s->in_pos + len > s->in_len)
        return -1;
    for (unsigned i = 0; i < len; i++)
        put(s, s->in[s->in_pos++]);
    return s->error ? -1 : 0;
}

static int inflate_block_fixed(inflate_t *s)
{
    static huffman_t lencode, distcode;
    static int built;
    if (!built) {
        short lengths[288];
        int i = 0;
        for (; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < 288; i++) lengths[i] = 8;
        construct(&lencode, lengths, 288);
        for (i = 0; i < 30; i++) lengths[i] = 5;
        construct(&distcode, lengths, 30);
        built = 1;
    }
    return codes(s, &lencode, &distcode);
}

static int inflate_block_dynamic(inflate_t *s)
{
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[320];
    huffman_t lencode, distcode;
    const int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->error || nlen > 286 || ndist > 30)
        return -1;
    int i = 0;
    for (; i < ncode; i++)
        lengths[order[i]] = (short)bits(s, 3);
    for (; i < 19; i++)
        lengths[order[i]] = 0;
    if (construct(&lencode, lengths, 19) != 0)
        return -1;
    for (i = 0; i < nlen + ndist;) {
        int sym = decode(s, &lencode);
        if (sym < 0)
            return -1;
        if (sym < 16) {
            lengths[i++] = (short)sym;
        } else {
            short len = 0;
            int rep;
            if (sym == 16) {
                if (i == 0)
                    return -1;
                len = lengths[i - 1];
                rep = 3 + bits(s, 2);
            } else if (sym == 17) {
                rep = 3 + bits(s, 3);
            } else {
                rep = 11 + bits(s, 7);
            }
            if (i + rep > nlen + ndist)
                return -1;
            while (rep--)
                lengths[i++] = len;
        }
    }
    if (lengths[256] == 0)
        return -1;
    const int err = construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1))
        return -1;
    const int derr = construct(&distcode, lengths + nlen, ndist);
    if (derr < 0 || (derr > 0 && ndist - distcode.count[0] != 1))
        return -1;
    return codes(s, &lencode, &distcode);
}

/* Inflate a zlib stream (2-byte header, no dictionary) into out[out_len]; returns the bytes made. */
static long zlib_inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len)
{
    if (in_len < 2 || (in[0] & 0x0F) != 8 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20))
        return -1;
    inflate_t s = {in, in_len, 2, 0, 0, out, out_len, 0, 0};
    int last;
    do {
        last = bits(&s, 1);
        const int type = bits(&s, 2);
        if (s.error)
            return -1;
        const int rc = type == 0 ? inflate_block_stored(&s) : type == 1 ? inflate_block_fixed(&s)
                     : type == 2 ? inflate_block_dynamic(&s) : -1;
        if (rc != 0 || s.error)
            return -1;
    } while (!last);
    return (long)s.out_pos;
}

/* ------------------------------------------------------------------------------------------ */
/* PNG                                                                                        */

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int paeth(int a, int b, int c)
{
    const int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

uint32_t *png_read(const char *path, int *w_out, int *h_out, int *has_alpha, char *err, size_t errlen)
{
    uint8_t *file = NULL, *idat = NULL, *raw = NULL;
    uint32_t *px = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errlen, "can't open %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    const long flen = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (flen > 0 && flen < (64L << 20))
        file = malloc((size_t)flen);
    const size_t len = file ? fread(file, 1, (size_t)flen, f) : 0;
    fclose(f);
    if (!file || len != (size_t)flen || len < 8 || memcmp(file, "\x89PNG\r\n\x1a\n", 8) != 0) {
        snprintf(err, errlen, "%s: not a PNG file", path);
        goto fail;
    }

    uint32_t w = 0, h = 0;
    int depth = 0, ctype = -1, interlace = 0, npal = 0;
    uint32_t pal[256];
    int trns_grey = -1, trns_r = -1, trns_g = -1, trns_b = -1;
    *has_alpha = 0;
    size_t idat_len = 0, pos = 8;
    for (int i = 0; i < 256; i++)
        pal[i] = 0xFF000000u;
    while (pos + 12 <= len) {
        const uint32_t clen = be32(file + pos);
        const uint8_t *type = file + pos + 4, *data = file + pos + 8;
        if (clen > len - pos - 12)
            break;
        if (!memcmp(type, "IHDR", 4) && clen >= 13) {
            w = be32(data);
            h = be32(data + 4);
            depth = data[8];
            ctype = data[9];
            interlace = data[12];
        } else if (!memcmp(type, "PLTE", 4)) {
            npal = (int)(clen / 3 > 256 ? 256 : clen / 3);
            for (int i = 0; i < npal; i++)
                pal[i] = 0xFF000000u | (uint32_t)data[i * 3] << 16 | (uint32_t)data[i * 3 + 1] << 8 | data[i * 3 + 2];
        } else if (!memcmp(type, "tRNS", 4)) {
            *has_alpha = 1;
            if (ctype == 3)
                for (uint32_t i = 0; i < clen && i < 256; i++)
                    pal[i] = (pal[i] & 0xFFFFFFu) | (uint32_t)data[i] << 24;
            else if (ctype == 0 && clen >= 2)
                trns_grey = data[0] << 8 | data[1];
            else if (ctype == 2 && clen >= 6) {
                trns_r = data[0] << 8 | data[1];
                trns_g = data[2] << 8 | data[3];
                trns_b = data[4] << 8 | data[5];
            }
        } else if (!memcmp(type, "IDAT", 4)) {
            uint8_t *grown = realloc(idat, idat_len + clen + 1);
            if (!grown)
                goto fail;
            idat = grown;
            memcpy(idat + idat_len, data, clen);
            idat_len += clen;
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        pos += 12 + clen;
    }

    static const int channels_of[7] = {1, 0, 3, 1, 2, 0, 4};
    if (w == 0 || h == 0 || w > 8192 || h > 8192 || ctype < 0 || ctype > 6 || !channels_of[ctype] ||
        (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16)) {
        snprintf(err, errlen, "%s: unsupported PNG (size %ux%u, colour type %d, depth %d)", path, w, h, ctype, depth);
        goto fail;
    }
    if (interlace) {
        snprintf(err, errlen, "%s: interlaced PNGs aren't supported; save it without interlacing", path);
        goto fail;
    }
    if (ctype == 4 || ctype == 6)
        *has_alpha = 1;
    const int channels = channels_of[ctype];
    const size_t bpp = (size_t)((channels * depth + 7) / 8); /* bytes per pixel, at least 1 */
    const size_t stride = ((size_t)w * (size_t)channels * (size_t)depth + 7) / 8;
    const size_t raw_len = (stride + 1) * h;
    raw = malloc(raw_len);
    if (!raw || !idat || zlib_inflate(idat, idat_len, raw, raw_len) != (long)raw_len) {
        snprintf(err, errlen, "%s: damaged PNG data", path);
        goto fail;
    }

    /* Undo the row filters in place. */
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = raw + y * (stride + 1);
        const uint8_t *prev = y ? row - (stride + 1) + 1 : NULL;
        const int filter = row[0];
        uint8_t *p = row + 1;
        for (size_t x = 0; x < stride; x++) {
            const int a = x >= bpp ? p[x - bpp] : 0, b = prev ? prev[x] : 0, c = prev && x >= bpp ? prev[x - bpp] : 0;
            switch (filter) {
            case 0: break;
            case 1: p[x] = (uint8_t)(p[x] + a); break;
            case 2: p[x] = (uint8_t)(p[x] + b); break;
            case 3: p[x] = (uint8_t)(p[x] + (a + b) / 2); break;
            case 4: p[x] = (uint8_t)(p[x] + paeth(a, b, c)); break;
            default:
                snprintf(err, errlen, "%s: damaged PNG (filter %d)", path, filter);
                goto fail;
            }
        }
    }

    px = malloc((size_t)w * h * sizeof *px);
    if (!px)
        goto fail;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *p = raw + y * (stride + 1) + 1;
        for (uint32_t x = 0; x < w; x++) {
            int sample[4];
            for (int ch = 0; ch < channels; ch++) {
                const size_t bit = ((size_t)x * (size_t)channels + (size_t)ch) * (size_t)depth;
                if (depth == 16)
                    sample[ch] = p[bit / 8] << 8 | p[bit / 8 + 1];
                else if (depth == 8)
                    sample[ch] = p[bit / 8];
                else
                    sample[ch] = p[bit / 8] >> (8 - depth - (int)(bit % 8)) & ((1 << depth) - 1);
            }
            /* to 8 bits per channel */
            const int max = (1 << depth) - 1;
            int v[4];
            for (int ch = 0; ch < channels; ch++)
                v[ch] = ctype == 3 ? sample[ch] : sample[ch] * 255 / max;
            uint32_t c;
            switch (ctype) {
            case 0:
                c = 0xFF000000u | (uint32_t)v[0] * 0x010101u;
                if (sample[0] == trns_grey)
                    c &= 0xFFFFFFu;
                break;
            case 2:
                c = 0xFF000000u | (uint32_t)v[0] << 16 | (uint32_t)v[1] << 8 | (uint32_t)v[2];
                if (sample[0] == trns_r && sample[1] == trns_g && sample[2] == trns_b)
                    c &= 0xFFFFFFu;
                break;
            case 3:
                c = v[0] < 256 ? pal[v[0]] : 0xFF000000u;
                break;
            case 4:
                c = (uint32_t)v[1] << 24 | (uint32_t)v[0] * 0x010101u;
                break;
            default: /* 6 */
                c = (uint32_t)v[3] << 24 | (uint32_t)v[0] << 16 | (uint32_t)v[1] << 8 | (uint32_t)v[2];
                break;
            }
            px[y * w + x] = c;
        }
    }
    free(raw);
    free(idat);
    free(file);
    *w_out = (int)w;
    *h_out = (int)h;
    return px;

fail:
    free(px);
    free(raw);
    free(idat);
    free(file);
    return NULL;
}
