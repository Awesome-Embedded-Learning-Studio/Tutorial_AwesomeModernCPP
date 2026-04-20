/**
 * @file ex04_diamond.cpp
 * @brief 练习：打印菱形
 *
 * 输入奇数 N，打印边长为 N 的菱形图案。
 * 菱形由上半部分（含中间行）和下半部分组成。
 */

#include <iostream>

void print_diamond(int n) {
    int mid = n / 2;  // 中间行的索引

    // 上半部分（含中间行）
    for (int i = 0; i <= mid; ++i) {
        // 打印前导空格
        for (int j = 0; j < mid - i; ++j) {
            std::cout << ' ';
        }
        // 打印星号
        for (int j = 0; j < 2 * i + 1; ++j) {
            std::cout << '*';
        }
        std::cout << '\n';
    }

    // 下半部分
    for (int i = mid - 1; i >= 0; --i) {
        // 打印前导空格
        for (int j = 0; j < mid - i; ++j) {
            std::cout << ' ';
        }
        // 打印星号
        for (int j = 0; j < 2 * i + 1; ++j) {
            std::cout << '*';
        }
        std::cout << '\n';
    }
}

int main() {
    int n = 0;
    std::cout << "请输入一个奇数 N（菱形的边长）: ";
    std::cin >> n;

    if (n <= 0 || n % 2 == 0) {
        std::cout << "请输入一个正奇数\n";
        return 1;
    }

    std::cout << '\n';
    print_diamond(n);

    // 示例：N = 5 的输出
    //   *
    //  ***
    // *****
    //  ***
    //   *

    return 0;
}
