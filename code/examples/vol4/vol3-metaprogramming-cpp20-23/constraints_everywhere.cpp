// concept 约束可以用在函数模板、类模板、成员函数、简写 auto 等多种位置
// 对应文章:documents/vol4-advanced/vol3-metaprogramming-cpp20-23/02-constraining-templates.md
// 编译运行:g++ -Wall -Wextra -std=c++20 constraints_everywhere.cpp -o ce && ./ce
#include <concepts>
#include <iostream>

template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

// ① 函数模板
template <Numeric T> T square(T x) {
    return x * x;
}

// ② 类模板:只对数值类型实例化
template <Numeric T> struct SafeNumber {
    T value;
    SafeNumber(T v) : value(v) {}
    // ③ 成员函数也能再加自己的约束
    SafeNumber& operator+=(Numeric auto other) {
        value += other;
        return *this;
    }
};

// ④ 简写语法:约束直接写在 auto 前
Numeric auto half(Numeric auto x) {
    return x / 2;
}

int main() {
    std::cout << "square(4) = " << square(4) << "\n";
    std::cout << "square(2.5) = " << square(2.5) << "\n";
    SafeNumber sn(3);
    sn += 4;
    std::cout << "SafeNumber(3) + 4 = " << sn.value << "\n";
    std::cout << "half(10) = " << half(10) << "\n";
    return 0;
}
