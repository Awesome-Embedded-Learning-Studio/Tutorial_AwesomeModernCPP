#include <iostream>

/// @brief 值传递——函数内部修改不影响外部
void add_one_by_value(int n) {
    n = n + 1; // 只修改了局部的拷贝
}

/// @brief 引用传递——函数内部直接修改外部变量
void add_one_by_ref(int& n) {
    n = n + 1; // 修改了原始变量
}

int main() {
    int a = 10;
    add_one_by_value(a);
    std::cout << a << "\n"; // 输出 10，没变

    add_one_by_ref(a);
    std::cout << a << "\n"; // 输出 11，变了

    return 0;
}
