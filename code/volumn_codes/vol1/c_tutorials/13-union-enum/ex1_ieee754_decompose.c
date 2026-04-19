#include <stdio.h>
#include <stdint.h>

typedef union {
    float    f;
    uint32_t u;
} FloatBits;

void print_float_bits(float value)
{
    FloatBits fb;
    fb.f = value;

    uint32_t sign     = (fb.u >> 31) & 0x1;
    uint32_t exponent = (fb.u >> 23) & 0xFF;
    uint32_t mantissa = fb.u & 0x7FFFFF;

    printf("  %10.4f -> sign=%u, exp=%3u (biased %d), mantissa=0x%06X\n",
           value, sign, exponent, (int)exponent - 127, mantissa);
}

int main(void)
{
    float values[] = {0.0f, -3.14f, 1.0f, 42.0f, 0.1f};

    for (int i = 0; i < 5; i++) {
        print_float_bits(values[i]);
    }

    return 0;
}
