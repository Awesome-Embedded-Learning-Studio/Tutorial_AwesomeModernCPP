/**
 * @file ex01_pointer_swap.cpp
 * @brief 练习：手写 swap 并观察地址
 *
 * 声明两个 int 变量，打印交换前的值和地址，
 * 通过指针手动交换它们的值，再打印交换后的结果。
 */

#include <iostream>

// 通过指针交换两个 int 的值
void swap_by_pointer(int* a, int* b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

int main() {
    int x = 42;
    int y = 99;

    std::cout << "===== 交换前 =====\n";
    std::cout << "x = " << x << ", 地址: " << &x << "\n";
    std::cout << "y = " << y << ", 地址: " << &y << "\n";

    // 通过指针交换
    swap_by_pointer(&x, &y);

    std::cout << "\n===== 交换后 =====\n";
    std::cout << "x = " << x << ", 地址: " << &x << "\n";
    std::cout << "y = " << y << ", 地址: " << &y << "\n";

    // 要点：地址不变，值互换
    std::cout << "\n注意：交换后变量的地址没有变化，只是存储的值互换了。\n";

    return 0;
}
