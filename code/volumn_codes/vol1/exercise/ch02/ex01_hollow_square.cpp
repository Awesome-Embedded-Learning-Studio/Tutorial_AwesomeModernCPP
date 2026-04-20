/**
 * @file ex01_hollow_square.cpp
 * @brief 练习：打印空心正方形
 *
 * 输入正整数 N，打印 N×N 的空心正方形图案。
 * 只有边缘打印 '*'，内部打印空格。
 */

#include <iostream>

void print_hollow_square(int n) {
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            // 第一行、最后一行、第一列、最后一列打印 '*'
            if (i == 0 || i == n - 1 || j == 0 || j == n - 1) {
                std::cout << "* ";
            } else {
                std::cout << "  ";
            }
        }
        std::cout << '\n';
    }
}

int main() {
    int n = 0;
    std::cout << "请输入正方形的边长 N: ";
    std::cin >> n;

    if (n <= 0) {
        std::cout << "边长必须为正整数\n";
        return 1;
    }

    std::cout << "\n" << n << "×" << n << " 空心正方形：\n\n";
    print_hollow_square(n);

    // 示例：N = 5 的输出
    // * * * * *
    // *       *
    // *       *
    // *       *
    // * * * * *

    return 0;
}
