/* The high-score table: PTOMBn.HS, 5 records of 262 bytes (a 6-byte Turbo Pascal Real score,
 * then a string[255] name), kept in ascending order so record 5 is the best. */
#ifndef PTOMB_HISCORE_H
#define PTOMB_HISCORE_H

#include "gamedata.h"

#define HISCORE_COUNT 5
#define HISCORE_MARKER "~|!+" /* name given to a new entry until it's typed in */

typedef struct {
    double score;
    char name[256];
} hiscore_t;

extern hiscore_t hiscores[HISCORE_COUNT + 1]; /* 1-based like DS:7892 + i*0x106 */

/* LoadHighScores (1854:662F): read the .HS file next to the EXE, or set the defaults. */
void hiscore_load(const gamedata_t *g);
/* 1854:6794: write the .HS file back. */
void hiscore_save(const gamedata_t *g);
/* 1854:6396: sort ascending (bubble sort, as the game does). */
void hiscore_sort(void);

#endif
