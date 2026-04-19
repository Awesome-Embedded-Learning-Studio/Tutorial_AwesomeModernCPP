#include "b.h"
#include "a.h"

#include <stdio.h>

// Same name as in a.c, but static — no conflict!
static void helper_a(void)
{
    printf("  [b.c] helper_a called (different from a.c's version)\n");
}

void print_from_b(void)
{
    printf("[b.c] print_from_b:\n");
    helper_a();
    printf("  [b.c] can access kSharedValue = %d via extern\n", kSharedValue);
}
