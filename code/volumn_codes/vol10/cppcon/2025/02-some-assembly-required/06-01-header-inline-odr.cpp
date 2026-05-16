/*
 * 验证：constexpr 隐式 inline，头文件中定义不违反 ODR
 *
 * 背景：文章 06 中提到 constexpr 函数可以放在头文件中，
 *       原因是 constexpr 隐式 inline（[dcl.constexpr]/1）
 *
 * 预期结果：多文件编译无链接错误，运行输出 value=26
 *
 * 编译命令：
 *   g++ -std=c++20 -Wall -Wextra -o /tmp/06-01 06-01-header-inline-odr.cpp
 *
 * 运行：
 *   /tmp/06-01
 *
 * 参考资料：
 *   - https://en.cppreference.com/cpp/language/constexpr
 *   - https://en.cppreference.com/cpp/language/inline
 *
 * 编译器：GCC 16.1.1
 */

#include <string>
#include <iostream>

// 模拟头文件中的 constexpr 函数（隐式 inline）
constexpr int square(int x) {
    return x * x;
}

// 模拟头文件中的 inline 函数
inline int add_one(int x) {
    return x + 1;
}

// 模拟头文件中的 inline 函数，组合上面两个
inline std::string describe(int x) {
    return "value=" + std::to_string(add_one(square(x)));
}

int main() {
    int input = 5;
    std::cout << describe(input) << std::endl;
    // square(5) = 25, add_one(25) = 26
    // 预期输出：value=26
    return 0;
}
