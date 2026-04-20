/**
 * @file ex06_const_pointer.cpp
 * @brief 练习：声明 const 指针并预测行为
 *
 * 演示三种 const 与指针的组合：
 *   1. const int* p1        — 指向常量的指针（不能通过指针修改值）
 *   2. int* const p2        — 常量指针（不能修改指针指向）
 *   3. const int* const p3  — 指向常量的常量指针（两者都不能改）
 *
 * 不能编译的操作用注释标出，附带解释。
 */

#include <iostream>

int main() {
    int a = 10;
    int b = 20;

    // ===== 1. const int* p1：指向常量的指针 =====
    std::cout << "===== const int* p1 =====\n";
    const int* p1 = &a;

    // 可以读取
    std::cout << "*p1 = " << *p1 << '\n';

    // 可以修改指针指向（让 p1 指向别的变量）
    p1 = &b;
    std::cout << "p1 指向 b 后，*p1 = " << *p1 << '\n';

    // 不能通过 p1 修改所指的值
    // *p1 = 30;  // 编译错误：不能给 const int 赋值

    // 但可以通过原始变量修改（a 本身不是 const）
    a = 15;
    p1 = &a;
    std::cout << "a 被修改后，*p1 = " << *p1 << '\n';

    // ===== 2. int* const p2：常量指针 =====
    std::cout << "\n===== int* const p2 =====\n";
    int* const p2 = &a;

    // 可以读取
    std::cout << "*p2 = " << *p2 << '\n';

    // 可以通过 p2 修改所指的值
    *p2 = 100;
    std::cout << "修改后 *p2 = " << *p2 << "，a = " << a << '\n';

    // 不能修改指针指向
    // p2 = &b;  // 编译错误：不能给 const 指针赋值

    // ===== 3. const int* const p3：指向常量的常量指针 =====
    std::cout << "\n===== const int* const p3 =====\n";
    const int* const p3 = &a;

    // 可以读取
    std::cout << "*p3 = " << *p3 << '\n';

    // 不能修改值
    // *p3 = 200;  // 编译错误

    // 不能修改指向
    // p3 = &b;    // 编译错误

    // ===== 记忆技巧 =====
    std::cout << "\n===== 记忆技巧 =====\n";
    std::cout << "看 const 在 * 的左边还是右边：\n";
    std::cout << "  const int* p    — const 修饰 int，值不可改\n";
    std::cout << "  int* const p    — const 修饰指针，指向不可改\n";
    std::cout << "  const int* const p — 两者都不可改\n";

    return 0;
}
