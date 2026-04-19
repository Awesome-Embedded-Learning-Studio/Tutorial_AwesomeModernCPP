#include "counter.h"

#include <stdio.h>

int main(void)
{
    printf("Initial: %d\n", counter_get());

    for (int i = 0; i < 5; i++) {
        counter_increment();
    }
    printf("After 5 increments: %d\n", counter_get());

    counter_reset();
    printf("After reset: %d\n", counter_get());

    return 0;
}
