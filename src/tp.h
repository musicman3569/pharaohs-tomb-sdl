/* Bits of the Turbo Pascal runtime that game logic depends on. */
#ifndef PTOMB_TP_H
#define PTOMB_TP_H

#include <stdint.h>

extern uint32_t tp_randseed;

/* Random(n): 0 <= result < n, using Turbo Pascal's linear congruential generator
 * (RandSeed := RandSeed * 134775813 + 1). Not yet checked against the game's System unit
 * (2714:08D5), so treat the exact sequence as unverified. */
int tp_random(int n);

#endif
