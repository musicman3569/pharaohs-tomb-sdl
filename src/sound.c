#include "sound.h"

#include "platform.h"

/* How long one step of a sweep with no delays took on the original machine: the loop runs
 * Delay(0), Crt Sound (a 32-bit divide plus port writes) and Delay(0) again, roughly 600 cycles,
 * or ~125 us on a 4.77 MHz 8088. A faster PC made these sounds shorter; this picks the
 * original IBM PC speed. */
#define ZERO_DELAY_STEP_US 125

void sfx_sweep(int lo, int hi, int gap, int hold, int step, bool up, bool staccato, bool enabled)
{
    if (!enabled)
        return;
    int freq = up ? lo : hi;

    if (gap == 0 && hold == 0) {
        /* No waits at all: the sweep is over in a few milliseconds of CPU time. Queue it to the
         * audio thread so it's audible, without blocking (the original didn't block either). */
        for (;;) {
            freq += up ? step : -step;
            if (staccato)
                plat_sound_blip(freq, ZERO_DELAY_STEP_US / 2, ZERO_DELAY_STEP_US / 2);
            else
                plat_sound_blip(freq, ZERO_DELAY_STEP_US, 0);
            if ((up && freq >= hi) || (!up && freq <= lo))
                break;
        }
        plat_nosound();
        return;
    }

    for (;;) {
        plat_delay(gap);
        freq += up ? step : -step;
        plat_sound(freq);
        plat_delay(hold);
        if (staccato)
            plat_nosound();
        if ((up && freq >= hi) || (!up && freq <= lo))
            break;
    }
    plat_nosound();
}

void sfx_pickup(bool enabled) { sfx_sweep(500, 2000, 0, 0, 10, true, false, enabled); }
void sfx_bonus(bool enabled) { sfx_sweep(10, 3000, 1, 1, 750, true, true, enabled); }
void sfx_death(bool enabled) { sfx_sweep(100, 1000, 1, 1, 200, false, false, enabled); }
void sfx_unlock(bool enabled) { sfx_sweep(1500, 3000, 1, 0, 200, true, false, enabled); }
void sfx_enemy_killed(bool enabled) { sfx_sweep(100, 8000, 1, 0, 2000, true, false, enabled); }
void sfx_artifact(bool enabled) { sfx_sweep(100, 5000, 2, 0, 500, true, false, enabled); }
