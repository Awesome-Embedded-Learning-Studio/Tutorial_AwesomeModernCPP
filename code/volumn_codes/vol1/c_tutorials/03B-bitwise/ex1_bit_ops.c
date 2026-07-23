#include <stdint.h>
#include <stdio.h>

/// @brief 将 value 的第 n 位置为 1
uint32_t bit_set(uint32_t value, int n) {
    return value | (1U << n);
}

/// @brief 将 value 的第 n 位清零
uint32_t bit_clear(uint32_t value, int n) {
    return value & ~(1U << n);
}

/// @brief 翻转 value 的第 n 位
uint32_t bit_toggle(uint32_t value, int n) {
    return value ^ (1U << n);
}

/// @brief 提取 value 的 [high:low] 位域（包含两端）
uint32_t bit_extract(uint32_t value, int high, int low) {
    uint32_t width = high - low + 1;
    value = value >> low;
    uint64_t mask = (1ULL << width) - 1;
    return value & mask;
}

int main(void) {
    // 演示:四大位操作 + 位域提取
    uint32_t v = 0x00;
    v = bit_set(v, 3);
    printf("置位第3位:  0x%08X\n", v); // 0x00000008

    v = bit_set(v, 0);
    printf("置位第0位:  0x%08X\n", v); // 0x00000009

    v = bit_clear(v, 3);
    printf("清零第3位:  0x%08X\n", v); // 0x00000001

    v = bit_toggle(v, 0);
    printf("翻转第0位:  0x%08X\n", v); // 0x00000000

    // 提取 0xABCD 的 [11:4] 位域
    uint32_t field = bit_extract(0xABCD, 11, 4);
    printf("0xABCD 的 [11:4] 位域: 0x%08X\n", field); // 0x000000BC
    return 0;
}
