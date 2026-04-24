#include <cstdint>
#include <functional>
#include <iostream>

// 测试1：验证 auto vs std::function 的性能差异
void test_auto_vs_function_overhead()
{
    // 测试 auto 存储 lambda 的性能
    auto lambda_auto = [](int x) { return x * 2; };

    // 测试 std::function 存储 lambda 的性能
    std::function<int(int)> lambda_function = [](int x) { return x * 2; };

    // 简单的性能测试
    constexpr int kIterations = 1000000;

    // 测试 auto 版本
    std::int64_t sum_auto = 0;
    for (int i = 0; i < kIterations; ++i) {
        sum_auto += lambda_auto(i);
    }

    // 测试 std::function 版本
    std::int64_t sum_function = 0;
    for (int i = 0; i < kIterations; ++i) {
        sum_function += lambda_function(i);
    }

    std::cout << "Auto sum: " << sum_auto << "\n";
    std::cout << "Function sum: " << sum_function << "\n";
    std::cout << "sizeof(lambda_auto): " << sizeof(lambda_auto) << "\n";
    std::cout << "sizeof(lambda_function): " << sizeof(lambda_function) << "\n";
}

// 测试2：验证模板参数传递 lambda 是否真的内联
template<typename Func>
std::int64_t call_func(Func f, std::int64_t value)
{
    return f(value);
}

void test_template_inlining()
{
    auto lambda = [](std::int64_t x) { return x * 2; };

    constexpr int kIterations = 1000000;
    std::int64_t sum = 0;

    for (int i = 0; i < kIterations; ++i) {
        sum += call_func(lambda, static_cast<std::int64_t>(i));
    }

    std::cout << "Template inlining test sum: " << sum << "\n";
}

// 测试3：验证 std::function 的间接调用
void test_function_indirection()
{
    std::function<std::int64_t(std::int64_t)> f = [](std::int64_t x) { return x * 2; };

    constexpr int kIterations = 1000000;
    std::int64_t sum = 0;

    for (int i = 0; i < kIterations; ++i) {
        sum += f(static_cast<std::int64_t>(i));
    }

    std::cout << "std::function indirection test sum: " << sum << "\n";
}

// 测试4：泛型 lambda 的模板实例化
void test_generic_lambda_instantiation()
{
    auto add = [](auto a, auto b) { return a + b; };

    int xi = add(3, 4);
    double xd = add(3.5, 2.5);

    std::cout << "Generic lambda int: " << xi << "\n";
    std::cout << "Generic lambda double: " << xd << "\n";
}

int main()
{
    std::cout << "=== Lambda Performance Verification Tests ===\n\n";

    std::cout << "--- Test 1: auto vs std::function overhead ---\n";
    test_auto_vs_function_overhead();
    std::cout << "\n";

    std::cout << "--- Test 2: Template parameter inlining ---\n";
    test_template_inlining();
    std::cout << "\n";

    std::cout << "--- Test 3: std::function indirection ---\n";
    test_function_indirection();
    std::cout << "\n";

    std::cout << "--- Test 4: Generic lambda instantiation ---\n";
    test_generic_lambda_instantiation();
    std::cout << "\n";

    return 0;
}
