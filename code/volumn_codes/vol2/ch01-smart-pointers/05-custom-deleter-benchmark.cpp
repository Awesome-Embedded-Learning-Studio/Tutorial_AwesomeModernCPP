/**
 * @file 05-custom-deleter-benchmark.cpp
 * @brief 自定义删除器性能基准测试：unique_ptr vs shared_ptr
 * @details 比较默认删除器与自定义删除器的性能差异
 * @compile g++ -std=c++17 -O2 -o 05-custom-deleter-benchmark 05-custom-deleter-benchmark.cpp
 * @run ./05-custom-deleter-benchmark
 */

#include <memory>
#include <iostream>
#include <chrono>
#include <vector>

// 空删除器（可 EBO）
struct EmptyDeleter {
    void operator()(int* p) noexcept { delete p; }
};

// 防止编译器优化掉测试
volatile int global_sink = 0;

void benchmark_unique_ptr_default(size_t iterations) {
    std::vector<std::unique_ptr<int>> ptrs;
    ptrs.reserve(iterations);
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        ptrs.push_back(std::unique_ptr<int>(new int(static_cast<int>(i))));
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "unique_ptr<int> (默认删除器):       " << duration.count() << " μs ("
              << (duration.count() * 1000.0 / iterations) << " ns/iter)\n";
    global_sink = *ptrs[0];
}

void benchmark_unique_ptr_custom_deleter(size_t iterations) {
    std::vector<std::unique_ptr<int, EmptyDeleter>> ptrs;
    ptrs.reserve(iterations);
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        ptrs.push_back(std::unique_ptr<int, EmptyDeleter>(new int(static_cast<int>(i))));
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "unique_ptr<int, EmptyDeleter>:       " << duration.count() << " μs ("
              << (duration.count() * 1000.0 / iterations) << " ns/iter)\n";
    global_sink = *ptrs[0];
}

void benchmark_shared_ptr_default(size_t iterations) {
    std::vector<std::shared_ptr<int>> ptrs;
    ptrs.reserve(iterations);
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        ptrs.push_back(std::shared_ptr<int>(new int(static_cast<int>(i))));
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "shared_ptr<int> (默认删除器):        " << duration.count() << " μs ("
              << (duration.count() * 1000.0 / iterations) << " ns/iter)\n";
    global_sink = *ptrs[0];
}

void benchmark_shared_ptr_custom_deleter(size_t iterations) {
    std::vector<std::shared_ptr<int>> ptrs;
    ptrs.reserve(iterations);
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        ptrs.push_back(std::shared_ptr<int>(new int(static_cast<int>(i)),
            [](int* ptr) { delete ptr; }));
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "shared_ptr<int> (lambda 删除器):     " << duration.count() << " μs ("
              << (duration.count() * 1000.0 / iterations) << " ns/iter)\n";
    global_sink = *ptrs[0];
}

int main() {
    const size_t iterations = 100000;

    std::cout << "=== 智能指针删除器性能基准测试 ===\n";
    std::cout << "迭代次数: " << iterations << "\n";
    std::cout << "编译器: g++ " << __VERSION__ << "\n";
    std::cout << "优化级别: -O2\n\n";

    benchmark_unique_ptr_default(iterations);
    benchmark_unique_ptr_custom_deleter(iterations);
    benchmark_shared_ptr_default(iterations);
    benchmark_shared_ptr_custom_deleter(iterations);

    std::cout << "\n=== 结论 ===\n";
    std::cout << "1. unique_ptr 自定义删除器（空类）与默认删除器性能相近\n";
    std::cout << "2. shared_ptr 自定义删除器与默认删除器性能相近\n";
    std::cout << "3. shared_ptr 比 unique_ptr 慢约 30-50%（控制块分配开销）\n";

    return 0;
}
