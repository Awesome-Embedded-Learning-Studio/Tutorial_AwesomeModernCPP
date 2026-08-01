// 配套 01-if-constexpr.md「if constexpr:编译期分支」
// 核心:if constexpr 丢弃的分支不实例化,所以 auto 返回类型不会因为两个分支类型不同而冲突
// 编译运行:g++ -std=c++17 -Wall -Wextra if_vs_plain_if.cpp -o ipi && ./ipi
#include <iostream>
#include <string>
#include <type_traits>

// if constexpr:条件为假的分支直接丢弃,不参与实例化
template <typename T> auto double_it(T x) {
    if constexpr (std::is_integral_v<T>) {
        return x + x; // 整数走加法,返回 int
    } else {
        return x + " world"; // 别的类型走字符串拼接,返回 string
    }
}

int main() {
    std::cout << double_it(21) << "\n";                   // 42
    std::cout << double_it(std::string("hello")) << "\n"; // hello world
}
