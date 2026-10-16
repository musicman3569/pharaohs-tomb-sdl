/* Game assets read from the user's own PTOMBn.EXE (packed or unpacked).
 *
 * Nothing copyrighted is built into the port: sprite pixels and level layouts come from the
 * EXE at run time. They are found by byte pattern (see docs/notes.md), the same way the Python
 * tools do it, so all four volumes work. */
#ifndef PTOMB_GAMEDATA_H
#define PTOMB_GAMEDATA_H

#include <stddef.h>
#include <stdint.h>

#define LEVEL_ROWS 12
#define LEVEL_COLS 20
#define MAX_LEVELS 20
#define SPRITE_SETS 40
#define SPRITE_FRAMES 4

typedef struct {
    uint8_t *image;          /* load module, unrelocated */
    size_t image_len;
    uint32_t entry;          /* linear CS:IP of the original program */
    const uint8_t *ds;       /* the data segment inside `image` */
    size_t ds_len;
    unsigned ds_seg;         /* paragraph, relative to the image */
    int num_levels;
    /* rows[level-1][row-1] = 20 tile characters, NUL-terminated */
    char rows[MAX_LEVELS][LEVEL_ROWS][LEVEL_COLS + 1];

    int volume;                                /* 1..4, from the 'ptombN' name in main */
    char level_names[MAX_LEVELS][41];          /* ShowLevelTitle's names for this volume */
    /* The game's own fonts for characters 80h-FFh (what it points INT 1Fh at): 128 glyphs x
     * 8 bytes, rows top first. Index by font_id_t. */
    uint8_t fonts[5][128 * 8];

    /* Where things are. All units after the main program are identical in the four volumes and
     * only move by a few paragraphs, so a routine's address is its PTOMB1 address + seg_delta. */
    int seg_delta;            /* this EXE's unit segments minus PTOMB1's, in paragraphs */
    char base_name[16];       /* 'ptombN', the game's name for its .MNU / .HS files */
    char dir[512];            /* folder the EXE is in, with trailing separator (or "") */
    /* Main-program routines (image offsets, 0 if not found): they move between volumes. */
    size_t fn_title_screen, fn_main_menu, fn_high_scores, fn_joystick_menu, fn_game_over;
} gamedata_t;

/* Units as they sit in PTOMB1 (image-relative paragraphs); add g->seg_delta for other volumes. */
enum {
    SEG_GAME = 0x0854,   /* game unit, shown in Ghidra as 1854 */
    SEG_GRAPH3 = 0x0F47,
    SEG_CRT = 0x1081,
    SEG_JOY = 0x10E3,
    SEG_SYSTEM = 0x1714,
};

/* Linear image offset of SEG:OFF in this EXE. */
size_t gamedata_addr(const gamedata_t *g, unsigned seg_ptomb1, unsigned off);

/* Text-drawing calls found in a routine, in code order (see gamedata_text_ops). */
typedef enum { OP_FONT, OP_COLOR, OP_CENTER, OP_AT, OP_STRLOAD } text_op_kind_t;
typedef struct {
    text_op_kind_t kind;
    int a, b;          /* FONT: font_id; COLOR: colour; CENTER: colour, row; AT: col, row */
    char text[81];     /* CENTER / AT / STRLOAD */
} text_op_t;

/* Scan len bytes of code at image offset `start` (in segment `cs`) for the game's text calls
 * with constant arguments: DrawTextCentered, DrawTextAt, TextColor, the font selectors and
 * StrLoad of a constant. Returns how many were stored in ops. */
int gamedata_text_ops(const gamedata_t *g, size_t start, size_t len, unsigned cs, text_op_t *ops, int max);

/* Which font-select routine (1854:1078..1124) picks each table. */
typedef enum {
    FONT_FRAME,  /* 1854:1078, DS:7180  border pieces for DrawFrame */
    FONT_GAME,   /* 1854:10A3, DS:6180  in-game: letters, digits, status icons */
    FONT_MENU,   /* 1854:10CE, DS:6580  menus, game over */
    FONT_TITLE,  /* 1854:10F9, DS:6980  title and high scores */
    FONT_EXTRA,  /* 1854:1124, DS:6D80 */
} font_id_t;

/* Load and scan an EXE. Returns 0, or -1 with a message in err. */
int gamedata_load(gamedata_t *g, const char *path, char *err, size_t errlen);
void gamedata_free(gamedata_t *g);

/* A sprite frame as a Graph3 picture buffer (16x16), or NULL if out of range. Sprite sets are the
 * 40 x 4 slots at DS:0000 that the game copies to BSS at startup (InitSprites_A/B). */
const uint8_t *gamedata_sprite(const gamedata_t *g, int set, int frame);

#endif
