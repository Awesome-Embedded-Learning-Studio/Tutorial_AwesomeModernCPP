// vector_vs_set.cpp —— vol6 ch00-01 命题验证
// 同为 O(log n) 的查找,缓存效应能差多少?
//
// 编译:g++ -O2 -std=c++17 vector_vs_set.cpp -o vector_vs_set
// 或:  cmake -B build && cmake --build build && ./build/vector_vs_set
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <set>
#include <vector>

using Clock = std::chrono::steady_clock;

/// 多轮取中位数,把离群值压下去(ch01 测量方法论的伏笔)
static double median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

int main() {
    constexpr int queries = 2'000'000;     // 每个 N 查 200 万次,摊薄单次噪声
    constexpr int trials = 5;              // 跑 5 轮取中位数
    volatile std::int64_t global_sink = 0; // 防止整段循环被死代码消除(DCE)

    printf("%-10s %18s %18s %10s\n", "N", "vector(ns/q)", "set(ns/q)", "set/vector");
    printf("------------------------------------------------------------\n");
    for (int N : {1024, 4096, 16384, 65536, 262144, 1048576}) {
        std::mt19937_64 rng(12345);
        std::vector<int> keys(N);
        for (int i = 0; i < N; ++i)
            keys[i] = i * 2; // 偶数、稀疏
        std::vector<int> sorted = keys;
        std::sort(sorted.begin(), sorted.end());      // vector 二分用
        std::set<int> sset(keys.begin(), keys.end()); // set 红黑树

        // 全部命中(查存在的 key),消除「找不到」走不同路径的偏差
        std::vector<int> toFind(queries);
        for (int i = 0; i < queries; ++i)
            toFind[i] = keys[rng() % N];

        std::vector<double> tv, ts;
        for (int t = 0; t < trials; ++t) {
            std::int64_t acc = 0;
            auto a = Clock::now();
            for (int q : toFind) {
                auto it = std::lower_bound(sorted.begin(), sorted.end(), q);
                acc += (it != sorted.end() && *it == q);
            }
            auto b = Clock::now();
            tv.push_back(std::chrono::duration<double, std::nano>(b - a).count() / queries);
            global_sink += acc;

            acc = 0;
            auto c = Clock::now();
            for (int q : toFind) {
                auto it = sset.find(q);
                acc += (it != sset.end());
            }
            auto d = Clock::now();
            ts.push_back(std::chrono::duration<double, std::nano>(d - c).count() / queries);
            global_sink += acc;
        }
        const double mv = median(tv), ms = median(ts);
        printf("%-10d %18.1f %18.1f %10.1fx\n", N, mv, ms, ms / mv);
    }
    printf("\nglobal_sink=%lld (防死代码消除)\n", (long long)global_sink);
}
