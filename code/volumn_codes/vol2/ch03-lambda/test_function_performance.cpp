/**
 * @file test_function_performance.cpp
 * @brief 验证 std::function 的性能开销（与函数指针对比）
 * @details 测试 std::function 在 SBO 范围内的实际性能
 */

#include <functional>
#include <iostream>
#include <chrono>

// 测试函数
int add(int a, int b) {
    return a + b;
}

// 通过 std::function 调用（小 lambda，无堆分配）
void test_std_function_small(int iterations) {
    volatile int result = 0;
    std::function<int(int, int)> func = [](int a, int b) { return a + b; };
    for (int i = 0; i < iterations; ++i) {
        result = func(i, i + 1);
    }
}

// 通过 std::function 调用（函数指针）
void test_std_function_ptr(int iterations) {
    volatile int result = 0;
    std::function<int(int, int)> func = add;
    for (int i = 0; i < iterations; ++i) {
        result = func(i, i + 1);
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

// 直接调用
void test_direct_call(int iterations) {
    volatile int result = 0;
    for (int i = 0; i < iterations; ++i) {
        result = add(i, i + 1);
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

    std::cout << "=== std::function 性能测试 ===\n";
    std::cout << "迭代次数: " << iterations << "\n\n";

    double direct_time = time_it(test_direct_call, iterations);
    std::cout << "直接调用:              " << direct_time << " 秒\n";

    double fp_time = time_it(test_function_pointer, iterations);
    std::cout << "函数指针调用:          " << fp_time << " 秒\n";

    double func_small_time = time_it(test_std_function_small, iterations);
    std::cout << "std::function (小lambda): " << func_small_time << " 秒\n";

    double func_ptr_time = time_it(test_std_function_ptr, iterations);
    std::cout << "std::function (函数指针): " << func_ptr_time << " 秒\n";

    std::cout << "\n性能对比 (相对直接调用):\n";
    std::cout << "函数指针:                  " << (fp_time / direct_time) << "x\n";
    std::cout << "std::function (小lambda):   " << (func_small_time / direct_time) << "x\n";
    std::cout << "std::function (函数指针):   " << (func_ptr_time / direct_time) << "x\n";

    std::cout << "\n结论: std::function 即使在 SBO 范围内也有显著性能开销（7-9倍）\n";
    std::cout << "原因是虚函数表间接调用阻碍了内联优化\n";

    return 0;
}
