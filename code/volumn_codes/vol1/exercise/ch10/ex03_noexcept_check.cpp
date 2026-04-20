/**
 * @file ex03_noexcept_check.cpp
 * @brief 练习：noexcept 检查
 *
 * safe_calc(int) noexcept 和 risky_calc(int)（负数时抛 invalid_argument）。
 * 用 noexcept() 运算符在运行时检查两者的 noexcept 属性。
 */

#include <iostream>
#include <stdexcept>

/// @brief 不抛异常的安全计算
/// @param x  输入值
/// @return x * 2 + 1
int safe_calc(int x) noexcept
{
    return x * 2 + 1;
}

/// @brief 可能抛异常的计算
/// @param x  输入值，必须非负
/// @return x * x
/// @throw std::invalid_argument  当 x < 0 时
int risky_calc(int x)
{
    if (x < 0) {
        throw std::invalid_argument(
            "risky_calc: negative input " + std::to_string(x));
    }
    return x * x;
}

int main()
{
    std::cout << "===== noexcept 检查 =====\n\n";

    // 用 noexcept() 运算符检查函数是否承诺不抛异常
    std::cout << "noexcept(safe_calc)  = " << std::boolalpha
              << noexcept(safe_calc(42)) << "\n";
    std::cout << "noexcept(risky_calc) = " << std::boolalpha
              << noexcept(risky_calc(42)) << "\n\n";

    // 调用 safe_calc —— 不会抛异常
    std::cout << "--- safe_calc ---\n";
    for (int i : {-3, 0, 7}) {
        std::cout << "  safe_calc(" << i << ") = " << safe_calc(i) << "\n";
    }

    // 调用 risky_calc —— 可能抛异常
    std::cout << "\n--- risky_calc ---\n";
    int inputs[] = {5, 0, -1, 3};

    for (int x : inputs) {
        try {
            int result = risky_calc(x);
            std::cout << "  risky_calc(" << x << ") = " << result << "\n";
        }
        catch (const std::invalid_argument& e) {
            std::cout << "  risky_calc(" << x << ") 抛出异常: " << e.what()
                      << "\n";
        }
    }

    std::cout << "\n要点:\n";
    std::cout << "  1. noexcept 声明是函数接口的一部分，影响调用方行为\n";
    std::cout << "  2. noexcept() 运算符在编译期可求值，用于模板和条件逻辑\n";
    std::cout << "  3. 如果 noexcept 函数意外抛异常，std::terminate 会被调用\n";

    return 0;
}
