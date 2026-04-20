/**
 * @file ex12_fix_dangling_ref.cpp
 * @brief 练习：修复悬空引用
 *
 * 演示并修复 get_max 函数中返回局部变量引用的 bug。
 * 局部变量在函数返回后即销毁，返回它的引用会产生悬空引用，
 * 访问悬空引用是未定义行为。
 */

#include <iostream>

// ===== 错误版本（注释掉，不可运行）=====
// int& get_max(int a, int b)
// {
//     int result = (a > b) ? a : b;  // result 是局部变量
//     return result;  // 严重错误！返回局部变量的引用
//     // 函数返回后 result 被销毁，引用指向的内存已无效
//     // 编译器通常会警告：warning: reference to local variable 'result' returned
// }

// ===== 正确版本：按值返回 =====
int get_max(int a, int b) {
    int result = (a > b) ? a : b;
    return result;  // 安全：返回值的副本
}

// ===== 另一种正确方案：通过引用参数输出 =====
void get_max_ref(int a, int b, int& out) {
    out = (a > b) ? a : b;
}

int main() {
    std::cout << "===== ex12: 修复悬空引用 =====\n\n";

    // 错误版本的调用（已注释）
    // int& m = get_max(3, 7);    // m 是悬空引用！
    // std::cout << m << "\n";    // 未定义行为

    // 正确版本 1：按值返回
    int m = get_max(3, 7);
    std::cout << "get_max(3, 7) = " << m << "\n";

    int m2 = get_max(42, 10);
    std::cout << "get_max(42, 10) = " << m2 << "\n";

    int m3 = get_max(-5, -3);
    std::cout << "get_max(-5, -3) = " << m3 << "\n";

    // 正确版本 2：通过引用参数输出
    std::cout << "\n===== 通过引用参数输出 =====\n";
    int result = 0;
    get_max_ref(100, 200, result);
    std::cout << "get_max_ref(100, 200, result) -> result = " << result << "\n";

    std::cout << "\n===== 修复要点 =====\n";
    std::cout << "Bug 原因：函数返回了局部变量 result 的引用\n";
    std::cout << "  - result 在函数返回后销毁\n";
    std::cout << "  - 返回的引用指向已不存在的对象（悬空引用）\n";
    std::cout << "  - 访问悬空引用是未定义行为\n";
    std::cout << "修复方法：\n";
    std::cout << "  1. 按值返回（return type 从 int& 改为 int）\n";
    std::cout << "  2. 通过引用参数输出结果\n";
    std::cout << "  3. 返回静态变量或全局变量的引用（需谨慎）\n";

    return 0;
}
