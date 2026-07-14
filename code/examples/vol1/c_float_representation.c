#include <stdio.h>
#include <string.h>

/* 浮点数为什么是近似值？把 0.1f 的 32 个二进制位摊开看就明白了。 */
int main(void) {
    /* 1) float 只有 32 位：1 符号 + 8 指数 + 23 尾数 */
    float f = 0.1f;
    unsigned int bits;
    memcpy(&bits, &f, sizeof(bits));

    printf("0.1f 存进 float 的 32 个二进制位 (符号 | 指数 | 尾数):\n  ");
    for (int i = 31; i >= 0; i--) {
        printf("%d", (bits >> i) & 1);
        if (i == 31 || i == 23)
            printf(" ");
    }
    printf("\n\n");

    /* 尾数 1001100110011... 是无限循环，23 位放不下只能截断，所以不是 0.1 */
    printf("用不同精度打印它，都不是 0.1：\n");
    printf("  9 位精度 : %.9f\n", f);
    printf("  20 位精度: %.20f\n\n", f);

    /* 2) 经典的 0.1 + 0.2 != 0.3 在 double 下才看得见 */
    double a = 0.1, b = 0.2, c = 0.3;
    printf("double: 0.1 + 0.2 == 0.3 ? %s\n", (a + b == c) ? "yes" : "no");
    printf("  a + b = %.20f\n", a + b);
    printf("  c     = %.20f\n", c);

    return 0;
}
