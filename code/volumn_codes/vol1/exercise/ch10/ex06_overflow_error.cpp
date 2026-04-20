/**
 * @file ex06_overflow_error.cpp
 * @brief 练习：整数溢出检测
 *
 * 定义 IntegerOverflow 异常类，实现 checked_divide，
 * 特别处理 INT_MIN / -1 的溢出情况。
 */

#include <climits>
#include <iostream>
#include <stdexcept>
#include <string>

/// @brief 整数溢出异常
class IntegerOverflow : public std::runtime_error {
private:
    int a_;
    int b_;

public:
    IntegerOverflow(int a, int b)
        : std::runtime_error(
              "integer overflow: " + std::to_string(a) + " / "
              + std::to_string(b)),
          a_(a),
          b_(b)
    {}

    int numerator() const { return a_; }
    int denominator() const { return b_; }
};

/// @brief 安全除法，检测溢出和除零
/// @param a  被除数
/// @param b  除数
/// @return a / b
/// @throw std::invalid_argument  当 b 为 0
/// @throw IntegerOverflow  当 INT_MIN / -1 导致溢出
int checked_divide(int a, int b)
{
    if (b == 0) {
        throw std::invalid_argument(
            "checked_divide: division by zero");
    }

    // INT_MIN / -1 在二补码下会溢出
    if (a == INT_MIN && b == -1) {
        throw IntegerOverflow(a, b);
    }

    return a / b;
}

int main()
{
    std::cout << "===== 整数溢出检测 =====\n\n";

    struct TestCase {
        int a;
        int b;
        bool should_throw;
        std::string desc;
    };

    TestCase tests[] = {
        {10, 3, false, "正常除法"},
        {-10, 2, false, "负数除法"},
        {0, 5, false, "零除以正数"},
        {100, 0, true, "除零"},
        {INT_MIN, -1, true, "INT_MIN / -1 溢出"},
        {INT_MIN, 1, false, "INT_MIN / 1 正常"},
        {INT_MAX, -1, false, "INT_MAX / -1 正常"},
    };

    for (const auto& test : tests) {
        std::cout << "  checked_divide(" << test.a << ", " << test.b << ")";
        if (!test.should_throw && test.a == INT_MIN) {
            std::cout << " (INT_MIN=" << INT_MIN << ")";
        }
        std::cout << " -- " << test.desc << "\n";

        try {
            int result = checked_divide(test.a, test.b);
            std::cout << "    结果: " << result << "\n";
        }
        catch (const IntegerOverflow& e) {
            std::cout << "    IntegerOverflow: " << e.what() << "\n";
        }
        catch (const std::invalid_argument& e) {
            std::cout << "    invalid_argument: " << e.what() << "\n";
        }
    }

    std::cout << "\n要点:\n";
    std::cout << "  1. 二补码下 INT_MIN / -1 的结果超出了 int 范围\n";
    std::cout << "  2. 自定义异常类可以携带更多上下文（操作数等）\n";
    std::cout << "  3. 安全的算术运算应在公共接口中主动检测边界\n";

    return 0;
}
