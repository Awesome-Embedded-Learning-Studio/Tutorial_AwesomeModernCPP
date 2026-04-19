#include <stdio.h>
#include <stdint.h>

uint32_t safe_shift_left(uint32_t val, int n, int bits)
{
    if (n < 0 || n >= bits) {
        return 0;
    }
    return val << n;
}

int main(void)
{
    printf("safe_shift_left(1, 4, 32) = 0x%08X\n", safe_shift_left(1, 4, 32));
    printf("safe_shift_left(1, 31, 32) = 0x%08X\n", safe_shift_left(1, 31, 32));
    printf("safe_shift_left(1, 32, 32) = 0x%08X (invalid, returns 0)\n",
           safe_shift_left(1, 32, 32));
    printf("safe_shift_left(1, -1, 32) = 0x%08X (invalid, returns 0)\n",
           safe_shift_left(1, -1, 32));

    return 0;
}
