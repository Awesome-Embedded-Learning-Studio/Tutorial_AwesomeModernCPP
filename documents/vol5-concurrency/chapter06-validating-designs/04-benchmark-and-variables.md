---
title: "基准工具与三个变量：Benchmark、规模、粒度、线程数"
description: "Google Benchmark 的用法，数据规模、任务粒度与线程数量三个变量的实测读数，以及用 perf stat 读硬件计数器。"
chapter: 5
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 基准工具与三个变量：Benchmark、规模、粒度、线程数

条件立住了，该上工具。Google Benchmark 的用法、数据规模、任务粒度、线程数量，四个名字这一篇对着读，最后 perf stat 看硬件计数器。

## Google Benchmark 入门

### 装起来，跑一个最简单的基准

Google Benchmark，C++ 生态里最常用的微基准框架。咱们装它，有两条路：系统包，Debian 与 Ubuntu 的包名是 `libbenchmark-dev`，或者 CMake 的 FetchContent 直接拉源码进构建。FetchContent 的写法如下。版本号用 v1.10.0，2026 年 11 月 17 日查的，当时是 GitHub 发布页上的最新发布。您用的时候再核一次：

```cmake
cmake_minimum_required(VERSION 3.20)
project(concurrency_benchmarks CXX)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
set(BENCHMARK_ENABLE_TESTING OFF)   # 不让框架自己的测试跟着编译
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.10.0)
FetchContent_MakeAvailable(benchmark)

add_executable(bench_demo bench_demo.cpp)
target_link_libraries(bench_demo PRIVATE benchmark::benchmark)
```

您用它的方式是写一个接收 `benchmark::State&` 的函数，框架反复调用里面的循环，动态决定迭代次数，最后报平均。把咱们的求和交给它：

```cpp
#include <benchmark/benchmark.h>

static void bm_sum_range(benchmark::State& state) {
  const long long n = state.range(0);
  for (auto _ : state) {
    long long s = 0;
    for (long long i = 0; i < n; ++i) s += i % 7;
    benchmark::DoNotOptimize(s);
  }
}
BENCHMARK(bm_sum_range)->Arg(100000000)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
```

`for (auto _ : state)` 就是计时循环，循环体每跑一次记一次迭代，官方指南推荐优先写 ranged-for，因为传统的 `KeepRunning()` 每次迭代多一次内存读写。您装好跑起来，会看到一张三列表格，Time、CPU、Iterations。列的格式从 v1.10.0 用户指南来，读数换成了咱们刚才的基准，看个样子：

```text
-------------------------------------------------------
Benchmark                    Time       CPU      Iterations
-------------------------------------------------------
bm_sum_range/100000000     51.5 ms    51.5 ms           10
```

数值是示意，跟咱们实测的 51 毫秒对得上，迭代次数按默认的 0.5 秒下限折出来也是十次上下。Time 是墙钟时间，CPU 是 CPU 时间，Iterations 是实际跑的迭代次数。单线程基准两列时间几乎重合，多线程基准就分家了：CPU 时间是所有线程的总和，8 条线程跑 100 毫秒墙钟，CPU 时间可能是 700 毫秒。看哪一列，取决于您要回答什么问题。

### 多线程基准：Threads 与 UseRealTime

咱们测线程数量，框架给两条路。一条是托管给框架，注册时写 `Threads(8)` 或者 `ThreadRange(1, 8)`，后者按 2 的倍数一档档跑，1、2、4、8 各来一遍。框架保证所有线程都走到循环起点才开始计时，全部退出才结束，起跑线是齐的，并发实验代码里的闸门干的就是同一份活。另一条是函数内部自己建线程，跟咱们架子的做法一致。官方文档建议自己建线程的基准配上 `UseRealTime()`，因为框架默认量的是主线程的 CPU 时间，工作线程干活的墙钟时间它看不见，读数会歪得没边。

```cpp
static void bm_parallel_sum(benchmark::State& state) {
  const long long n = 100000000LL;
  const int chunks = 8;
  std::atomic<int> next{0};
  for (auto _ : state) {
    next.store(0);
    auto worker = [=, &next] {
      long long s = 0;
      for (;;) {
        int c = next.fetch_add(1, std::memory_order_relaxed);
        if (c >= chunks) break;
        long long from = c * n / chunks;
        long long to = (c + 1) * n / chunks;
        for (long long i = from; i < to; ++i) s += i % 7;
      }
      benchmark::DoNotOptimize(s);
    };
    std::vector<std::thread> pool;
    for (int i = 0; i < state.threads(); ++i) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
  }
}
BENCHMARK(bm_parallel_sum)->ThreadRange(1, 16)->UseRealTime();
```

`state.threads()` 把当前档位的线程数交给您，一份代码跑遍 1、2、4、8、16 五档，线程数量变量的读数就这么排出来。

### 咱们本篇用的计时架子

本篇的读数全部出自咱们手搓的计时架子。架子很小，全貌如下，概念跟框架对得上，您装好了库，随时可以换回去：

<OnlineCompilerDemo
  title="手搓计时架子：一次一轮，读数逐轮交出"
  source-path="code/examples/vol5/chapter06/14_timing_harness.cpp"
  description="do_not_optimize、clobber_memory 与 measure_ms 三件套，配一亿次求和连跑六轮的 main，每轮毫秒数原样打印，不做平均。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// timing_harness.cpp —— 本篇用的计时架子：一次一轮，读数逐轮交出
#include <chrono>
#include <cstdio>
#include <vector>

// 对应 benchmark::DoNotOptimize 的 v1.10.0 写法：非 const 引用，读写约束
template <class T>
inline void do_not_optimize(T& value) {
  asm volatile("" : "+m,r"(value) : : "memory");
}

// 官方 v1.10.0 已把 ClobberMemory 改成 atomic_signal_fence（只约束信号处理器那一路内存次序的栅栏），这里摆的是旧式空汇编写法
inline void clobber_memory() {
  asm volatile("" ::: "memory");
}

// 跑 reps 轮，交回每一轮的毫秒数
template <class F>
std::vector<double> measure_ms(F&& f, int reps) {
  using clock = std::chrono::steady_clock;
  std::vector<double> out;
  for (int r = 0; r < reps; ++r) {
    auto t0 = clock::now();
    f();
    auto t1 = clock::now();
    out.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  return out;
}

int main() {
  // 一亿次求和连跑六轮，每轮读数原样交出来（正文“预热与尾差”一节的实验）
  const long long n = 100000000LL;
  auto rounds = measure_ms([&] {
    long long s = 0;
    for (long long i = 0; i < n; ++i) s += i % 7;
    do_not_optimize(s);
  }, 6);
  for (int r = 0; r < (int)rounds.size(); ++r)
    std::printf("round=%d %.1fms\n", r, rounds[r]);
  return 0;
}
```

</OnlineCompilerDemo>

架子的计时循环一次一轮，多轮由调用方排，读数逐轮交出，不做平均，咱们挑它就是为了把每一轮的原始读数都拿到手。`do_not_optimize` 跟 `benchmark::DoNotOptimize` 对应，咱们照 v1.10.0 的写法摆的非 const 引用加读写约束。`clobber_memory` 对应 `benchmark::ClobberMemory`，官方在 v1.10.0 把它改成了 `atomic_signal_fence`，也就是只约束信号处理器那一路内存次序的栅栏，咱们摆的还是旧式空汇编写法。它没有框架的动态迭代，预热也干脆没有，换来的是每一轮读数都看得见，这正是本篇要的：原始读数列全，量级自己说话。

## 数据规模：收益从哪里开始

三个变量，咱们从数据规模动起。同一份求和活，8 条线程，规模从一百万加到一亿，把三轮读数收拢成一张表：

| 元素个数 | 串行（毫秒） | 8 线程（毫秒） | 加速比 |
|---|---|---|---|
| 100 万 | 0.50 - 0.54 | 0.23 - 0.60 | 0.91 - 2.17 倍 |
| 1000 万 | 5.14 - 5.68 | 1.31 - 1.36 | 3.78 - 4.17 倍 |
| 1 亿 | 51.22 - 51.68 | 11.60 - 12.80 | 4.00 - 4.46 倍 |

表里收的是三轮的区间。一百万档与一亿档的三轮原始读数，《量之前，先把条件立住》里已经列全，一千万档咱们补在这里：串行 5.14、5.68、5.26 毫秒，8 线程 1.36、1.36、1.31 毫秒，加速比 3.78、4.17、4.03 倍。

规模上去，加速比跟着上去，到 1000 万之后横住。一百万个元素的活不够 8 条线程分的，进场出场就把它吃光了。真正的并发负载里，规模对收益的影响比这更直接：数据大了，串行段（汇总、校验、写结果）的比例反而被摊薄，这也是 Gustafson 定律的出发点，《什么时候值得引入并发》的扩展节讲过。您给自己程序量规模的时候，起点就定在串行耗时与线程进场开销同量级的地方，从那里往上找收益的拐点。

## 任务粒度：切多细算合适

咱们把总工作量固定在两亿次加法，8 条线程，块数从 8 加到两千万，每档跑三轮：

```text
chunks=8        round=0 22.5ms
chunks=8        round=1 22.2ms
chunks=8        round=2 21.3ms
chunks=800      round=0 18.7ms
chunks=800      round=1 19.3ms
chunks=800      round=2 17.3ms
chunks=80000    round=0 18.2ms
chunks=80000    round=1 18.5ms
chunks=80000    round=2 18.3ms
chunks=20000000 round=0 257.6ms
chunks=20000000 round=1 258.7ms
chunks=20000000 round=2 262.0ms
```

咱们看读数，从 8 块到 800 块，耗时从 22 毫秒掉到 18 毫秒上下。8 块意味着每条线程开局领一块、跑完收工，一块是两千五百万次加法，谁分到的活重谁就得自己扛到底，别人帮不上。800 块让跑得快的线程多领几块，负载的波动被摊掉了。到 8 万块，读数没有再动：每块两千五百次加法，领一次任务的成本还只是零头。

两千万块把成本翻到了台面上。每块只剩 10 次加法，领任务用的 `fetch_add` 却一步没少，8 条线程反复抢同一个原子下标，抢到的活还不够付抢的力气，读数从 18 毫秒涨到 258 毫秒，慢了 14 倍。粒度的两头都有代价：太粗，负载不均时有人干等。太细，协调本身变成主角。咱们在 2.3 讲锁的粒度时见过同一件事的锁版本，判据也一样：看协调的成本跟活的成本在哪个规模上交叉。交叉点因活而异。把求和换成 perf stat 一节的争用负载，8 条线程抢一个原子变量，交叉点立刻挪位置，您亲手跑一遍就能看见数字倒过来。

## 线程数量：加到哪一步停手

线程数量是最后一个变量。咱们还是用两亿次加法，块数跟线程数走，一档一线程，每档三轮：

```text
threads=1  round=0 109.5ms
threads=1  round=1 102.6ms
threads=1  round=2 102.8ms
threads=2  round=0 72.6ms
threads=2  round=1 56.6ms
threads=2  round=2 57.7ms
threads=4  round=0 32.1ms
threads=4  round=1 32.5ms
threads=4  round=2 36.8ms
threads=8  round=0 23.8ms
threads=8  round=1 27.5ms
threads=8  round=2 23.3ms
threads=16 round=0 19.7ms
threads=16 round=1 20.5ms
threads=16 round=2 19.0ms
```

有一件事交代在前头：粒度一节的读数与上面的读数是分开两个时段跑的，同样的 8 条线程跑两亿次，两组数也对不齐，负载有漂，您比的时候看量级，别抠个位数。折成加速比，串行一轮 103 毫秒上下：2 条线程 1.8 倍，4 条 3.0 倍，8 条 4.2 倍，16 条 5.2 倍。每翻一倍，收益都在缩水。8 条线程之后还有两成，笔者机器的 16 个逻辑核是 8 个物理核加超线程，手里的纯算术活 ALU（算术逻辑单元，CPU 里做加减运算的部件）有富余，两个线程挤一条核反而把空隙填上了，这是候选解释，笔者没有做关闭超线程的对照实验，读数本身只说明一件事：在笔者机器上，加到 16 没有变慢。

咱们换一个活，判断就会反过来。下一节要用到的争用实验里，8 条线程抢一个原子变量，线程越多争用越凶，加速比掉到一倍以下，收益全部赔进去，争用版与独占槽位差 108 倍。所以您判断线程数量的时候，必须连着负载一起量，空口的推荐没有意义。

## perf stat：硬件计数器看什么（扩展）

本节带（扩展）标记，跳过它不影响单元主线，想弄明白快慢背后的原因，咱们接着往下走。

基准测出多快，硬件计数器能回答为什么。CPU 内部有一组计数器，数周期、数指令、数缓存未命中，Linux 的 `perf` 工具读它们，`perf stat` 把程序跑一遍，把一路的计数报出来。笔者机器上的 perf 是 7.3.1，硬件计数器可用，下面的读数是实测。WSL2 环境下 perf 能不能读硬件计数器跟内核配置有关，您的环境要是读不到，perf 会把计数器标成不支持，本节跟着思路走就好。

咱们给 perf 准备两个对照程序，都是 8 条线程各做 625 万次加一，总共五千万次。一个各写各的，每条线程的累加结果放进自己独占的 64 字节槽位，64 字节是常见缓存行的宽度，`alignas(64)` 把相邻线程的计数器隔开，谁也不会跟谁挤在同一条缓存行上（挤在一起的后果归 6.5《[怎样调整设计](05-adjusting-design)》，那里专门讲 false sharing）。另一个让全部线程抢同一个 `std::atomic<long long>`，用 relaxed 的 `fetch_add`，内存序的道理 5.4《[内存序的四档：从顺序一致性到 relaxed](../chapter05-atomic-memory-model/04-choosing-memory-orders)》讲过。写法上笔者把两个程序合在一个可执行文件里，用命令行参数切换，源码在下面：

<OnlineCompilerDemo
  title="perf stat 的两个对照：各写各的对抢同一个原子"
  source-path="code/examples/vol5/chapter06/15_exp_f_counter.cpp"
  description="8 条线程各 625 万次加一：不带参数（或 p）走独占 64 字节槽位，毫秒级。带 s 走抢同一个 atomic，慢两个量级。perf stat 的计数读数见正文表格。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_f_counter.cpp —— perf stat 的两个对照：各写各的对抢同一个 atomic
#include <atomic>
#include <cstdio>
#include <latch>
#include <thread>
#include <vector>

// 对应 benchmark::DoNotOptimize 的写法：非 const 引用，读写约束
template <class T>
inline void do_not_optimize(T& value) {
  asm volatile("" : "+m,r"(value) : : "memory");
}

struct alignas(64) Slot { long long v = 0; };

constexpr long long kPerThread = 6250000LL;   // 每线程 625 万次，八条共五千万
std::atomic<long long> shared{0};
std::vector<Slot> slots(8);
std::latch go{1};

int main(int argc, char** argv) {
  bool use_shared = (argc > 1 && argv[1][0] == 's');
  std::vector<std::thread> pool;
  for (int t = 0; t < 8; ++t) {
    pool.emplace_back([t, use_shared] {
      go.wait();                     // 八条线程在闸门口排队，起跑线齐
      long long s = 0;
      for (long long i = 0; i < kPerThread; ++i) {
        if (use_shared) shared.fetch_add(1, std::memory_order_relaxed);
        else { s += 1; do_not_optimize(s); }   // 不加它，编译器把整段循环折成常量
      }
      slots[t].v = s;                // 各写各的：累加结果落进自己的槽位
    });
  }
  go.count_down();                   // 放闸
  for (auto& t : pool) t.join();
  long long total = shared.load();
  for (const auto& slot : slots) total += slot.v;
  std::printf("mode=%s total=%lld\n", use_shared ? "shared" : "private", total);
  return 0;
}
```

</OnlineCompilerDemo>

把片段连同架子的头文件编成 `exp_f_counter`（`g++ -std=c++20 -O2 -pthread exp_f_counter.cpp -o exp_f_counter`，main 函数与线程收拢的部分按片段补全），命令行参数 p 走各写各的，s 走抢共享的。咱们跑起来，读数如下：

```bash
perf stat -e task-clock,context-switches,cpu-migrations,cycles,instructions,cache-references,cache-misses ./exp_f_counter p
perf stat -e task-clock,context-switches,cpu-migrations,cycles,instructions,cache-references,cache-misses ./exp_f_counter s
```

```text
 Performance counter stats for './exp_f_counter p':

             16.34 msec task-clock:u
                 0      context-switches:u
                 0      cpu-migrations:u
        66,838,682      cycles:u
       302,708,996      instructions:u
           106,416      cache-references:u
            24,790      cache-misses:u

       0.004508114 seconds time elapsed
```

```text
 Performance counter stats for './exp_f_counter s':

          3,787.52 msec task-clock:u
                 0      context-switches:u
                 0      cpu-migrations:u
    19,710,897,115      cycles:u
       357,147,024      instructions:u
        36,971,112      cache-references:u
        26,197,532      cache-misses:u

       0.485875850 seconds time elapsed
```

计数名后面的 `:u` 是 perf 的事件修饰符，表示只统计用户态发生的事件。咱们把两份读数放在一起算：

| 指标 | 各写各的 | 抢同一个 atomic | 差异 |
|---|---|---|---|
| 墙钟时间 | 4.5 毫秒 | 486 毫秒 | 108 倍 |
| 指令数 | 3.03 亿 | 3.57 亿 | 多 18% |
| 周期数 | 0.67 亿 | 197.1 亿 | 295 倍 |
| IPC | 4.5 | 0.018 | 250 倍 |
| 缓存未命中率 | 23.3% | 70.9% | 高出 47.6 个百分点 |

咱们把读数放在一起看，故事很完整。指令数只多 18%，说明两边干的活差不多。周期数差 295 倍，多出来的时间全花在等待上。IPC 是每周期指令数，4.5 说明各写各的版本流水线满负荷运转，0.018 说明争用版几乎每个周期都空转，8 条核排着队等同一条缓存行从别的核手里赶过来。缓存未命中率从 23.3% 跳到 70.9%，反复失效的，正是人人都在加一的共享计数器的缓存行。各写各的版本未命中率的基数其实很小，缓存引用只有十万次出头，两万多次未命中不构成负担。争用版不一样，缓存引用三千七百万次，未命中跟着涨到两千六百万次，三亿多条指令里，访存的大头全耗在抢同一条缓存行上。墙钟 108 倍、周期 295 倍之间的差距来自并行度：争用版 8 条线程全在自旋，CPU 时间烧了 3.79 秒，各写各的只烧了 16 毫秒。

有两项计数，咱们没有用上，但它们的读法值得您知道。上下文切换与 CPU 迁移在两边都是零：线程都在用户态自旋，没有阻塞，调度器没有理由搬动它们。要是您读数里的两项计数偏高，含义就换了：线程在频繁阻塞（等锁、等 I/O），或者线程数远超核数被来回换出。给线程绑核可以减少迁移，`taskset -c 0-7 ./your_program` 就是一条命令的事，本篇没有用到绑核的场面，笔者就不演示了。

perf 与基准框架也能联动。较新的 Google Benchmark 提供 `--benchmark_perf_counters` 参数（Linux，依赖 libpfm，一个读硬件计数器的库），把计数器直接并进基准报告，您跑一遍基准就能同时拿到时间与计数。更通用的做法还是上面的包法，`perf stat` 在外面把整个基准包一遍。

## 轮到您动手的时候

架子与全套实验都在手边，把自己的活交进去，几步就够：

1. 把您程序里最贵的一段活写成函数，给它配一个串行版本，两边用同一组编译选项。
2. 用 `measure_ms` 或者装好的 Google Benchmark 量三轮起步，原始读数全部记下，连同机器、核数、日期一起写进记录。
3. 三个变量挨个动：数据规模、任务块数、线程数，每次只动一个，动之前想好预期，动之后对着预期看读数。
4. 读数反常（比串行慢、随线程数上升、轮间跳动超过三成）的时候，用 perf stat 把周期与缓存未命中数拉出来，看看时间花在哪。

预期跟读数对不上的时候，别急着改设计，那正是您对程序的想象与机器的真实行为之间的缝隙。6.5《[怎样调整设计](05-adjusting-design)》讲怎么顺着缝隙调设计，入口见下一节。

## 下一步

翻页之前，请您把本篇的求和活换成您自己的最贵函数，跑出一条串行基线，再看三个变量里哪一个对收益影响最大。

## 参考资源

写本篇的时候，推进思路借鉴了两本书里讲测量的部分。一本是 Williams 的《C++ Concurrency in Action》，另一本是 Paul E. McKenney 的 perfbook（全名见下面的链接），讲测量与验证的思想。例子与文字都是笔者自己写的，读数在文中交代的机器上实测。Google Benchmark 的用法，笔者按 v1.10.0 用户指南核对。标准行为按 cppreference 核对。

- [Google Benchmark 用户指南][gb-guide]，`DoNotOptimize`、多线程基准与预热选项的出处，v1.10.0，2026-11-17 查证。
- [Google Benchmark 发布页][gb-releases]，FetchContent 用的版本号 v1.10.0 出自这里，2026-11-17 查证。
- [Is Parallel Programming Hard, And, If So, What Can You Do About It?（perfbook）][perfbook]，Paul E. McKenney 的免费在线书，kernel.org 镜像。
- [std::chrono::steady_clock（cppreference）][cppref-steady]，C++11 起的单调时钟，测量间隔用它。
- [std::latch（cppreference）][cppref-latch]，C++20 起的一次性闸门，并发实验代码用它齐起跑线。
- [std::atomic（cppreference）][cppref-atomic]，`fetch_add` 与内存序参数的出处。
- [perf stat 教程（perf wiki）][perf-wiki]，硬件计数器的官方教程。

[gb-guide]: https://github.com/google/benchmark/blob/main/docs/user_guide.md
[gb-releases]: https://github.com/google/benchmark/releases
[perfbook]: https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook-e1.html
[cppref-steady]: https://en.cppreference.com/w/cpp/chrono/steady_clock
[cppref-latch]: https://en.cppreference.com/w/cpp/thread/latch
[cppref-atomic]: https://en.cppreference.com/w/cpp/atomic/atomic
[perf-wiki]: https://perf.wiki.kernel.org/index.php/Tutorial#Counting_with_perf_stat
