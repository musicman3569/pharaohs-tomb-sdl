/* Title screen, main menu, high scores, and the text screens behind the menu. */
#ifndef PTOMB_MENU_H
#define PTOMB_MENU_H

#include <stdbool.h>

#include "gamedata.h"

/* Load the title picture, PTOMB.CFG and the high scores. Returns 0, or -1 with a message. */
int menu_init(const gamedata_t *g, char *err, size_t errlen);

/* TitleScreen (1000:6227): picture, credits and the walking demo. */
void menu_title_screen(void);

/* MainMenu (1000:5A9F). Returns when the player quits. */
void menu_main(bool cheat_from_command_line);

/* GameOver (1000:3336): if the score made the table, ask for a name and save it. */
void menu_game_over(double score);

/* VolumeEnding (1854:4E01): the text pages shown after level 20 of a volume. */
void menu_volume_ending(int volume);

/* ShowHighScores (1000:5403); wait = wait for a key afterwards. */
void menu_show_high_scores(bool wait);

/* The key layout loaded from PTOMB.CFG ('1', '2' or '3'), or 0 if there was none. */
int menu_key_layout(void);

#endif
