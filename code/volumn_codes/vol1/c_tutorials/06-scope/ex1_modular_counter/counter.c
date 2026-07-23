#include "counter.h"

static int counter = 0;
void counter_increment(void) {
    counter++;
}

void counter_reset(void) {
    counter = 0;
}

int counter_get(void) {
    return counter;
}
