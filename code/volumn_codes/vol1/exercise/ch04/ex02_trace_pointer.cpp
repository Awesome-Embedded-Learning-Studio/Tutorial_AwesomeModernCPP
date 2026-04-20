/**
 * @file ex02_trace_pointer.cpp
 * @brief 练习：追踪指针的值
 *
 * 逐步追踪以下代码中各变量的变化：
 *   int x = 10, y = 20;
 *   int* p = &x;  int* q = &y;
 *   *p = *q;      // 把 *q(20) 赋给 *p，x 变为 20
 *   p = q;        // p 也指向 y
 *   *p = 30;      // 通过 p 修改 y 为 30
 * 最后打印 x, y, *p, *q 的值。
 */

#include <iostream>

int main() {
    int x = 10;
    int y = 20;
    int* p = &x;
    int* q = &y;

    std::cout << "初始状态:\n";
    std::cout << "  x = " << x << ", y = " << y << "\n";
    std::cout << "  *p = " << *p << " (指向 x), *q = " << *q << " (指向 y)\n\n";

    // 第一步：*p = *q — 将 q 所指的值赋给 p 所指的变量
    *p = *q;
    std::cout << "执行 *p = *q 后:\n";
    std::cout << "  x = " << x << ", y = " << y << "\n";
    std::cout << "  *p = " << *p << ", *q = " << *q << "\n\n";

    // 第二步：p = q — 让 p 也指向 y
    p = q;
    std::cout << "执行 p = q 后:\n";
    std::cout << "  p 和 q 都指向 y\n";
    std::cout << "  *p = " << *p << ", *q = " << *q << "\n\n";

    // 第三步：*p = 30 — 通过 p 修改 y
    *p = 30;
    std::cout << "执行 *p = 30 后:\n";
    std::cout << "  x = " << x << ", y = " << y << "\n";
    std::cout << "  *p = " << *p << ", *q = " << *q << "\n\n";

    // 最终结果
    std::cout << "===== 最终结果 =====\n";
    std::cout << "x = " << x << "\n";  // 期望：20
    std::cout << "y = " << y << "\n";  // 期望：30
    std::cout << "*p = " << *p << "\n";  // 期望：30
    std::cout << "*q = " << *q << "\n";  // 期望：30

    return 0;
}
