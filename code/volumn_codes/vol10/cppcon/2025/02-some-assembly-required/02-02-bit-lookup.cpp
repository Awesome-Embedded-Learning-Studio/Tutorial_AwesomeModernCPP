/*
 * 验证：位查找表技巧的正确性
 *
 * 背景：文章中使用 uint64_t 位移来查找 ASCII 字符是否属于某个集合。
 * 原始代码在 uc >= 64 时存在未定义行为（C++ 标准 [expr.shift]：
 * 位移量不得大于等于位宽）。
 *
 * 修复：添加 uc < 64 的范围守卫。
 *
 * 预期结果：所有可打印 ASCII 字符的判断结果与朴素写法一致。
 *
 * 编译命令：
 *   g++ -std=c++20 -O2 -Wall -Wextra -o /tmp/bit_lookup /tmp/bit_lookup.cpp
 *
 * 运行：
 *   /tmp/bit_lookup
 *
 * 编译器：GCC 16.1.1
 */

#include <cstdint>
#include <cstdio>

// 手工构造一个查找表：只有 '0'-'9' 对应的位被置1
constexpr uint64_t make_digit_table() {
    uint64_t table = 0;
    for (int i = '0'; i <= '9'; ++i) {
        table |= (uint64_t{1} << i);
    }
    return table;
}

constexpr uint64_t kDigitTable = make_digit_table();

// 修复版：添加 uc < 64 守卫，避免位移 >= 64 的 UB
bool is_digit_bitlookup(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 64) return false;
    return (kDigitTable >> uc) & 1;
}

// 传统写法，作为对照
bool is_digit_naive(char c) {
    return c >= '0' && c <= '9';
}

int main() {
    // 测试所有可打印 ASCII 字符
    bool all_match = true;
    for (int i = 32; i < 127; ++i) {
        char c = static_cast<char>(i);
        if (is_digit_bitlookup(c) != is_digit_naive(c)) {
            printf("Mismatch at '%c' (ASCII %d): bitlookup=%d, naive=%d\n",
                   c, i, is_digit_bitlookup(c), is_digit_naive(c));
            all_match = false;
        }
    }
    if (all_match) {
        printf("All printable ASCII chars match!\n");
    }

    // 再测几个边界情况
    printf("'5' is digit: %d\n", is_digit_bitlookup('5'));
    printf("'a' is digit: %d\n", is_digit_bitlookup('a'));
    printf("NUL is digit: %d\n", is_digit_bitlookup('\0'));
    return 0;
}
