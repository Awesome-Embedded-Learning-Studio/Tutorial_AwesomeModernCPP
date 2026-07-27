// 配套 04-tmp-core-techniques.md「模板递归:用实例化做循环」
// 演示经典 TMP 范式:模板递归 + 特化终止,在编译期算阶乘
// 编译运行:g++ -std=c++20 -Wall -Wextra tmp_factorial.cpp -o tf && ./tf
#include <iostream>

// 经典 TMP:模板递归计算阶乘,靠特化提供终止条件
template <unsigned N> struct Factorial {
    static constexpr unsigned value = N * Factorial<N - 1>::value;
};

template <> struct Factorial<0> {
    static constexpr unsigned value = 1;
};

int main() {
    std::cout << "Factorial<5>::value  = " << Factorial<5>::value << "\n";
    std::cout << "Factorial<10>::value = " << Factorial<10>::value << "\n";
    static_assert(Factorial<5>::value == 120, "5! should be 120");
    static_assert(Factorial<10>::value == 3628800, "10! should be 3628800");
    std::cout << "编译期断言全部通过\n";
}
