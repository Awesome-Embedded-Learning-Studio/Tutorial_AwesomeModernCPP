/*
 * 验证 consteval + reinterpret_cast 无法编译
 *
 * C++ 标准 [expr.const] 明确规定 reinterpret_cast 不能出现在常量表达式求值中。
 * consteval 函数要求每次调用都是合法的常量表达式，因此 reinterpret_cast
 * 不能在 consteval 函数中使用。
 *
 * 预期结果：编译失败
 * 错误信息应包含类似内容：
 *   "reinterpret_cast is not allowed in a constant expression"
 *   或 "call to non-'constexpr' function"
 *
 * 编译命令：
 *   g++ -std=c++20 05-02-consteval-endian-broken.cpp -o broken
 *   clang++ -std=c++20 05-02-consteval-endian-broken.cpp -o broken
 */

#include <iostream>

// !! 这段代码无法编译 !!
// reinterpret_cast 不能出现在常量表达式中
consteval bool is_little_endian_broken() {
    constexpr int test = 1;
    return reinterpret_cast<const char*>(&test)[0] == 1;
    //     ^^^^^^^^^^^^^^^^^ 编译错误：reinterpret_cast is not allowed
    //                       in a constant expression
}

int main() {
    std::cout << "Little endian: " << is_little_endian_broken() << "\n";
    return 0;
}
