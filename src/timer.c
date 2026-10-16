#include "timer.h"

int tick_counter[TICK_COUNT];
uint32_t tick_total;

void timer_isr(void)
{
    for (int i = 0; i < TICK_COUNT; i++)
        tick_counter[i]++;
    tick_total++;
}
