// benchmark_methodology_verify.cpp
// 验证: 原文基准测试的方法论问题——构造时间污染了移动测量
// 原文的移动基准包含了 BigData 的构造开销，导致加速比被严重低估。
// 本程序分离构造和拷贝/移动，展示真实的性能差距。
// Standard: C++17
// g++ -std=c++17 -O2 -Wall -Wextra -o benchmark_methodology_verify benchmark_methodology_verify.cpp

#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>

class BigData
{
    std::vector<double> payload_;

public:
    explicit BigData(std::size_t n) : payload_(n)
    {
        std::iota(payload_.begin(), payload_.end(), 0.0);
    }

    BigData(const BigData& other) : payload_(other.payload_) {}
    BigData(BigData&& other) noexcept = default;
    BigData& operator=(const BigData&) = default;
    BigData& operator=(BigData&&) noexcept = default;
};

/// @brief 测量函数执行时间的辅助模板
template<typename Func>
double measure_ms(Func&& func, int iterations)
{
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        func();
    }
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int main()
{
    constexpr std::size_t kDataSize = 1000000;   // 100 万个 double，约 8MB
    constexpr int kIterations = 100;

    std::cout << "=== 基准测试方法论验证 ===\n";
    std::cout << "数据大小: " << kDataSize * sizeof(double) / 1024 << " KB\n";
    std::cout << "迭代次数: " << kIterations << "\n\n";

    // 仅构造（baseline）
    auto construct_time = measure_ms([&]() {
        BigData source(kDataSize);
        (void)source;
    }, kIterations);
    std::cout << "仅构造（baseline）: " << construct_time << " ms\n";

    // 构造 + 拷贝
    auto copy_time = measure_ms([&]() {
        BigData source(kDataSize);
        BigData copy = source;
        (void)copy;
    }, kIterations);
    std::cout << "构造 + 拷贝:        " << copy_time << " ms\n";

    // 构造 + 移动
    auto move_time = measure_ms([&]() {
        BigData source(kDataSize);
        BigData moved = std::move(source);
        (void)moved;
    }, kIterations);
    std::cout << "构造 + 移动:        " << move_time << " ms\n\n";

    // 分离真实开销
    double actual_copy = copy_time - construct_time;
    double actual_move = move_time - construct_time;

    std::cout << "=== 分离后的实际耗时 ===\n";
    std::cout << "纯拷贝耗时:  " << actual_copy << " ms\n";
    std::cout << "纯移动耗时:  " << actual_move << " ms\n";

    if (actual_move > 0.01) {
        std::cout << "真实加速比:  " << actual_copy / actual_move << "x\n";
    } else {
        std::cout << "移动耗时太小，在测量噪声范围内（接近零）\n";
        std::cout << "真实加速比: >> " << actual_copy << " / ~0 ms，"
                  << "数量级差距远超原文报告的 3.65x\n";
    }

    std::cout << "\n=== 原文基准测试分析 ===\n";
    std::cout << "原基准中，移动构造的 " << move_time << " ms 中，\n";
    std::cout << "构造占 " << construct_time << " ms ("
              << (construct_time / move_time * 100) << "%)。\n";
    std::cout << "真正的移动操作仅约 " << actual_move << " ms。\n";

    return 0;
}
