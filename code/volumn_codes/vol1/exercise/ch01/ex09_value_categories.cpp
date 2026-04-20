/**
 * @file ex09_value_categories.cpp
 * @brief 练习：分类判断 — 左值 vs 右值
 *
 * 演示左值和右值的区别：
 *   - 可以取地址的是左值（lvalue）
 *   - 不能取地址的是右值（rvalue）
 *
 * 演示引用绑定规则和悬垂引用的修复。
 */

#include <iostream>

// 修复前：返回局部变量的引用 — 悬垂引用！
// const std::string& get_prefix_bad() {
//     std::string prefix = "Hello";
//     return prefix;  // 危险！返回局部变量的引用
// }

// 修复后：按值返回
std::string get_prefix() {
    std::string prefix = "Hello";
    return prefix;  // 安全：返回值的副本（或被 NRVO 优化）
}

int main() {
    // ===== 1. 取地址判断左值/右值 =====
    std::cout << "===== 取地址判断 =====\n";

    int x = 42;
    int* ptr = &x;

    // x 是左值：可以取地址
    std::cout << "&x      = " << &x << "（x 是左值）\n";

    // *ptr 是左值：解引用返回左值
    std::cout << "&(*ptr) = " << &(*ptr) << "（*ptr 是左值）\n";

    // ++x 是左值：前缀自增返回左值引用
    std::cout << "&(++x)  = " << &(++x) << "（++x 是左值）\n";

    // x + 3 是右值：不能取地址
    // &(x + 3);  // 编译错误：不能取右值的地址

    // x++ 是右值：后缀自增返回右值（旧值的副本）
    // &(x++);    // 编译错误：不能取右值的地址

    // ===== 2. 引用绑定规则 =====
    std::cout << "\n===== 引用绑定规则 =====\n";

    int a = 10;

    // 左值引用绑定到左值 — OK
    int& r1 = a;
    std::cout << "int& r1 = a;       // OK, r1 = " << r1 << '\n';

    // 左值引用绑定到右值 — 编译错误
    // int& r2 = 10;  // 编译错误：非常量引用不能绑定到右值

    // const 左值引用绑定到右值 — OK（const 引用延长临时对象生命周期）
    const int& r3 = 10;
    std::cout << "const int& r3 = 10; // OK, r3 = " << r3 << '\n';

    // 右值引用绑定到右值 — OK
    int&& r4 = 10;
    std::cout << "int&& r4 = 10;      // OK, r4 = " << r4 << '\n';

    // 右值引用绑定到左值 — 编译错误
    // int&& r5 = a;  // 编译错误：右值引用不能绑定到左值

    // ===== 3. 悬垂引用的修复 =====
    std::cout << "\n===== 悬垂引用的修复 =====\n";

    // 正确方式：按值返回
    std::string result = get_prefix();
    std::cout << "get_prefix() = \"" << result << "\"\n";
    std::cout << "修复方法：函数按值返回而非返回引用\n";

    return 0;
}
