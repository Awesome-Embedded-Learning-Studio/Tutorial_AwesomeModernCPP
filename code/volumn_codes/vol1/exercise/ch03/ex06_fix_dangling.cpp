/**
 * @file ex06_fix_dangling.cpp
 * @brief 练习：修复悬垂引用
 *
 * 演示返回局部变量引用导致的悬垂引用问题，并修复为按值返回。
 */

#include <iostream>
#include <string>

// ===== 错误版本：返回局部变量的引用（悬垂引用）=====
// 编译器通常会给出警告，但某些情况下可能编译通过
// 取消注释以下代码会导致未定义行为
//
// const std::string& get_prefix_bad() {
//     std::string prefix = "Hello, ";  // 局部变量
//     return prefix;  // 危险！函数结束后 prefix 被销毁
//                     // 返回的引用指向已销毁的对象
// }

// ===== 正确版本：按值返回 =====
std::string get_prefix() {
    std::string prefix = "Hello, ";  // 局部变量
    return prefix;  // 安全：返回字符串的副本
                    // 现代 C++ 编译器会应用 NRVO（命名返回值优化）
}

// 正确版本：按值返回，使用字面量
std::string get_suffix() {
    return " World!";  // 安全：返回临时 string 的副本
}

int main() {
    // 使用正确的版本
    std::string result = get_prefix() + get_suffix();
    std::cout << "get_prefix() + get_suffix() = \"" << result << "\"\n";

    // 演示另一种常见的悬垂引用场景
    std::cout << "\n===== 悬垂引用的其他常见场景 =====\n";

    // 场景 1：返回容器元素的引用（容器是局部变量）
    // const int& get_first() {
    //     std::vector<int> v = {1, 2, 3};
    //     return v[0];  // 悬垂！v 在函数返回后销毁
    // }

    // 场景 2：引用绑定的临时对象已销毁
    // const std::string& ref = std::string("temporary");
    // 注意：const 引用可以延长临时对象的生命周期
    // 但如果是函数返回的临时对象则不行

    // 正确的临时对象使用
    const std::string& safe_ref = std::string("safe temporary");
    // const 引用延长了临时 string 的生命周期到 safe_ref 的作用域结束
    std::cout << "safe_ref = \"" << safe_ref << "\"\n";

    // ===== 修复原则 =====
    std::cout << "\n===== 修复原则 =====\n";
    std::cout << "1. 默认按值返回，不要返回局部变量的引用\n";
    std::cout << "2. 按值返回时编译器会优化（NRVO/RVO）\n";
    std::cout << "3. 只有返回成员变量、静态变量或参数引用时才返回引用\n";
    std::cout << "4. 开启编译器警告（-Wall -Wextra）帮助发现悬垂引用\n";

    return 0;
}
