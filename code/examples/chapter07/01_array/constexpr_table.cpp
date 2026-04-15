// constexpr_table.cpp - 编译期查表示例
#include <array>
#include <iostream>

// 编译期生成平方表
constexpr std::array<int, 10> make_square_table() {
    std::array<int, 10> table{};
    for (size_t i = 0; i < table.size(); ++i) {
        table[i] = static_cast<int>(i * i);
    }
    return table;
}

// 表在编译期计算，存储在只读段（若编译器支持）
constexpr auto square_table = make_square_table();

// 编译期断言 - 在编译期验证表内容
static_assert(square_table[0] == 0, "Square of 0 should be 0");
static_assert(square_table[5] == 25, "Square of 5 should be 25");
static_assert(square_table[9] == 81, "Square of 9 should be 81");

// 使用查表的函数（编译期优化后可能直接内联常量）
int get_square(int n) {
    if (n >= 0 && n < 10) {
        return square_table[n];
    }
    return -1;  // 错误值
}

int main() {
    std::cout << "Square table (computed at compile time):\n";
    for (size_t i = 0; i < square_table.size(); ++i) {
        std::cout << i << "^2 = " << square_table[i] << '\n';
    }

    std::cout << "\nLookup results:\n";
    for (int i = 0; i < 10; ++i) {
        std::cout << "get_square(" << i << ") = " << get_square(i) << '\n';
    }

    return 0;
}
