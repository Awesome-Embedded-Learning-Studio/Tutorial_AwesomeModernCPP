/**
 * @file ex02_factorial.cpp
 * @brief 练习：计算阶乘
 *
 * 使用 for 循环计算 N!，同时展示 int 和 long long 的溢出边界。
 * int 最大可算到 12! = 479001600
 * long long 最大可算到 20! = 2432902008176640000
 */

#include <iostream>
#include <limits>

// 用 int 计算阶乘
int factorial_int(int n) {
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

// 用 long long 计算阶乘
long long factorial_long_long(int n) {
    long long result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

int main() {
    int n = 0;
    std::cout << "请输入一个非负整数 N: ";
    std::cin >> n;

    if (n < 0) {
        std::cout << "阶乘未定义负数\n";
        return 1;
    }

    // 展示类型范围
    std::cout << "\n===== 类型范围 =====\n";
    std::cout << "int 最大值:       " << std::numeric_limits<int>::max() << '\n';
    std::cout << "long long 最大值: " << std::numeric_limits<long long>::max()
              << '\n';

    // 计算并显示阶乘表
    std::cout << "\n===== 阶乘表 =====\n";
    std::cout << "n\tint 阶乘\t\tlong long 阶乘\n";
    std::cout << "------------------------------------------------\n";

    for (int i = 0; i <= n; ++i) {
        std::cout << i << "\t";

        // int 阶乘：超过 12! 会溢出
        if (i <= 12) {
            std::cout << factorial_int(i) << "\t\t\t";
        } else {
            std::cout << "(溢出)\t\t\t";
        }

        // long long 阶乘：超过 20! 会溢出
        if (i <= 20) {
            std::cout << factorial_long_long(i);
        } else {
            std::cout << "(溢出)";
        }
        std::cout << '\n';
    }

    // 计算用户输入的阶乘
    std::cout << "\n" << n << "! = ";
    if (n <= 20) {
        std::cout << factorial_long_long(n) << '\n';
    } else {
        std::cout << "超出 long long 范围，无法精确计算\n";
    }

    return 0;
}
