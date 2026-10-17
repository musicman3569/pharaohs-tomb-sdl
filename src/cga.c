#include "cga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t cga_vram[CGA_H * CGA_STRIDE];
uint8_t cga_tag[CGA_H * CGA_W];
uint16_t cga_tagpos[CGA_H * CGA_W];
uint8_t cga_cur_tag;
uint16_t cga_cur_pos;

static const rgb_t palettes[2][2][4] = {
    { /* palette 0 */
        { {0, 0, 0}, {0x00, 0xAA, 0x00}, {0xAA, 0x00, 0x00}, {0xAA, 0x55, 0x00} },
        { {0, 0, 0}, {0x55, 0xFF, 0x55}, {0xFF, 0x55, 0x55}, {0xFF, 0xFF, 0x55} },
    },
    { /* palette 1: what the game selects (INT 10h AH=0Bh BX=0100h, intensity off) */
        { {0, 0, 0}, {0x00, 0xAA, 0xAA}, {0xAA, 0x00, 0xAA}, {0xAA, 0xAA, 0xAA} },
        { {0, 0, 0}, {0x55, 0xFF, 0xFF}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0xFF} },
    },
};

void cga_clear(void)
{
    memset(cga_vram, 0, sizeof cga_vram);
    memset(cga_tag, TAG_NONE, sizeof cga_tag);
}

const rgb_t *cga_palette(int palette, int bright)
{
    return palettes[palette & 1][bright ? 1 : 0];
}

void cga_to_argb(uint32_t *out, int pitch, const rgb_t *pal)
{
    uint32_t lut[4];
    for (int i = 0; i < 4; i++)
        lut[i] = 0xFF000000u | ((uint32_t)pal[i].r << 16) | ((uint32_t)pal[i].g << 8) | pal[i].b;
    for (int y = 0; y < CGA_H; y++)
        for (int x = 0; x < CGA_W; x++)
            out[y * pitch + x] = lut[cga_get(x, y)];
}

/* --- Minimal PNG writer: uncompressed ("stored") deflate blocks, so no zlib is needed. --- */

static uint32_t crc_table[256];

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static int write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t hdr[8];
    put32(hdr, len);
    memcpy(hdr + 4, type, 4);
    uint32_t crc = crc_update(0xFFFFFFFFu, hdr + 4, 4);
    crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
    uint8_t tail[4];
    put32(tail, crc);
    return fwrite(hdr, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) &&
           fwrite(tail, 1, 4, f) == 4 ? 0 : -1;
}

int cga_write_png(const char *path, const rgb_t *pal, int scale)
{
    if (scale < 1)
        scale = 1;
    const int w = CGA_W * scale, h = CGA_H * scale;
    uint32_t *px = malloc((size_t)w * h * sizeof *px);
    if (!px)
        return -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const rgb_t c = pal[cga_get(x / scale, y / scale)];
            px[y * w + x] = 0xFF000000u | (uint32_t)c.r << 16 | (uint32_t)c.g << 8 | c.b;
        }
    const int rc = png_write_argb(path, px, w, h);
    free(px);
    return rc;
}

int png_write_argb(const char *path, const uint32_t *px, int width, int height)
{
    const uint32_t w = (uint32_t)width, h = (uint32_t)height;
    const size_t row = 1 + (size_t)w * 3, raw_len = row * h;

    uint8_t *raw = malloc(raw_len);
    if (!raw)
        return -1;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *p = raw + y * row;
        *p++ = 0; /* filter: none */
        for (uint32_t x = 0; x < w; x++) {
            const uint32_t c = px[y * w + x];
            *p++ = (uint8_t)(c >> 16); *p++ = (uint8_t)(c >> 8); *p++ = (uint8_t)c;
        }
    }

    /* zlib stream: header, stored blocks of up to 65535 bytes, Adler-32. */
    const size_t nblocks = (raw_len + 65534) / 65535;
    const size_t z_len = 2 + raw_len + nblocks * 5 + 4;
    uint8_t *z = malloc(z_len), *zp = z;
    if (!z) {
        free(raw);
        return -1;
    }
    *zp++ = 0x78; *zp++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_len; off += 65535) {
        size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
        *zp++ = off + n == raw_len ? 1 : 0;
        *zp++ = (uint8_t)n; *zp++ = (uint8_t)(n >> 8);
        *zp++ = (uint8_t)~n; *zp++ = (uint8_t)(~n >> 8);
        memcpy(zp, raw + off, n);
        zp += n;
        for (size_t i = 0; i < n; i++) {
            a = (a + raw[off + i]) % 65521;
            b = (b + a) % 65521;
        }
    }
    put32(zp, (b << 16) | a);

    if (!crc_table[1])
        crc_init();
    FILE *f = fopen(path, "wb");
    int rc = -1;
    if (f) {
        uint8_t ihdr[13];
        put32(ihdr, w);
        put32(ihdr + 4, h);
        ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0; /* 8-bit RGB */
        rc = fwrite("\x89PNG\r\n\x1a\n", 1, 8, f) == 8 &&
             write_chunk(f, "IHDR", ihdr, 13) == 0 &&
             write_chunk(f, "IDAT", z, (uint32_t)z_len) == 0 &&
             write_chunk(f, "IEND", NULL, 0) == 0 ? 0 : -1;
        if (fclose(f) != 0)
            rc = -1;
    }
    free(z);
    free(raw);
    return rc;
}
