// ex11_fibonacci_table.cpp
// 练习：编译期斐波那契查找表
// 用 constexpr 函数生成包含 30 个 Fibonacci 数的 std::array，用 static_assert 验证

#include <array>
#include <cstdint>
#include <iostream>

constexpr std::array<uint32_t, 30> make_fibonacci_table()
{
    std::array<uint32_t, 30> table{};
    table[0] = 0;
    table[1] = 1;
    for (std::size_t i = 2; i < table.size(); ++i) {
        table[i] = table[i - 1] + table[i - 2];
    }
    return table;
}

// 编译期生成斐波那契表
constexpr auto kFibTable = make_fibonacci_table();

// 编译期验证
static_assert(kFibTable[10] == 55, "Fibonacci(10) should be 55");
static_assert(kFibTable[20] == 6765, "Fibonacci(20) should be 6765");

int main()
{
    std::cout << "=== Fibonacci 查找表（前 30 项） ===" << std::endl;
    for (std::size_t i = 0; i < kFibTable.size(); ++i) {
        std::cout << "Fib(" << i << ") = " << kFibTable[i] << std::endl;
    }

    std::cout << "\n验证：" << std::endl;
    std::cout << "table[10] = " << kFibTable[10] << " (期望 55)" << std::endl;
    std::cout << "table[20] = " << kFibTable[20] << " (期望 6765)" << std::endl;

    return 0;
}
