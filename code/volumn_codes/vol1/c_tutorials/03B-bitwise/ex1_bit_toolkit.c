#include <stdio.h>
#include <stdint.h>

uint32_t bit_set(uint32_t value, int n)
{
    return value | (1u << n);
}

uint32_t bit_clear(uint32_t value, int n)
{
    return value & ~(1u << n);
}

uint32_t bit_toggle(uint32_t value, int n)
{
    return value ^ (1u << n);
}

uint32_t bit_extract(uint32_t value, int high, int low)
{
    int width = high - low + 1;
    uint32_t mask = (width >= 32) ? 0xFFFFFFFF : ((1u << width) - 1);
    return (value >> low) & mask;
}

static void print_binary(uint32_t v)
{
    for (int i = 31; i >= 0; i--) {
        printf("%d", (v >> i) & 1);
        if (i % 4 == 0 && i > 0) printf(" ");
    }
    printf("\n");
}

int main(void)
{
    uint32_t v = 0;

    v = bit_set(v, 3);
    printf("Set bit 3:    "); print_binary(v);

    v = bit_set(v, 7);
    printf("Set bit 7:    "); print_binary(v);

    v = bit_toggle(v, 3);
    printf("Toggle bit 3: "); print_binary(v);

    v = bit_clear(v, 7);
    printf("Clear bit 7:  "); print_binary(v);

    uint32_t val = 0xABCD1234;
    printf("\nExtract bits [19:16] from 0x%08X: 0x%X\n",
           val, bit_extract(val, 19, 16));

    return 0;
}
