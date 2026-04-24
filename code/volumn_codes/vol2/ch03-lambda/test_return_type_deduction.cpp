#include <iostream>

// 测试返回类型推导规则
void test_simple_return_deduction()
{
    // 单个 return 语句 - 应该能推导
    auto square = [](int x) { return x * x; };
    static_assert(std::is_same_v<decltype(square(5)), int>, "Should deduce int");

    // 显式返回类型
    auto divide = [](int a, int b) -> double {
        return static_cast<double>(a) / b;
    };
    static_assert(std::is_same_v<decltype(divide(10, 3)), double>, "Should be double");

    std::cout << "Simple return deduction: OK\n";
}

// 测试复杂返回类型推导
void test_complex_return_deduction()
{
    // 多个 return 语句，相同类型 - 应该能推导
    auto simple_multi = [](int x) -> int {
        if (x > 0) {
            return x * 2;
        } else if (x < 0) {
            return -x;
        }
        return 0;
    };
    static_assert(std::is_same_v<decltype(simple_multi(5)), int>, "Should deduce int");

    std::cout << "Complex return deduction: OK\n";
}

// 测试默认返回类型推导规则
void test_default_deduction_rules()
{
    // C++14: 如果有多个 return 语句，所有必须推导出相同类型
    auto uniform_returns = [](int x) {
        if (x > 0) {
            return x * 2;  // int
        }
        return x * 3;     // int - 相同类型，OK
    };

    // C++11: 多个 return 语句必须推导出相同类型
    auto cxx11_compatible = [](int x) -> int {
        if (x > 0) {
            return x * 2;
        }
        return 0;
    };

    std::cout << "Default deduction rules: OK\n";
}

int main()
{
    std::cout << "=== Lambda Return Type Deduction Tests ===\n\n";

    test_simple_return_deduction();
    test_complex_return_deduction();
    test_default_deduction_rules();

    std::cout << "\nAll deduction tests passed!\n";

    return 0;
}
