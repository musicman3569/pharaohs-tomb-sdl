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
} gamedata_t;

/* Load and scan an EXE. Returns 0, or -1 with a message in err. */
int gamedata_load(gamedata_t *g, const char *path, char *err, size_t errlen);
void gamedata_free(gamedata_t *g);

/* A sprite frame as a Graph3 picture buffer (16x16), or NULL if out of range. Sprite sets are the
 * 40 x 4 slots at DS:0000 that the game copies to BSS at startup (InitSprites_A/B). */
const uint8_t *gamedata_sprite(const gamedata_t *g, int set, int frame);

#endif
