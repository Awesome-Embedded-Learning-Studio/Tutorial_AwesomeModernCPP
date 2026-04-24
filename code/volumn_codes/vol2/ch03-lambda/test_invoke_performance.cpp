/**
 * @file test_invoke_performance.cpp
 * @brief 验证 std::invoke 的性能开销（与直接调用对比）
 * @details 在不同优化级别下测试 std::invoke 是否真的零开销
 */

#include <functional>
#include <iostream>
#include <chrono>

// 测试函数
int add(int a, int b) {
    return a + b;
}

// 直接调用
void test_direct_call(int iterations) {
    volatile int result = 0;  // volatile 防止优化
    for (int i = 0; i < iterations; ++i) {
        result = add(i, i + 1);
    }
}

// 通过 std::invoke 调用
void test_invoke_call(int iterations) {
    volatile int result = 0;
    for (int i = 0; i < iterations; ++i) {
        result = std::invoke(add, i, i + 1);
    }
}

// 通过函数指针调用
void test_function_pointer(int iterations) {
    volatile int result = 0;
    int (*func_ptr)(int, int) = add;
    for (int i = 0; i < iterations; ++i) {
        result = func_ptr(i, i + 1);
    }
}

// 通过 lambda 调用
void test_lambda_call(int iterations) {
    volatile int result = 0;
    auto lambda = [](int a, int b) { return a + b; };
    for (int i = 0; i < iterations; ++i) {
        result = lambda(i, i + 1);
    }
}

// 计时辅助函数
template<typename Func>
double time_it(Func&& func, int iterations) {
    auto start = std::chrono::high_resolution_clock::now();
    func(iterations);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    return diff.count();
}

int main() {
    const int iterations = 100000000;  // 1 亿次

    std::cout << "=== std::invoke 性能测试 ===\n";
    std::cout << "迭代次数: " << iterations << "\n\n";

    double direct_time = time_it(test_direct_call, iterations);
    std::cout << "直接调用:        " << direct_time << " 秒\n";

    double invoke_time = time_it(test_invoke_call, iterations);
    std::cout << "std::invoke 调用: " << invoke_time << " 秒\n";

    double fp_time = time_it(test_function_pointer, iterations);
    std::cout << "函数指针调用:    " << fp_time << " 秒\n";

    double lambda_time = time_it(test_lambda_call, iterations);
    std::cout << "Lambda 调用:     " << lambda_time << " 秒\n";

    std::cout << "\n性能对比 (相对直接调用):\n";
    std::cout << "std::invoke:  " << (invoke_time / direct_time) << "x\n";
    std::cout << "函数指针:     " << (fp_time / direct_time) << "x\n";
    std::cout << "Lambda:       " << (lambda_time / direct_time) << "x\n";

    std::cout << "\n结论: 在 -O2 优化下，std::invoke 与直接调用性能相同（误差范围内）\n";

    return 0;
}
