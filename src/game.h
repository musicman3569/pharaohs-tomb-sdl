/* Game state and the in-game logic: GameTick (1000:4F69) and the player routines it calls.
 *
 * Fields are named for what they do, with the original DS address in the comment, so the
 * code can be read side by side with the disassembly. */
#ifndef PTOMB_GAME_H
#define PTOMB_GAME_H

#include <stdbool.h>

#include "gamedata.h"
#include "level.h"

typedef struct {
    const gamedata_t *data;
    level_t lv;

    /* Persistent across levels */
    double score;          /* [7982] 6-byte Real */
    int lives;             /* [75E4] */
    int spears;            /* [75E2] */
    int treasure;          /* [75E6] */
    int t_count;           /* [75EC] */
    int saved_spears;      /* [75FC] values restored when a level (re)starts */
    int saved_t_count;     /* [75F8] */
    int saved_treasure;    /* [75FA] */
    int level;             /* [75D0] */
    int levels_entered;    /* [75EE] */
    bool cheat;            /* [795D] */
    bool sound_on;         /* [7994] */
    bool life_bonus_taken; /* [796B] hidden extra life, once per game */
    bool restored;         /* [796E] skip restoring counters on the next StartLevel */
    int key_layout;        /* [8101] PTOMB.CFG byte: '1' Shift, '2' Alt/Ctrl, '3' Ctrl/Alt */

    /* Player */
    int px, py;            /* [74DC] [74DE]  py is the bottom row */
    int frame;             /* [74DA] 1..4 */
    int facing;            /* [74EA] 0 right, 1 left */
    int prev_facing;       /* [796C] */
    int turn_count;        /* [75E0] */
    bool jumping;          /* [7966] in the air (jumping or falling) */
    bool jump_hit;         /* [7967] */
    bool falling;          /* [7969] fell off a ledge rather than jumped */
    bool walk_blocked;     /* [796A] */
    bool pushed;           /* [7973] PushPlayer in progress */
    bool on_platform;      /* [796D] */
    int jump_steps;        /* [75D6] < 0x22 rising, >= 0x22 falling */
    int jump_pitch;        /* [75F4] */
    int jump_pause;        /* [75F6] */
    bool key_cyan;         /* [7961] */
    bool key_magenta;      /* [7962] */
    bool invisible;        /* [795E] */
    int invis_timer;       /* [75F2] */
    bool frozen;           /* [7968] */
    int freeze_count;      /* [75EA] */
    bool wind_right;       /* [7971] level 10 */
    bool wind_left;        /* [7972] level 16 */

    /* Spear in flight */
    bool shot;             /* [795F] */
    bool shot_left;        /* [7960] */
    int shot_x, shot_y;    /* [75DC] [75DE] */

    /* Dart traps: one dart per direction for the whole level. The left dart's y is DS:7606,
     * which is also glint entry 0's x, so it's stored there (lv.glint_x[0]). */
    bool dart_r;           /* [796F] */
    int dart_rx, dart_ry;  /* [7600] [7602] */
    bool dart_l;           /* [7970] */
    int dart_lx;           /* [7604] */

    /* Gem glint animation */
    bool glint_on;         /* [7864] */
    int glint_frame;       /* [7866] */
    int glint_pick;        /* [786A] */

    int loop_r, loop_c;    /* [75C0] [75C2] loop counters KillPlayer forces to their end */
    int last_key;          /* [795C] */
    bool quit;             /* [7963] */
    bool game_over;        /* [7964] */
} game_t;

extern game_t G;

/* How often GameTick runs. On the original it runs flat out; anything speed-sensitive is paced
 * by the 18.2 Hz timer counters instead. A fixed 70 Hz keeps those counters from being missed
 * while keeping per-call randomness (gem glints) independent of the monitor. */
#define GAME_TICK_HZ 70

/* ResetGameState (1000:08D7). */
void game_reset_state(const gamedata_t *data, int key_layout);

/* PlayGame (1000:53B7): StartLevel(level), then GameTick until quit or game over. */
void game_play(int level, bool cheat);

/* The title-screen walking demo (1000:5D18). */
void game_title_demo(const gamedata_t *data, int key_layout);

/* StartLevel (1000:7638), including the level title wait (1854:06A5). */
void game_start_level(int level);

/* One pass of GameTick (1000:4F69). */
void game_tick(void);

#endif
