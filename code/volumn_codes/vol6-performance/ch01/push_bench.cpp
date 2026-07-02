// push_bench.cpp —— vol6 ch01-02 最小完整 GBench 例子
// 测 std::vector::push_back,演示 DoNotOptimize/ClobberMemory + 参数扫描 + 重复聚合
//
// 构建(任选其一):
//   1) 系统装了 GBench(Arch: pacman -S benchmark):
//      g++ -O2 -std=c++17 push_bench.cpp -o push_bench -lbenchmark -lpthread
//   2) CMake + FetchContent(reader 免预装,见同目录 CMakeLists.txt):
//      cmake -B build && cmake --build build && ./build/push_bench
#include <benchmark/benchmark.h>
#include <vector>

// push_back 带 DoNotOptimize+ClobberMemory:防 DCE + 强制写落内存
static void BM_PushBack(benchmark::State& state) {
    for (auto _ : state) { // 计时循环:框架控制迭代次数
        std::vector<int> v;
        for (int i = 0; i < state.range(0); ++i) {
            v.push_back(i);
            benchmark::DoNotOptimize(v.data()); // 防 DCE + 内存 barrier
        }
        benchmark::ClobberMemory(); // 确保写真正落内存
    }
    state.SetComplexityN(state.range(0)); // 告诉框架 big-O 的 N,自动拟合
}

BENCHMARK(BM_PushBack)
    ->RangeMultiplier(2)
    ->Range(8, 8 << 6)            // 参数扫描:8,16,32,...,512
    ->UseRealTime()               // 报墙钟时间,不是 CPU 时间
    ->Repetitions(3)              // 跑 3 轮
    ->ReportAggregatesOnly(true); // 只报 mean/median/stddev/cv

BENCHMARK_MAIN();
