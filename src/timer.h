/* The game's INT 1Ch timer handler (1854:6AFA).
 *
 * On the PC it runs on every BIOS timer tick, 1193182 / 65536 = ~18.2065 Hz, and just adds 1
 * to six counters at DS:7F6A..7F74. Game code checks a counter for > 0 and zeroes it, which is
 * how speed stays the same on fast and slow machines. */
#ifndef PTOMB_TIMER_H
#define PTOMB_TIMER_H

#include <stdint.h>

#define TIMER_HZ (1193182.0 / 65536.0)

enum {
    TICK_7F6A, /* player horizontal movement (PlayerMoveHorizontal) */
    TICK_7F6C, /* enemies (MoveEnemies) */
    TICK_7F6E, /* jump/fall animation */
    TICK_7F70, /* left-firing dart traps */
    TICK_7F72, /* right-firing dart traps */
    TICK_7F74, /* delays */
    TICK_COUNT
};

extern int tick_counter[TICK_COUNT];
extern uint32_t tick_total; /* port-only: ticks since start */

void timer_isr(void);

#endif
