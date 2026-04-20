// ex12_constexpr_popcount.cpp
// 练习：constexpr popcount
// 用 Brian Kernighan 技巧统计二进制中 1 的个数，编译期验证

#include <iostream>

constexpr int count_bits(int n)
{
    int count = 0;
    while (n != 0) {
        n &= (n - 1);  // 消除最低位的 1
        ++count;
    }
    return count;
}

// 编译期验证
static_assert(count_bits(0) == 0, "0 应该有 0 个 1");
static_assert(count_bits(7) == 3, "7 (0b111) 应该有 3 个 1");
static_assert(count_bits(255) == 8, "255 (0b11111111) 应该有 8 个 1");
static_assert(count_bits(1) == 1, "1 (0b1) 应该有 1 个 1");
static_assert(count_bits(1023) == 10, "1023 (0b1111111111) 应该有 10 个 1");

int main()
{
    std::cout << "=== constexpr popcount ===" << std::endl;

    int test_values[] = {0, 1, 7, 255, 1023, -1};
    for (int v : test_values) {
        std::cout << "count_bits(" << v << ") = " << count_bits(v) << std::endl;
    }

    std::cout << "\nstatic_assert 全部通过！" << std::endl;
    return 0;
}
