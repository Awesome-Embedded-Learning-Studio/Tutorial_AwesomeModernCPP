// scalability.cpp — vol6 ch05-02 扩展性曲线
// 同一个并行任务(分块累加),用 1/2/4/8 线程跑,看吞吐怎么随核数扩展。
// 理想:线性扩展(吞吐 ×N)。实际:受 Amdahl(串行段)+ 调度/同步开销限制。
// 编译: g++ -O2 -std=c++17 -pthread scalability.cpp -o scalability
// 跑:   ./scalability
// 注:WSL2 单 NUMA 节点,本程序不演示 NUMA 跨节点惩罚(本机也测不了)。
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using clk = std::chrono::steady_clock;
constexpr long N = 100'000'000; // 1 亿元素
static std::vector<int> data(N);

void partial_sum(int tid, int nthreads, long& out) {
    long chunk = N / nthreads;
    long beg = (long)tid * chunk;
    long end = (tid == nthreads - 1) ? N : beg + chunk;
    long s = 0;
    for (long i = beg; i < end; ++i)
        s += data[i];
    out = s;
}

double run_n(int nthreads) {
    std::vector<long> results(nthreads);
    std::vector<std::thread> ts;
    auto t0 = clk::now();
    for (int t = 0; t < nthreads; ++t)
        ts.emplace_back(partial_sum, t, nthreads, std::ref(results[t]));
    for (auto& t : ts)
        t.join();
    auto t1 = clk::now();
    // 防优化:汇总
    volatile long sink = 0;
    for (long r : results)
        sink += r;
    (void)sink;
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
    for (long i = 0; i < N; ++i)
        data[i] = (int)(i % 7);
    run_n(1); // warmup
    std::printf("===== 扩展性曲线(并行累加 %ld 元素)=====\n", N);
    std::printf("%-8s %12s %12s\n", "线程数", "耗时(ms)", "加速比");
    double t1 = 0;
    for (int n : {1, 2, 4, 8}) {
        // 取 3 次最小(更稳)
        double best = 1e9;
        for (int k = 0; k < 3; ++k)
            best = std::min(best, run_n(n));
        if (n == 1)
            t1 = best;
        std::printf("%-8d %10.1f %12.2fx\n", n, best, t1 / best);
    }
    std::printf("理想是线性扩展(加速比=N);实际受 Amdahl(汇总/同步)+ 调度开销限制。\n");
    return 0;
}
