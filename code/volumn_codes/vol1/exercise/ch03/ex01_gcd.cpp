/**
 * @file ex01_gcd.cpp
 * @brief 练习：最大公约数
 *
 * 使用辗转相除法（欧几里得算法）实现 gcd(int a, int b)。
 * 测试用例：gcd(48, 18) = 6, gcd(100, 75) = 25, gcd(7, 3) = 1。
 */

#include <iostream>

// 辗转相除法求最大公约数
int gcd(int a, int b) {
    // 确保 a >= 0, b >= 0
    if (a < 0) a = -a;
    if (b < 0) b = -b;

    while (b != 0) {
        int remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

// 递归版本（更简洁）
int gcd_recursive(int a, int b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    if (b == 0) {
        return a;
    }
    return gcd_recursive(b, a % b);
}

int main() {
    // 测试用例
    struct TestCase {
        int a;
        int b;
        int expected;
    };

    TestCase tests[] = {
        {48, 18, 6},
        {100, 75, 25},
        {7, 3, 1},
        {0, 5, 5},
        {12, 12, 12},
        {17, 0, 17},
    };

    std::cout << "===== gcd 测试 =====\n\n";

    for (const auto& t : tests) {
        int result = gcd(t.a, t.b);
        int result_rec = gcd_recursive(t.a, t.b);
        bool pass = (result == t.expected);
        bool pass_rec = (result_rec == t.expected);

        std::cout << "gcd(" << t.a << ", " << t.b << ") = " << result
                  << " (预期 " << t.expected << ") "
                  << (pass ? "[PASS]" : "[FAIL]") << '\n';

        std::cout << "gcd_recursive(" << t.a << ", " << t.b << ") = "
                  << result_rec
                  << (pass_rec ? " [PASS]" : " [FAIL]") << '\n';
        std::cout << '\n';
    }

    // 运行时交互
    int a = 0, b = 0;
    std::cout << "请输入两个整数（用空格分隔）: ";
    std::cin >> a >> b;
    std::cout << "gcd(" << a << ", " << b << ") = " << gcd(a, b) << '\n';

    return 0;
}
