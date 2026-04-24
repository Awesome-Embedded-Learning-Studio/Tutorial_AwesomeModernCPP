/**
 * @file test_weak_ptr_performance.cpp
 * @brief 验证 weak_ptr::lock() 的性能开销
 *
 * 测试目标：
 * 1. 对比 weak_ptr::lock() 与直接访问 shared_ptr 的性能差异
 * 2. 测量 lock() 的实际开销
 */

#include <memory>
#include <iostream>
#include <chrono>
#include <vector>

// 简单的性能计时器
class Timer {
public:
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}

    double elapsed() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start_).count();
    }

private:
    std::chrono::high_resolution_clock::time_point start_;
};

// 测试 1: 直接访问 shared_ptr 的性能
void test_shared_ptr_direct_access(int iterations) {
    auto shared = std::make_shared<int>(42);

    Timer timer;
    volatile int sum = 0;  // volatile 防止编译器优化掉循环

    for (int i = 0; i < iterations; ++i) {
        sum += *shared;
    }

    double elapsed = timer.elapsed();
    std::cout << "直接访问 shared_ptr: " << elapsed << " ms, sum = " << sum << "\n";
}

// 测试 2: 通过 weak_ptr::lock() 访问的性能
void test_weak_ptr_lock_access(int iterations) {
    auto shared = std::make_shared<int>(42);
    std::weak_ptr<int> weak = shared;

    Timer timer;
    volatile int sum = 0;

    for (int i = 0; i < iterations; ++i) {
        if (auto locked = weak.lock()) {
            sum += *locked;
        }
    }

    double elapsed = timer.elapsed();
    std::cout << "通过 weak_ptr::lock() 访问: " << elapsed << " ms, sum = " << sum << "\n";
}

// 测试 3: 对比不同场景下的性能差异
void test_performance_comparison() {
    std::cout << "=== weak_ptr::lock() 性能测试 ===\n";
    std::cout << "编译环境: g++ (需确认版本)\n";
    std::cout << "优化级别: -O2 或 -O3 (需确认)\n\n";

    const int iterations = 10000000;  // 1000 万次迭代

    std::cout << "迭代次数: " << iterations << "\n\n";

    // 预热
    test_shared_ptr_direct_access(1000);
    test_weak_ptr_lock_access(1000);

    std::cout << "实际测试:\n";
    test_shared_ptr_direct_access(iterations);
    test_weak_ptr_lock_access(iterations);

    std::cout << "\n注意事项:\n";
    std::cout << "1. lock() 确实涉及原子操作，会增加开销\n";
    std::cout << "2. 在热路径中频繁调用 lock() 会带来可测量的性能影响\n";
    std::cout << "3. 但大多数应用场景下，这个开销是可以接受的\n";
    std::cout << "4. 如果性能确实成为瓶颈，应考虑重新设计所有权关系\n";
}

int main() {
    test_performance_comparison();
    return 0;
}
