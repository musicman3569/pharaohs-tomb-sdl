#include "tp.h"

uint32_t tp_randseed;

int tp_random(int n)
{
    if (n <= 0)
        return 0;
    tp_randseed = tp_randseed * 134775813u + 1u;
    return (int)(((tp_randseed >> 16) * (uint32_t)n) >> 16);
}
