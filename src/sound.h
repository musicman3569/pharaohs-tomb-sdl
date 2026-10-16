/* Sound effects: the frequency sweep at 1854:15F0 and the effects built on it. */
#ifndef PTOMB_SOUND_H
#define PTOMB_SOUND_H

#include <stdbool.h>

/* 1854:15F0 Sweep(lo, hi, gap, hold, step, up, staccato, enabled).
 * Starting from lo (up) or hi (down), repeat: Delay(gap); freq +-= step; Sound(freq);
 * Delay(hold); NoSound if staccato; until freq passes the far end; then NoSound.
 * Delays are timer ticks. Does nothing when `enabled` (sound effects on) is false. */
void sfx_sweep(int lo, int hi, int gap, int hold, int step, bool up, bool staccato, bool enabled);

void sfx_pickup(bool enabled);       /* 1854:58D2 keys, spears, switches, T */
void sfx_bonus(bool enabled);        /* 1854:592F full spears, extra life, bonuses */
void sfx_death(bool enabled);        /* 1854:595F */
void sfx_unlock(bool enabled);       /* 1854:598F keyhole, magenta lock */
void sfx_enemy_killed(bool enabled); /* 1854:5900 */
void sfx_artifact(bool enabled);     /* 1000:00B9 */

#endif
