/* Level state and StartLevel / DrawLevelRow (1000:7638 / 1000:0A58). */
#ifndef PTOMB_LEVEL_H
#define PTOMB_LEVEL_H

#include <stdbool.h>

#include "gamedata.h"

#define MAX_ENEMIES 10
#define MAX_GLINTS 64

/* One enemy slot (DS:7EA4 + i*0x12). See docs/notes.md "Enemies". */
typedef struct {
    int frame;  /* +00 animation frame 1..4 */
    int x, y;   /* +02 / +04, y is the bottom row like PutPic */
    int speed;  /* +06 */
    int dx, dy; /* +08 / +0A */
    int type;   /* +0C 1..10 */
    bool free;  /* +11 */
} enemy_t;

typedef struct {
    int level;                                      /* [75D0], 1..20 */
    unsigned char map[LEVEL_ROWS + 1][LEVEL_COLS + 1]; /* tile codes, 1-based like DS:7857 */
    enemy_t enemy[MAX_ENEMIES + 1];                 /* 1-based like the game */
    int spawn_count;                                /* [7F76], counts every spawn attempt */
    int glint_x[MAX_GLINTS + 1], glint_y[MAX_GLINTS + 1]; /* 'c' gems, DS:7606 + n*6 */
    int glint_count;                                /* [7868] */
    int player_x, player_y;                         /* [74DC] / [74DE] */
    int wind;                                       /* +2 level 10 ([7971]), -2 level 16 ([7972]) */
    bool invisible;                                 /* [795E], set at the start of level 17 */
} level_t;

/* StartLevel: clear the screen, draw all 12 rows, fill the tile map, spawn enemies, and draw
 * the line above the status bar. The status bar text itself isn't drawn yet. */
void level_start(level_t *lv, const gamedata_t *g, int level);

#endif
