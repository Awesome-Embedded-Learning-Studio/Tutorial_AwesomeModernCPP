// lock_cost.cpp — vol6 ch05-03 锁的开销与「无锁不是银弹」
// 三种「单线程自增 N 次」的每操作成本:
//   A. 普通非原子 int(基线,无同步)
//   B. std::atomic(无锁,但每条指令有 memory_order 开销)
//   C. std::mutex 加解锁(无竞争 fast path)
// 编译: g++ -O2 -std=c++17 -pthread lock_cost.cpp -o lock_cost
// 跑:   ./lock_cost
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

using clk = std::chrono::steady_clock;
constexpr long N = 100'000'000;

int main() {
    // A. 非原子基线
    long plain = 0;
    auto t0 = clk::now();
    for (long i = 0; i < N; ++i)
        ++plain;
    auto t1 = clk::now();
    double t_plain = std::chrono::duration<double, std::nano>(t1 - t0).count() / N;

    // B. atomic relaxed(无锁)
    std::atomic<long> atom{0};
    t0 = clk::now();
    for (long i = 0; i < N; ++i)
        atom.fetch_add(1, std::memory_order_relaxed);
    t1 = clk::now();
    double t_atom = std::chrono::duration<double, std::nano>(t1 - t0).count() / N;

    // B2. atomic seq_cst(默认,更强内存序,更贵)
    std::atomic<long> atom_sc{0};
    t0 = clk::now();
    for (long i = 0; i < N; ++i)
        atom_sc.fetch_add(1); // 默认 seq_cst
    t1 = clk::now();
    double t_sc = std::chrono::duration<double, std::nano>(t1 - t0).count() / N;

    // C. mutex 无竞争
    std::mutex m;
    long guarded = 0;
    t0 = clk::now();
    for (long i = 0; i < N; ++i) {
        std::lock_guard<std::mutex> lk(m);
        ++guarded;
    }
    t1 = clk::now();
    double t_mutex = std::chrono::duration<double, std::nano>(t1 - t0).count() / N;

    volatile long sink = plain + atom.load() + atom_sc.load() + guarded;
    (void)sink;
    std::printf("===== 同步开销(单线程自增 %ld 次,ns/op)=====\n", N);
    std::printf("  非原子 int(基线):          %5.2f ns\n", t_plain);
    std::printf("  atomic relaxed(无锁,弱序): %5.2f ns\n", t_atom);
    std::printf("  atomic seq_cst(默认强序):   %5.2f ns\n", t_sc);
    std::printf("  mutex 无竞争(加解锁):      %5.2f ns\n", t_mutex);
    std::printf("  mutex/atomic_relaxed = %.1fx\n", t_mutex / t_atom);
    std::printf("  注:这是无竞争场景;有竞争时 mutex 排队/可能内核切换,代价暴涨。\n");
    return 0;
}
