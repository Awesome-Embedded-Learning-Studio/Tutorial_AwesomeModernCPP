// branchless.cpp — vol6 ch04-06 分支:branchless 与 predication
// 不可预测分支的三种写法:if 分支 / std::min/max / 位运算 trick
// 编译: g++ -O2 -std=c++17 branchless.cpp -o branchless
// 跑:   taskset -c 0 ./branchless
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

using clk = std::chrono::steady_clock;
inline void do_not_optimize(uint64_t v) {
    asm volatile("" : "+r"(v)::"memory");
}

constexpr int N = 1 << 20;

// 场景:clamp(x, lo, hi),数据随机(分支不可预测)
template <class F> double time_ns(F&& f, int repeat) {
    f();
    f();
    auto t0 = clk::now();
    for (int r = 0; r < repeat; ++r)
        f();
    auto t1 = clk::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / repeat / N;
}

int main() {
    std::vector<int> v(N);
    for (int i = 0; i < N; ++i)
        v[i] = (int)(((unsigned)i * 1103515245u + 12345u) & 0xFFFF); // 伪随机 0..65535
    const int LO = 16384, HI = 49152; // 中段,使分支约 50/50(最难预测)
    const int REP = 50;
    volatile uint64_t sink = 0;

    // 1. if 分支(配合 -fno-if-conversion 保留真分支;否则编译器可能自动无分支化)
    double t_if = time_ns(
        [&] {
            uint64_t s = 0;
            for (int x : v) {
                if (x < LO)
                    x = LO;
                else if (x > HI)
                    x = HI;
                s += x;
            }
            sink += s;
        },
        REP);

    // 2. std::min/std::max(无分支写法,-O2 在循环里常被向量化成 SIMD 掩码)
    double t_cmov = time_ns(
        [&] {
            uint64_t s = 0;
            for (int& x : v) {
                x = std::max(x, LO);
                x = std::min(x, HI);
                s += x;
            }
            sink += s;
        },
        REP);

    // 3. 位运算 clamp(教科书 branchless)
    auto bit_clamp = [](int x, int lo, int hi) -> int {
        // 利用「无分支」条件传送的位 trick(编译器会进一步优化)
        int t = (x < lo) ? lo : x;
        return (t > hi) ? hi : t;
    };
    double t_bit = time_ns(
        [&] {
            uint64_t s = 0;
            for (int x : v)
                s += bit_clamp(x, LO, HI);
            sink += s;
        },
        REP);

    do_not_optimize(sink);
    std::printf("===== branchless vs 分支(clamp,随机数据 50/50)=====\n");
    std::printf("  if 分支:      %5.2f ns/次(预测失败冲刷)\n", t_if);
    std::printf("  std::min/max: %5.2f ns/次\n", t_cmov);
    std::printf("  位 trick:     %5.2f ns/次\n", t_bit);
    std::printf("  if / cmov = %.2fx\n", t_if / t_cmov);
    std::printf("  注:默认 -O2 下 GCC 常把循环里的 if 自动无分支化(本例 -S 下 cmov 数=0,是 SIMD "
                "掩码向量化),要看汇编确认\n");
    return 0;
}
