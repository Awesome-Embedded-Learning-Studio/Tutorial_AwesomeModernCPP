/**
 * @file ex02_is_prime.cpp
 * @brief 练习：素数判断
 *
 * 实现 bool is_prime(int n)，只检查到 sqrt(n)。
 * 测试用例：2->true, 17->true, 18->false, 1->false。
 */

#include <cmath>
#include <iostream>

bool is_prime(int n) {
    if (n < 2) {
        return false;
    }
    if (n == 2) {
        return true;
    }
    if (n % 2 == 0) {
        return false;
    }
    int limit = static_cast<int>(std::sqrt(n));
    for (int i = 3; i <= limit; i += 2) {
        if (n % i == 0) {
            return false;
        }
    }
    return true;
}

int main() {
    // 测试用例
    struct TestCase {
        int n;
        bool expected;
    };

    TestCase tests[] = {
        {2, true},
        {17, true},
        {18, false},
        {1, false},
        {0, false},
        {-5, false},
        {3, true},
        {4, false},
        {97, true},
        {100, false},
    };

    std::cout << "===== is_prime 测试 =====\n\n";

    int passed = 0;
    int total = 0;
    for (const auto& t : tests) {
        bool result = is_prime(t.n);
        ++total;
        if (result == t.expected) {
            ++passed;
        }
        std::cout << "is_prime(" << t.n << ") = "
                  << (result ? "true" : "false")
                  << " (预期 " << (t.expected ? "true" : "false") << ") "
                  << (result == t.expected ? "[PASS]" : "[FAIL]") << '\n';
    }

    std::cout << "\n通过 " << passed << "/" << total << " 个测试\n";

    // 运行时交互
    std::cout << "\n请输入一个整数: ";
    int n = 0;
    std::cin >> n;
    std::cout << n << " 是" << (is_prime(n) ? "" : "不是") << "素数\n";

    return 0;
}
