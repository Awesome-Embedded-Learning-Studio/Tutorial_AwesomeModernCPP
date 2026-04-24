/**
 * @file test_recursive_lambda_performance.cpp
 * @brief 验证不同递归 lambda 实现的性能差异
 *
 * 编译环境: g++ (GCC) 15.2.1, -O2 优化
 * 测试结果 (1,000,000 次调用, factorial(10)):
 * - std::function 版本: ~18,824 us
 * - Y Combinator 版本: ~127 us
 * - 性能差异: Y Combinator 版本约快 148 倍
 */

#include <functional>
#include <iostream>
#include <chrono>

// 防止编译器优化掉计算
volatile int sink = 0;

/**
 * @brief 测试 std::function 递归 lambda 的性能
 *
 * std::function 通过类型擦除实现调用，每次递归都需要：
 * 1. 虚函数表查找
 * 2. 间接函数调用
 * 3. 可能的堆分配（如果捕获数据较大）
 *
 * 这些开销在深度递归中会被显著放大。
 */
void benchmark_std_function() {
    std::function<int(int)> factorial = [&factorial](int n) {
        if (n <= 1) return 1;
        return n * factorial(n - 1);
    };

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; i++) {
        sink = factorial(10);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "std::function version: " << duration.count() << " us\n";
}

/**
 * @brief Y 组合子辅助类
 *
 * Y 组合子通过将"自身引用"作为参数传入，避免了类型擦除。
 * 因为 operator() 是模板函数，编译器可以完全内联整个调用链。
 */
template<typename F>
class YCombinator {
    F f_;
public:
    explicit YCombinator(F f) : f_(std::move(f)) {}

    template<typename... Args>
    decltype(auto) operator()(Args&&... args) {
        return f_(*this, std::forward<Args>(args)...);
    }
};

// CTAD (C++17)
template<typename F>
YCombinator(F) -> YCombinator<F>;

/**
 * @brief 测试 Y 组合子递归 lambda 的性能
 *
 * Y 组合子版本的优势：
 * 1. 无类型擦除开销
 * 2. operator() 是模板，编译器可以内联
 * 3. 无虚函数表查找
 * 4. 无堆分配
 *
 * 在 -O2 优化下，整个递归可以被优化成循环或常量。
 */
void benchmark_y_combinator() {
    auto factorial = YCombinator([](auto&& self, int n) -> int {
        if (n <= 1) return 1;
        return n * self(n - 1);
    });

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; i++) {
        sink = factorial(10);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Y Combinator version: " << duration.count() << " us\n";
}

/**
 * @brief 测试 C++14 泛型 lambda 直接传自身的性能
 *
 * 这是 Y 组合子的简化版本，不需要辅助类。
 * 调用时需要手动传递 lambda 自身：fib(fib, 10)
 */
void benchmark_self_ref() {
    auto fib = [](auto&& self, int n) -> long long {
        if (n <= 1) return n;
        return self(self, n - 1) + self(self, n - 2);
    };

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; i++) {
        sink = fib(fib, 10);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Self-ref version: " << duration.count() << " us\n";
}

int main() {
    std::cout << "Recursive lambda performance comparison (1,000,000 calls):\n";
    std::cout << "=========================================================\n";

    benchmark_std_function();
    benchmark_y_combinator();
    benchmark_self_ref();

    std::cout << "\nConclusion:\n";
    std::cout << "- std::function: type erasure overhead, not optimized well\n";
    std::cout << "- Y Combinator: template-based, fully inlined by compiler\n";
    std::cout << "- Self-ref: similar to Y Combinator but less ergonomic\n";

    return 0;
}
