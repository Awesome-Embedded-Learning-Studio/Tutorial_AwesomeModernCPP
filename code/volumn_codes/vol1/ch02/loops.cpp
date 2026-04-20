// loops.cpp -- 综合循环练习
// 编译: g++ -Wall -Wextra -o loops loops.cpp

#include <iostream>
#include <iomanip>

/// @brief 打印九九乘法表
void print_multiplication_table()
{
    std::cout << "=== 九九乘法表 ===" << std::endl;
    for (int i = 1; i <= 9; ++i) {
        for (int j = 1; j <= i; ++j) {
            std::cout << j << "x" << i << "=" << std::setw(2) << i * j << " ";
        }
        std::cout << std::endl;
    }
}

/// @brief 猜数字游戏，演示 while + break 的配合
void guess_number_game()
{
    const int kSecret = 42;
    int guess = 0;
    int attempts = 0;

    std::cout << "\n=== 猜数字游戏 ===" << std::endl;
    std::cout << "我想了一个 1-100 之间的数字，你来猜！" << std::endl;

    while (true) {
        std::cout << "你的猜测: ";
        std::cin >> guess;
        ++attempts;

        if (guess == kSecret) {
            std::cout << "恭喜！你用了 " << attempts << " 次猜中了！" << std::endl;
            break;
        } else if (guess < kSecret) {
            std::cout << "太小了，再试试。" << std::endl;
        } else {
            std::cout << "太大了，再试试。" << std::endl;
        }
    }
}

/// @brief 打印由星号组成的金字塔
void print_pyramid()
{
    const int kHeight = 5;

    std::cout << "\n=== 金字塔图案 ===" << std::endl;
    for (int row = 1; row <= kHeight; ++row) {
        // 打印前导空格
        for (int space = 0; space < kHeight - row; ++space) {
            std::cout << " ";
        }
        // 打印星号（第 row 行有 2*row - 1 个星号）
        for (int star = 0; star < 2 * row - 1; ++star) {
            std::cout << "*";
        }
        std::cout << std::endl;
    }
}

int main()
{
    print_multiplication_table();
    guess_number_game();
    print_pyramid();

    return 0;
}
