/**
 * @file ex03_divmod.cpp
 * @brief 练习：用 struct 返回多个值
 *
 * 定义 DivResult 结构体包含商和余数，实现 divmod 函数。
 * 测试：divmod(17, 5) -> 3, 2; divmod(100, 7) -> 14, 2。
 */

#include <iostream>

// 用结构体封装多个返回值
struct DivResult {
    int quotient;   // 商
    int remainder;  // 余数
};

// 返回商和余数的函数
DivResult divmod(int dividend, int divisor) {
    DivResult result;
    result.quotient = dividend / divisor;
    result.remainder = dividend % divisor;
    return result;
}

// 打印 DivResult
void print_div_result(const DivResult& r) {
    std::cout << "商 = " << r.quotient << ", 余数 = " << r.remainder << '\n';
}

int main() {
    // 测试用例
    std::cout << "===== divmod 测试 =====\n\n";

    auto test = [](int a, int b, int expect_q, int expect_r) {
        DivResult r = divmod(a, b);
        bool pass_q = (r.quotient == expect_q);
        bool pass_r = (r.remainder == expect_r);
        std::cout << "divmod(" << a << ", " << b << ") -> ";
        print_div_result(r);
        std::cout << "  预期: 商=" << expect_q << ", 余数=" << expect_r
                  << " " << (pass_q && pass_r ? "[PASS]" : "[FAIL]") << '\n';
    };

    test(17, 5, 3, 2);
    test(100, 7, 14, 2);
    test(10, 3, 3, 1);
    test(20, 4, 5, 0);
    test(0, 5, 0, 0);

    // 验证关系：dividend == quotient * divisor + remainder
    std::cout << "\n===== 验证不变式 =====\n";
    int pairs[][2] = {{17, 5}, {100, 7}, {42, 13}, {99, 10}};
    for (auto& p : pairs) {
        DivResult r = divmod(p[0], p[1]);
        bool invariant = (p[0] == r.quotient * p[1] + r.remainder);
        std::cout << p[0] << " == " << r.quotient << " * " << p[1]
                  << " + " << r.remainder << " ? "
                  << (invariant ? "成立" : "不成立") << '\n';
    }

    return 0;
}
