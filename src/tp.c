#include "tp.h"

#include <math.h>
#include <string.h>

uint32_t tp_randseed;

int tp_random(int n)
{
    if (n <= 0)
        return 0;
    tp_randseed = tp_randseed * 134775813u + 1u;
    return (int)(((tp_randseed >> 16) * (uint32_t)n) >> 16);
}

double tp_real48_decode(const unsigned char *b)
{
    if (b[0] == 0)
        return 0.0;
    double m = 1.0;
    const unsigned char mant[5] = {(unsigned char)(b[5] & 0x7F), b[4], b[3], b[2], b[1]};
    for (int i = 0; i < 5; i++)
        m += mant[i] / pow(2.0, 7 + 8 * i);
    return (b[5] & 0x80 ? -m : m) * pow(2.0, b[0] - 129);
}

void tp_real48_encode(double v, unsigned char *b)
{
    memset(b, 0, 6);
    if (v == 0.0)
        return;
    const int neg = v < 0;
    if (neg)
        v = -v;
    int e;
    double m = frexp(v, &e) * 2.0; /* 1 <= m < 2 */
    e -= 1;
    b[0] = (unsigned char)(e + 129);
    m -= 1.0;
    unsigned long long bits = (unsigned long long)(m * (double)(1ULL << 39) + 0.5); /* 39 bits */
    b[5] = (unsigned char)((bits >> 32) & 0x7F);
    b[4] = (unsigned char)(bits >> 24);
    b[3] = (unsigned char)(bits >> 16);
    b[2] = (unsigned char)(bits >> 8);
    b[1] = (unsigned char)bits;
    if (neg)
        b[5] |= 0x80;
}
