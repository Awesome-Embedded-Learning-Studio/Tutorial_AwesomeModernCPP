#include <stdio.h>
#include <limits.h>

// Try compiling with: -fsanitize=undefined to observe signed overflow detection
int main(void)
{
    int i = INT_MAX;
    unsigned int u = UINT_MAX;

    printf("INT_MAX  = %d,  INT_MAX + 1  = %d\n", i, i + 1);
    printf("UINT_MAX = %u, UINT_MAX + 1 = %u\n", u, u + 1);

    return 0;
}
