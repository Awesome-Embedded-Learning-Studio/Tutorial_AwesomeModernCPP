// Concepts 的四种语法形式:把约束写进模板参数、requires 子句、简写 auto、内嵌 requires 表达式
// 对应文章:documents/vol4-advanced/vol3-metaprogramming-cpp20-23/01-concepts.md
// 编译运行:g++ -Wall -Wextra -std=c++20 concepts_four_forms.cpp -o four_forms && ./four_forms
#include <concepts>
#include <iostream>
#include <string>

template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

// 形式①:把 concept 直接当约束写在模板参数列表里
template <Numeric T> T form1(T a, T b) {
    return a + b;
}

// 形式②:requires 子句(trailing requires-clause)
template <typename T>
    requires Numeric<T>
T form2(T a, T b) {
    return a + b;
}

// 形式③:简写模板语法(constrained auto)
auto form3(Numeric auto a, Numeric auto b) {
    return a + b;
}

// 形式④:模板参数列表后跟 requires,内层是一个 requires 表达式
template <typename T>
    requires requires(T x) { x + x; }
T form4(T a, T b) {
    return a + b;
}

int main() {
    std::cout << "form1: " << form1(3, 5) << "\n";
    std::cout << "form2: " << form2(2.0, 3.0) << "\n";
    std::cout << "form3: " << form3(10, 20) << "\n";
    std::cout << "form4: " << form4(std::string("a"), std::string("b")) << "\n";
    return 0;
}
