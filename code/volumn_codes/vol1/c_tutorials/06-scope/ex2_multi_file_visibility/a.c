#include "a.h"

#include <stdio.h>

const int kSharedValue = 42;

static void helper_a(void)
{
    printf("  [a.c] helper_a called, kSharedValue = %d\n", kSharedValue);
}

void print_from_a(void)
{
    printf("[a.c] print_from_a:\n");
    helper_a();
}

void set_shared_value(int val)
{
    // Note: kSharedValue is const, this would require removing const
    // For demonstration, we show that extern linkage works:
    printf("[a.c] kSharedValue is const (%d), cannot modify\n", kSharedValue);
    (void)val;
}
