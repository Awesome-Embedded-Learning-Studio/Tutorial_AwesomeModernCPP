// 06_loops.cpp
// 循环语句综合演示：九九乘法表、金字塔图案、素数筛选

#include <iomanip>
#include <iostream>

void print_multiplication_table() {
    std::cout << "=== 九九乘法表 ===" << std::endl;
    for (int i = 1; i <= 9; ++i) {
        for (int j = 1; j <= i; ++j) {
            std::cout << j << "x" << i << "=" << std::setw(2) << i * j << " ";
        }
        std::cout << std::endl;
    }
}

void print_pyramid() {
    const int kHeight = 5;

    std::cout << "\n=== 金字塔图案 ===" << std::endl;
    for (int row = 1; row <= kHeight; ++row) {
        for (int space = 0; space < kHeight - row; ++space) {
            std::cout << " ";
        }
        for (int star = 0; star < 2 * row - 1; ++star) {
            std::cout << "*";
        }
        std::cout << std::endl;
    }
}

void print_primes_up_to(int n) {
    std::cout << "\n=== " << n << " 以内的素数 ===" << std::endl;
    for (int m = 2; m <= n; ++m) {
        bool is_prime = true;
        for (int d = 2; d * d <= m; ++d) {
            if (m % d == 0) {
                is_prime = false;
                break;
            }
        }
        if (is_prime) {
            std::cout << m << " ";
        }
    }
    std::cout << std::endl;
}

int main() {
    print_multiplication_table();
    print_pyramid();
    print_primes_up_to(50);

    return 0;
}
