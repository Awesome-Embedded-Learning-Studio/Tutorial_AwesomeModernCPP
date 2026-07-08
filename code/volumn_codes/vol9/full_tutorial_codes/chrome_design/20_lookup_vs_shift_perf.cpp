// flat_map 性能:查找 O(log n) cache 友好 vs 插入 O(n) shift 的实测对比
// 来源:flat_map 实战(三)(六) + 前置知识(二)
// 编译:g++ -std=c++20 -Wall -Wextra -I. 20_lookup_vs_shift_perf.cpp -o 20_lookup_vs_shift_perf
// -pthread

#include "flat_map.hpp"

#include <chrono>
#include <iostream>
#include <map>
#include <vector>

int main() {
    using namespace tamcpp::chrome;
    constexpr int N = 100'000;

    std::cout << "=== 构造 ===\n";
    std::vector<std::pair<int, int>> raw;
    raw.reserve(N);
    for (int i = 0; i < N; ++i)
        raw.emplace_back(i, i * 2);

    auto t0 = std::chrono::steady_clock::now();
    flat_map<int, int> fm(raw.begin(), raw.end()); // flat_map 批量构造,O(N log N)
    auto t1 = std::chrono::steady_clock::now();
    std::map<int, int> sm(raw.begin(), raw.end()); // std::map 逐个插入,O(N log N)
    auto t2 = std::chrono::steady_clock::now();
    std::cout << "  flat_map 构造 " << N
              << " 元素: " << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
              << " ms\n";
    std::cout << "  std::map  构造 " << N
              << " 元素: " << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count()
              << " ms\n";

    std::cout << "\n=== 查找:flat_map vs std::map(同 O(log n),常数因子差)==\n";
    auto tf = std::chrono::steady_clock::now();
    volatile long acc1 = 0;
    for (int i = 0; i < N; ++i)
        acc1 += fm.find(i)->second;
    auto tf2 = std::chrono::steady_clock::now();
    volatile long acc2 = 0;
    for (int i = 0; i < N; ++i)
        acc2 += sm.find(i)->second;
    auto ts = std::chrono::steady_clock::now();
    std::cout << "  flat_map find x" << N << ": "
              << std::chrono::duration_cast<std::chrono::milliseconds>(tf2 - tf).count() << " ms\n";
    std::cout << "  std::map  find x" << N << ": "
              << std::chrono::duration_cast<std::chrono::milliseconds>(ts - tf2).count() << " ms\n";
    std::cout << "  (flat_map cache 友好通常更快)\n";

    std::cout << "\n=== 插入:flat_map O(n) shift vs std::map O(log n)==\n";
    auto ti = std::chrono::steady_clock::now();
    for (int i = N; i < N + 1000; ++i)
        fm.insert({i, i}); // 1000 次插入,每次 O(n) shift
    auto ti2 = std::chrono::steady_clock::now();
    for (int i = N; i < N + 1000; ++i)
        sm.insert({i, i}); // std::map O(log n)
    auto ts2 = std::chrono::steady_clock::now();
    std::cout << "  flat_map 插入 1000 次: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(ti2 - ti).count()
              << " ms (O(n) shift)\n";
    std::cout << "  std::map  插入 1000 次: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(ts2 - ti2).count()
              << " ms (O(log n))\n";
    std::cout << "  (查多写少 → flat_map;写多 → std::map)\n";
    return 0;
}
