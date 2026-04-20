/**
 * @file ex09_simple_calculator.cpp
 * @brief 练习：简单计算器
 *
 * 用 switch 实现一个简单计算器：读取两个整数和一个运算符，
 * 输出运算结果。处理除数为零的情况。
 */

#include <iostream>

int main() {
    std::cout << "===== ex09: 简单计算器 =====\n\n";

    double a = 0.0;
    double b = 0.0;
    char op = '\0';

    std::cout << "请输入表达式（如 3 + 5）: ";
    std::cin >> a >> op >> b;

    double result = 0.0;
    bool valid = true;

    switch (op) {
        case '+':
            result = a + b;
            break;
        case '-':
            result = a - b;
            break;
        case '*':
            result = a * b;
            break;
        case '/':
            if (b == 0.0) {
                std::cout << "错误：除数不能为零\n";
                valid = false;
            } else {
                result = a / b;
            }
            break;
        default:
            std::cout << "错误：不支持的运算符 '" << op << "'\n";
            valid = false;
            break;
    }

    if (valid) {
        std::cout << a << " " << op << " " << b << " = " << result << "\n";
    }

    // 批量测试
    std::cout << "\n===== 批量测试 =====\n";

    struct TestCase {
        double lhs;
        char oper;
        double rhs;
    };

    TestCase tests[] = {
        {10, '+', 5},
        {10, '-', 3},
        {4,  '*', 7},
        {10, '/', 2},
        {10, '/', 0},
        {5,  '%', 2},
    };

    for (const auto& t : tests) {
        double r = 0.0;
        bool ok = true;

        switch (t.oper) {
            case '+': r = t.lhs + t.rhs; break;
            case '-': r = t.lhs - t.rhs; break;
            case '*': r = t.lhs * t.rhs; break;
            case '/':
                if (t.rhs == 0.0) {
                    std::cout << t.lhs << " / " << t.rhs
                              << " -> 错误：除数不能为零\n";
                    ok = false;
                } else {
                    r = t.lhs / t.rhs;
                }
                break;
            default:
                std::cout << t.lhs << " " << t.oper << " " << t.rhs
                          << " -> 错误：不支持的运算符\n";
                ok = false;
                break;
        }

        if (ok) {
            std::cout << t.lhs << " " << t.oper << " " << t.rhs
                      << " = " << r << "\n";
        }
    }

    return 0;
}
