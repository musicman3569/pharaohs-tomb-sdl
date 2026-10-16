/* Bits of the Turbo Pascal runtime that game logic depends on. */
#ifndef PTOMB_TP_H
#define PTOMB_TP_H

#include <stdint.h>

extern uint32_t tp_randseed;

/* Random(n): 0 <= result < n, using Turbo Pascal's linear congruential generator
 * (RandSeed := RandSeed * 134775813 + 1). Not yet checked against the game's System unit
 * (2714:08D5), so treat the exact sequence as unverified. */
int tp_random(int n);

/* Turbo Pascal 6-byte Real: byte 0 exponent (bias 129, 0 = zero), bytes 1-5 mantissa with an
 * implied leading 1, top bit of byte 5 the sign. Scores are stored this way. */
double tp_real48_decode(const unsigned char *b);
void tp_real48_encode(double v, unsigned char *b);

#endif
