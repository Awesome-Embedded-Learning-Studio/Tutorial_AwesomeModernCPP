#include "math_utils.h"

int clamp_int(int value, int min_val, int max_val)
{
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

int count_digits(int n)
{
    if (n == 0) return 1;
    if (n < 0) n = -n;
    int count = 0;
    while (n > 0) {
        n /= 10;
        count++;
    }
    return count;
}
