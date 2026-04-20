/**
 * @file ex08_ref_find_bugs.cpp
 * @brief 练习：找错 — 引用相关 bug
 *
 * 找出并修复四类常见的引用使用错误：
 *   1. 返回局部变量的引用
 *   2. 未初始化的引用
 *   3. 将地址赋给引用
 *   4. 将字面量传给非 const 引用
 */

#include <iostream>

// ---- Bug 1: 返回局部变量的引用 ----
// 错误写法（已注释）:
// int& buggy_get_ref() {
//     int local = 42;
//     return local;  // 返回局部变量的引用，悬空引用！
// }

// 正确写法：按值返回
int correct_get_value() {
    int local = 42;
    return local;  // 安全：按值返回会拷贝
}

// ---- Bug 2: 未初始化的引用 ----
void demo_uninitialized_ref() {
    std::cout << "Bug 2: 未初始化的引用\n";
    // 错误写法（编译错误）:
    // int& ref;  // 引用必须初始化！

    // 正确写法：引用必须在声明时绑定到一个对象
    int value = 10;
    int& ref = value;  // 正确：绑定到 value
    std::cout << "  ref = " << ref << " (绑定到 value)\n\n";
}

// ---- Bug 3: 将地址赋给引用 ----
void demo_address_to_ref() {
    std::cout << "Bug 3: 将地址赋给引用\n";
    int x = 10;
    // 错误写法（编译错误或隐式转换）:
    // int& ref = &x;  // &x 是 int*，不能赋给 int&

    // 正确写法：
    int& ref = x;       // 绑定到 x
    int* ptr = &x;      // 取地址用指针
    std::cout << "  ref = " << ref << ", *ptr = " << *ptr << "\n\n";
}

// ---- Bug 4: 将字面量传给非 const 引用 ----
// 错误写法：
// void buggy_increment(int& x) {
//     ++x;
// }
// buggy_increment(5);  // 错误！字面量不能绑定到非 const 引用

// 正确写法 1：使用 const 引用（如果不需要修改）
void print_value(const int& x) {
    std::cout << "  值: " << x << "\n";
}

// 正确写法 2：使用值传递（小类型推荐）
void safe_increment(int x) {
    std::cout << "  " << x << " + 1 = " << (x + 1) << "\n";
}

int main() {
    std::cout << "===== 引用相关 bug 修复 =====\n\n";

    // Bug 1 演示
    std::cout << "Bug 1: 返回局部变量的引用\n";
    int val = correct_get_value();
    std::cout << "  正确做法（按值返回）: " << val << "\n\n";

    // Bug 2 演示
    demo_uninitialized_ref();

    // Bug 3 演示
    demo_address_to_ref();

    // Bug 4 演示
    std::cout << "Bug 4: 字面量与非 const 引用\n";
    print_value(42);      // const 引用可以绑定到字面量
    safe_increment(5);    // 值传递可以接受字面量

    std::cout << "\n要点总结:\n";
    std::cout << "  1. 不要返回局部变量的引用\n";
    std::cout << "  2. 引用必须在声明时初始化\n";
    std::cout << "  3. 引用绑定对象，不是地址（地址用指针）\n";
    std::cout << "  4. 字面量只能绑定到 const 引用\n";

    return 0;
}
