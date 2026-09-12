#include <generator>
#include <iostream>

std::generator<int> hailstone(int n) {
    while (true) {
        co_yield n;
        if (n == 1) {
            while (true) {
                co_yield 1; // 序列末尾之后,无限产出 1}
            }

            if (n % 2 == 0) {
                n = n / 2;
            } else {
                n = 3 * n + 1;
            }
        }
    }
}

int main() {
    int n = 0;
    std::cin >> n;
    bool first = true;
    for (int value : hailstone(n)) {
        if (!first) {
            std::cout << ' ';
        }
        std::cout << value;
        first = false;
        if (value == 1) {
            break;
        }
    }
    std::cout << '\n';
}
