/**
 * @file ex03_reserve_benchmark.cpp
 * @brief 练习：reserve 性能基准测试
 *
 * 对比 push_back 100000 个整数时，
 * 使用 reserve(100000) 与不使用 reserve 的时间差异。
 */

#include <chrono>
#include <iostream>
#include <vector>

/// @brief 不使用 reserve，逐个 push_back
/// @param n 要添加的元素数量
/// @return 耗时（毫秒）
double bench_without_reserve(int n)
{
    auto start = std::chrono::steady_clock::now();

    std::vector<int> vec;
    for (int i = 0; i < n; ++i) {
        vec.push_back(i);
    }

    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    return elapsed.count();
}

/// @brief 使用 reserve，逐个 push_back
/// @param n 要添加的元素数量
/// @return 耗时（毫秒）
double bench_with_reserve(int n)
{
    auto start = std::chrono::steady_clock::now();

    std::vector<int> vec;
    vec.reserve(n);
    for (int i = 0; i < n; ++i) {
        vec.push_back(i);
    }

    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    return elapsed.count();
}

int main()
{
    std::cout << "===== reserve 性能基准测试 =====\n\n";

    constexpr int kCount = 100000;

    // 不使用 reserve
    double time_no_reserve = bench_without_reserve(kCount);
    std::cout << "不使用 reserve: " << time_no_reserve << " ms\n";

    // 使用 reserve
    double time_with_reserve = bench_with_reserve(kCount);
    std::cout << "使用 reserve:   " << time_with_reserve << " ms\n\n";

    // 计算加速比
    double speedup = time_no_reserve / time_with_reserve;
    std::cout << "加速比: " << speedup << "x\n\n";

    std::cout << "要点:\n";
    std::cout << "  1. 不 reserve 时 vector 多次重新分配内存并拷贝\n";
    std::cout << "  2. reserve 一次性分配足够空间，避免重分配\n";
    std::cout << "  3. 当元素数量可预估时，应始终使用 reserve\n";

    return 0;
}
