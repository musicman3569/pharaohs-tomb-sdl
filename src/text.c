#include "text.h"

#include <stdio.h>
#include <string.h>

#include "cga.h"
#include "font8x8.h"

static const uint8_t *high_font; /* what INT 1Fh points at */
static int cur_col = 1, cur_row = 1, cur_color = 7;

void text_select_font(const gamedata_t *g, font_id_t font)
{
    high_font = g->fonts[font];
}

void text_gotoxy(int col, int row)
{
    cur_col = col;
    cur_row = row;
}

void text_color(int color)
{
    cur_color = color;
}

/* BIOS character output in CGA mode 4: the whole 8x8 cell is written, ink pixels in the
 * colour's low two bits, the rest colour 0. Writes past column 40 wrap to the next row; the
 * game never writes to the very last cell, so the BIOS scroll that would cause isn't needed. */
void text_putc(int ch)
{
    ch &= 0xFF;
    if (cur_col > 40) {
        cur_col = 1;
        cur_row++;
    }
    if (cur_col >= 1 && cur_row >= 1 && cur_row <= 25) {
        static const uint8_t blank[8];
        const uint8_t *glyph = ch < 0x80 ? font8x8_glyph(ch) : high_font ? high_font + (ch - 0x80) * 8 : blank;
        const int x0 = (cur_col - 1) * 8, y0 = (cur_row - 1) * 8;
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++)
                cga_set(x0 + c, y0 + r, (glyph[r] & (0x80 >> c)) ? (cur_color & 3) : 0);
    }
    cur_col++;
}

void text_write(const char *s)
{
    while (*s)
        text_putc((unsigned char)*s++);
}

static int remap_letter(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 0x41 : c; /* the set at 1854:1365 */
}

void text_draw_at(int col, int row, const char *s)
{
    text_gotoxy(col, row);
    for (; *s; s++)
        text_putc(remap_letter((unsigned char)*s));
}

void text_draw_centered(int color, int row, const char *s)
{
    const int len = (int)strlen(s);
    text_gotoxy((40 - len) / 2 + 1, row);
    text_color(color);
    for (; *s; s++)
        text_putc(remap_letter((unsigned char)*s));
}

void text_draw_number(double value, int col, int row)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%ld", (long)value); /* Trunc */
    text_gotoxy(col, row);
    for (const char *p = buf; *p; p++) {
        int c = (unsigned char)*p;
        if (c >= '1' && c <= '9') /* the set at 1854:1500 */
            c += 0x83;
        else if (c == '0')
            c += 0x8D;
        text_putc(c);
    }
}

void text_clear_status_line(void)
{
    text_gotoxy(1, 25);
    text_write("                                       ");
}

void text_draw_frame(const gamedata_t *g, int color, int left, int top, int right, int bottom)
{
    text_select_font(g, FONT_FRAME);
    text_color(color);
    text_gotoxy(left, top);
    text_putc(0x80);
    for (int x = left + 1; x <= right - 1; x++) {
        text_gotoxy(x, top);
        text_putc(0x81);
    }
    text_gotoxy(right, top);
    text_putc(0x83);
    for (int y = top + 1; y <= bottom - 1; y++) {
        text_gotoxy(left, y);
        text_putc(0x84);
        text_gotoxy(right, y);
        text_putc(0x85);
    }
    text_gotoxy(left, bottom);
    text_putc(0x86);
    for (int x = left + 1; x <= right - 1; x++) {
        text_gotoxy(x, bottom);
        text_putc(0x87);
    }
    text_gotoxy(right, bottom);
    text_putc(0x89);
}
