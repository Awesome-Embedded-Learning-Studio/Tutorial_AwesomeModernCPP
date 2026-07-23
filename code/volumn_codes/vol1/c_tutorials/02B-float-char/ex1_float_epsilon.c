#include <float.h>
#include <math.h>
#include <stdio.h>

// double 用 DBL_EPSILON
int double_equal(double a, double b) {
    return fabs(a - b) < DBL_EPSILON;
}

// float 用 FLT_EPSILON
int float_equal(float a, float b) {
    return fabsf(a - b) < FLT_EPSILON;
}

int main(void) {
    // 演示:0.1 + 0.2 与 0.3 在不同精度下的近似相等判断
    printf("double: 0.1 + 0.2 == 0.3 ? %s\n", double_equal(0.1 + 0.2, 0.3) ? "yes" : "no");
    printf("float:  0.1f + 0.2f == 0.3f ? %s\n", float_equal(0.1f + 0.2f, 0.3f) ? "yes" : "no");
    return 0;
}
