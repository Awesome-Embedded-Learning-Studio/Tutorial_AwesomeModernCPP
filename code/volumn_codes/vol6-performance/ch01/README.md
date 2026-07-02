# vol6 ch01 · Benchmark 方法论 — 代码示例

对应文章:`documents/vol6-performance/ch01-benchmark-methodology/02-credible-microbenchmark.md`

## push_bench

ch01-02 的最小完整 GBench 例子:测 `std::vector::push_back`,演示四件套——`DoNotOptimize`/`ClobberMemory` 防 DCE、`Range` 参数扫描、`Repetitions`+`ReportAggregatesOnly` 重复聚合、`UseRealTime` 墙钟计时。

### 构建(任选其一)

```bash
# 1) 系统装了 GBench(Arch: pacman -S benchmark;macOS: brew install google-benchmark)
g++ -O2 -std=c++17 push_bench.cpp -o push_bench -lbenchmark -lpthread
./push_bench

# 2) CMake + FetchContent(免预装,首次会 clone + build benchmark,几分钟)
cmake -B build && cmake --build build && ./build/push_bench
```

### 怎么读输出

- `Time` 是墙钟(`UseRealTime`)、`CPU` 是 CPU 时间;聚合行的 `Iterations` 列显示的是重复次数(3),不是每轮真实迭代数(被聚合隐藏了)。
- 盯 `cv`(coefficient of variation = `stddev/mean`):<1% 很稳;>5% 这轮测得不可信,查噪声源(ch01-03)。
- 时间随 N 涨才是 `push_back` 真实的样子;如果你测出来不随 N 变,多半是被 DCE 删成空壳了(缺 `DoNotOptimize`)。

### 一个会踩的坑

`BENCHMARK_ENABLE_TESTING OFF`(关 benchmark 自己的测试目标)flag 名是 `BENCHMARK_ENABLE_TESTING`,不是 `BENCHMARK_ENABLE_TESTS`。写错的话 `cmake --build` 会因为 benchmark 内部测试目标失败而整体非零退出,即便你的 `push_bench` 已经编过了——看输出里有没有 `Built target push_bench`,有就直接 `./build/push_bench` 跑。
