#include <stdio.h>
#include <string.h>

/* Why are floating-point numbers approximations? Spread out the 32 bits of 0.1f and it becomes
 * clear. */
int main(void) {
    /* 1) float has only 32 bits: 1 sign + 8 exponent + 23 mantissa */
    float f = 0.1f;
    unsigned int bits;
    memcpy(&bits, &f, sizeof(bits));

    printf("32 bits of 0.1f in a float (sign | exponent | mantissa):\n  ");
    for (int i = 31; i >= 0; i--) {
        printf("%d", (bits >> i) & 1);
        if (i == 31 || i == 23)
            printf(" ");
    }
    printf("\n\n");

    /* The mantissa 1001100110011... repeats forever; 23 bits can't hold it, so it gets truncated.
     */
    printf("Printed at different precisions — never exactly 0.1:\n");
    printf("  9 digits : %.9f\n", f);
    printf("  20 digits: %.20f\n\n", f);

    /* 2) The classic 0.1 + 0.2 != 0.3 only shows up under double */
    double a = 0.1, b = 0.2, c = 0.3;
    printf("double: 0.1 + 0.2 == 0.3 ? %s\n", (a + b == c) ? "yes" : "no");
    printf("  a + b = %.20f\n", a + b);
    printf("  c     = %.20f\n", c);

    return 0;
}
