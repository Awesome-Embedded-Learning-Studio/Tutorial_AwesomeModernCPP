#include "math_utils.h"

#include <stdio.h>

int main(void)
{
    printf("clamp(50, 0, 100) = %d\n", clamp_int(50, 0, 100));
    printf("clamp(-5, 0, 100) = %d\n", clamp_int(-5, 0, 100));
    printf("clamp(200, 0, 100) = %d\n", clamp_int(200, 0, 100));

    printf("digits(0) = %d\n", count_digits(0));
    printf("digits(42) = %d\n", count_digits(42));
    printf("digits(12345) = %d\n", count_digits(12345));
    printf("digits(-999) = %d\n", count_digits(-999));

    return 0;
}
