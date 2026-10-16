/* Text in graphics mode, the way the game does it.
 *
 * The game writes with Crt (GotoXY, TextColor, Write) while in CGA mode 4, so the BIOS draws
 * each character as an opaque 8x8 cell on a 40x25 grid: ink in the text colour, everything
 * else colour 0. Characters 00h-7Fh come from the BIOS ROM font (the port's own font8x8 here),
 * 80h-FFh from the table INT 1Fh points at, which the game sets to one of its own fonts.
 *
 * Its text routines then remap characters into that upper half: A-Z become 82h-9Bh and digits
 * B4h-BDh, so all lettering uses the game's fonts and only punctuation uses the ROM font. */
#ifndef PTOMB_TEXT_H
#define PTOMB_TEXT_H

#include "gamedata.h"

/* Point "INT 1Fh" at one of the game's fonts (1854:1078 .. 1124). */
void text_select_font(const gamedata_t *g, font_id_t font);

/* Crt GotoXY / TextColor / Write(char), 1-based cell coordinates. */
void text_gotoxy(int col, int row);
void text_color(int color);
void text_putc(int ch);
void text_write(const char *s); /* raw characters, no remapping */

/* 1854:1385 DrawTextAt(col, row, s): current colour, A-Z remapped. */
void text_draw_at(int col, int row, const char *s);
/* 1854:1446 DrawTextCentered(color, row, s): A-Z remapped, centred on 40 columns. */
void text_draw_centered(int color, int row, const char *s);
/* 1854:1520 DrawNumber(value, col, row): Str(Trunc(value)) with digits remapped. */
void text_draw_number(double value, int col, int row);
/* 1854:1330 ClearStatusLine: 39 spaces at (1, 25). */
void text_clear_status_line(void);
/* 1854:114F DrawFrame: a box of border glyphs in the frame font. Leaves that font selected. */
void text_draw_frame(const gamedata_t *g, int color, int left, int top, int right, int bottom);

#endif
