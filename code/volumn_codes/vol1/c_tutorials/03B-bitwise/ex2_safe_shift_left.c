#include <stdint.h>
#include <stdio.h>

uint32_t safe_shift_left(uint32_t val, int n, int bits) {
    // bits 必须是合法位宽，n 必须落在 [0, bits) 内
    if (bits <= 0 || bits > 32 || n < 0 || n >= bits) {
        return 0;
    }
    return val << n;
}

int main(void) {
    // 演示:安全左移,非法移位量返回 0
    printf("1U << 4 (bits=32):  %u\n", safe_shift_left(1U, 4, 32));  // 合法: 16
    printf("1U << 32 (bits=32): %u\n", safe_shift_left(1U, 32, 32)); // 非法: 0
    printf("1U << -1 (bits=32): %u\n", safe_shift_left(1U, -1, 32)); // 非法: 0
    printf("1U << 8 (bits=0):   %u\n", safe_shift_left(1U, 8, 0));   // 非法: 0
    return 0;
}
