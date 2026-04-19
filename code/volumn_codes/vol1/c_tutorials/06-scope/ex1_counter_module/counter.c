#include "counter.h"

static int kCount = 0;

void counter_increment(void)
{
    kCount++;
}

int counter_get(void)
{
    return kCount;
}

void counter_reset(void)
{
    kCount = 0;
}
