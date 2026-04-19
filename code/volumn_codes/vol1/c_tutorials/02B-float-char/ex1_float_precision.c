#include <stdio.h>
#include <math.h>
#include <float.h>

int float_equal(float a, float b)
{
    float diff = fabsf(a - b);
    if (a == 0.0f || b == 0.0f) {
        return diff < FLT_EPSILON;
    }
    return diff / fmaxf(fabsf(a), fabsf(b)) < FLT_EPSILON;
}

int main(void)
{
    float a = 0.1f + 0.2f;
    float b = 0.3f;

    printf("0.1f + 0.2f = %.20f\n", a);
    printf("0.3f        = %.20f\n", b);
    printf("a == b: %s\n", a == b ? "true" : "false");
    printf("float_equal(a, b): %s\n", float_equal(a, b) ? "true" : "false");

    return 0;
}
