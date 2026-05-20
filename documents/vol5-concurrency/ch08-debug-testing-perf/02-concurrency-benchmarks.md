---
title: "并发性能测试与基准"
description: "掌握 Google Benchmark 的使用方法，避开并发基准测试中的常见陷阱，学会用性能计数器定位瓶颈"
chapter: 8
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - atomic
  - mutex
  - 优化
  - 进阶
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard: [17, 20]
prerequisites:
  - "并发程序调试技巧"
  - "线程池"
related:
  - "CPU cache 与 OS 线程"
---

# 并发性能测试与基准

上一篇我们解决了正确性问题——用 TSan 抓 data race、用 Helgrind 查锁顺序、用 Clang TSA 在编译期预防线程安全违规。但是，一个正确的并发程序不等于一个高效的并发程序。笔者见过太多这样的场景：某人花了三天把一个 mutex 换成了无锁队列，兴奋地宣布"性能提升了 3 倍"，结果一看 benchmark 方法——单次运行、没有预热、编译器差点把整个循环优化没了、连 `UseRealTime` 都没加。你测出来的"3 倍提升"可能只是测量误差。

这一篇我们要解决的核心问题是：如何科学地测量并发程序的性能。我们会从 Google Benchmark 的基础用法开始，然后深入并发 benchmark 的设计陷阱（这里面坑多得超乎想象），再通过一个实战案例对比不同同步方案的真实性能差异，最后介绍 `perf stat` 这个 Linux 下的性能计数器工具——它能告诉你程序到底慢在哪里。

## Google Benchmark 基础

### 安装

Google Benchmark（下面简称 GBench）是 C++ 生态里最主流的微基准测试框架，由 Google 开源维护。安装方式有多种，最简单的是用 CMake 的 FetchContent：

```cmake
cmake_minimum_required(VERSION 3.20)
project(concurrency_benchmarks CXX)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.0
)
# 不让 GBench 自己跑测试，节省编译时间
set(BENCHMARK_ENABLE_TESTING OFF)
FetchContent_MakeAvailable(benchmark)
```

如果你更倾向于系统级安装：

```bash
# Ubuntu/Debian
sudo apt install libbenchmark-dev

# macOS
brew install google-benchmark

# vcpkg
vcpkg install benchmark
```

### 第一个 benchmark

GBench 的核心思路是：你写一个函数，框架会自动决定运行多少次迭代才能得到统计上可靠的结果。我们先写一个最简单的例子来熟悉它的 API：

```cpp
#include <benchmark/benchmark.h>
#include <vector>
#include <numeric>

// 一个简单的累加 benchmark
static void bm_vector_sum(benchmark::State& state)
{
    // Setup 阶段：不在计时范围内
    std::vector<int> data(10000, 42);

    // 计时循环：框架会反复执行这段代码
    for (auto _ : state) {
        int sum = std::accumulate(data.begin(), data.end(), 0);
        // 防止编译器优化掉 sum
        benchmark::DoNotOptimize(sum);
    }

    // 可选：报告额外信息
    state.SetItemsProcessed(state.iterations() * data.size());
}

BENCHMARK(bm_vector_sum);

BENCHMARK_MAIN();
```

编译运行：

```bash
clang++ -O2 -std=c++17 benchmark_demo.cpp -lbenchmark -lpthread -o demo
./demo
```

输出大概是这样的：

```text
-------------------------------------------------------
Benchmark             Time           CPU      Iterations
-------------------------------------------------------
bm_vector_sum      1234 ns       1234 ns       567890
```

每列的含义：`Time` 是墙钟时间（wall time），`CPU` 是 CPU 时间（即进程在 CPU 上实际花费的时间，包括用户态和内核态），`Iterations` 是框架跑了多少次迭代。对于单线程 benchmark，Time 和 CPU 应该非常接近；但对于多线程 benchmark，CPU 时间会是所有线程 CPU 时间的总和——这就是为什么我们需要 `UseRealTime`。

### 多线程 benchmark

GBench 原生支持多线程测试。通过 `Threads(n)` 指定线程数，或者用 `ThreadRange` 自动遍历不同的线程数：

```cpp
#include <benchmark/benchmark.h>
#include <atomic>

// 一个 atomic 计数器的多线程 benchmark
static void bm_atomic_counter(benchmark::State& state)
{
    std::atomic<int> counter{0};
    const int num_threads = state.threads();

    for (auto _ : state) {
        // 每个线程做一次原子递增
        counter.fetch_add(1, std::memory_order_relaxed);
        benchmark::ClobberMemory();
    }

    // 使用墙钟时间，否则多线程下 CPU 时间是累加的
    state.SetItemsProcessed(state.iterations());
}

// 测试 1/2/4/8 线程
BENCHMARK(bm_atomic_counter)
    ->ThreadRange(1, 8)
    ->UseRealTime();

BENCHMARK_MAIN();
```

这里有几个关键点需要说明。`ThreadRange(1, 8)` 会让框架分别用 1、2、4、8 个线程来跑这个 benchmark（2 的幂次）。`UseRealTime()` 非常重要——没有它的话，框架默认报告 CPU 时间，多线程下 CPU 时间是所有线程时间之和。比如 4 个线程跑了 100ms 墙钟时间，CPU 时间可能是 350ms（因为有等待和调度开销），如果报告 CPU 时间你会觉得"更慢了"——这完全是误导。`ClobberMemory()` 是一个编译器层面的内存屏障，告诉编译器"不要缓存任何内存状态"，防止优化器把我们的原子操作优化掉。

输出会类似：

```text
------------------------------------------------------------------
Benchmark                        Time          CPU      Iterations
------------------------------------------------------------------
bm_atomic_counter/1           2.3 ns       2.3 ns    300000000
bm_atomic_counter/2           1.8 ns       3.5 ns    400000000
bm_atomic_counter/4           2.1 ns       7.8 ns    333333333
bm_atomic_counter/8           3.5 ns       25 ns     200000000
```

注意看 CPU 列：线程越多，总 CPU 时间越高，但墙钟时间（Time 列）并不是线性下降的——从 1 线程到 2 线程有一定加速，但到 4 和 8 线程反而变慢了。这是因为所有线程都在对同一个原子变量执行写操作，缓存行在核心间来回失效（这和 false sharing 的机制类似，但严格来说是 true sharing 下的缓存行争用）。这是并发性能分析中一个非常典型的模式：不是线程越多越快。

## 并发 benchmark 设计陷阱

写一个正确的 benchmark 比写一个正确的并发程序还难——因为你要对抗编译器的优化、CPU 的缓存行为、操作系统的调度策略。这些因素在单线程 benchmark 里就会搞事情，在多线程 benchmark 里更加变本加厉。

### 预热：冷启动与稳态

CPU 的缓存层次结构（L1、L2、L3）对性能的影响是数量级的。第一次访问一个数据，它可能要从主存（DRAM）加载，需要 100-300 个 CPU 周期；第二次访问，它已经在 L1 缓存里了，只需要 3-4 个周期。如果你的 benchmark 不做预热，第一次迭代的数据加载会严重拉高平均时间。

GBench 的 `KeepRunning()` 循环本身会做一定程度的预热——框架会先跑少量迭代来"稳定"结果。但如果你在循环外分配了大块内存，这些内存在第一次迭代时可能不在缓存里。如果你的目标是测量"稳态"性能，可以在循环前手动跑几次：

```cpp
static void bm_with_warmup(benchmark::State& state)
{
    std::vector<int> data(10000);

    // 预热：让数据进入缓存
    for (int i = 0; i < 100; ++i) {
        volatile int dummy = data[0];
        (void)dummy;
    }

    for (auto _ : state) {
        int sum = 0;
        for (int v : data) {
            sum += v;
        }
        benchmark::DoNotOptimize(sum);
    }
}
```

但反过来——如果你想测量的是"冷启动"性能（比如某个操作的首次执行延迟），那就不应该预热。关键是要清楚你在测什么。

### 编译器优化：你的对手

这是最容易中招的陷阱。编译器的任务是把你的代码变快——但你的目标是测量代码的原始速度。如果编译器发现你的计算结果没有被使用，它可能直接把整个循环优化掉。如果编译器发现你每次循环都在做同样的计算，它可能把循环提到循环外只算一次。

GBench 提供了两个关键工具来对抗这些问题：

```cpp
// benchmark::DoNotOptimize(expr)
// 告诉编译器 expr 的值"可能"被外部使用，不要优化掉
benchmark::DoNotOptimize(result);

// benchmark::ClobberMemory()
// 告诉编译器所有内存状态都可能被外部修改
// 相当于一个全局的读写屏障
benchmark::ClobberMemory();
```

一个实用的模式是把它们配合使用：

```cpp
for (auto _ : state) {
    int result = expensive_computation();
    benchmark::DoNotOptimize(result);
    benchmark::ClobberMemory();
}
```

`DoNotOptimize` 保证 `result` 不会被优化掉，`ClobberMemory` 保证每次循环的内存读取不会被优化为"上一次已经读过了直接复用"。但注意不要滥用 `ClobberMemory`——它会告诉编译器所有内存都可能被修改，编译器因此必须保守地重新加载所有被缓存到寄存器中的值，这在某些场景下会引入额外的访存开销，让你测到的性能比实际情况更差。

### false sharing：看不见的性能杀手

False sharing 是并发性能杀手——两个线程各自修改不同的变量，但这些变量恰好在同一个缓存行（通常 64 字节）上，导致每次写入都要让另一个核心的缓存行失效。让我们用 benchmark 来直观感受一下它的威力：

```cpp
#include <benchmark/benchmark.h>
#include <vector>
#include <thread>

// 有 false sharing 的版本
struct alignas(64) PaddedCounter {
    int value{0};
    // padding 到 64 字节，避免 false sharing
    char padding[60];
};

static void bm_false_sharing(benchmark::State& state)
{
    const int num_threads = state.threads();

    // 故意把计数器紧密排列——制造 false sharing
    auto* counters = new int[num_threads]();

    for (auto _ : state) {
        int idx = state.thread_index();
        counters[idx]++;
        benchmark::ClobberMemory();
    }

    delete[] counters;
}

static void bm_no_false_sharing(benchmark::State& state)
{
    const int num_threads = state.threads();

    // 每个计数器独占一个缓存行
    auto* counters = new PaddedCounter[num_threads];

    for (auto _ : state) {
        int idx = state.thread_index();
        counters[idx].value++;
        benchmark::ClobberMemory();
    }

    delete[] counters;
}

// 在多线程下对比
BENCHMARK(bm_false_sharing)->ThreadRange(2, 16)->UseRealTime();
BENCHMARK(bm_no_false_sharing)->ThreadRange(2, 16)->UseRealTime();

BENCHMARK_MAIN();
```

编译运行后，你会看到类似这样的结果（具体数字取决于你的 CPU）：

```text
-------------------------------------------------------------------
Benchmark                         Time           CPU    Iterations
-------------------------------------------------------------------
bm_false_sharing/2             8.5 ns       17 ns     82352941
bm_false_sharing/4             15 ns        58 ns     47058823
bm_false_sharing/8             28 ns       210 ns     25000000
bm_no_false_sharing/2          3.2 ns       6.4 ns    218750000
bm_no_false_sharing/4          3.4 ns       13 ns     205882352
bm_no_false_sharing/8          3.6 ns       28 ns     194444444
```

没有 padding 的版本，线程越多越慢——因为每个核心每次写入都要把其他核心的缓存行踢掉，缓存一致性协议（MESI）的开销随线程数超线性增长（粗略为 O(n²)，因为每次写入需要通知其他 n-1 个核心）。加了 padding 之后，每个计数器独占一个缓存行，线程之间互不干扰，性能几乎不随线程数变化。这个差异在 8 线程时可以达到接近 8 倍——这就是 false sharing 的真实杀伤力。

### 线程创建：别在循环里造线程

不要在 benchmark 循环里创建和销毁线程。线程创建是一个昂贵的操作——内核需要为它分配栈空间、初始化线程控制块、把它注册到调度器——在 Linux 上通常需要 50-200 微秒。如果你在每次迭代里都 `std::thread(...) + join()`，你测到的绝大部分时间是线程创建开销而不是你想测的逻辑：

```cpp
// 错误示范：把线程创建放在循环里
static void bm_bad(benchmark::State& state)
{
    for (auto _ : state) {
        // 每次迭代创建和销毁线程——你测的是线程创建不是业务逻辑
        std::thread t([]() { /* do something trivial */ });
        t.join();
    }
}
```

正确的做法是在循环外创建线程（比如用线程池），循环内只提交任务和等待结果。GBench 的 `Threads(n)` 已经帮你在循环外创建好了线程，你只需要在循环体内做实际的工作。

## 实战：对比不同同步方案

理论讲够了，我们来做一个实际的对比实验。我们会用 GBench 测试三种同步方案在相同工作负载下的性能差异：`std::mutex`、自旋锁（spinlock）、以及 `std::atomic` 的 CAS 循环。测试场景是多个线程并发递增一个共享计数器——这是最简单但也最经典的并发微基准。

```cpp
#include <benchmark/benchmark.h>
#include <mutex>
#include <atomic>
#include <thread>

// 方案一：std::mutex
static void bm_mutex_counter(benchmark::State& state)
{
    int counter = 0;
    std::mutex mu;

    for (auto _ : state) {
        std::lock_guard<std::mutex> lk(mu);
        counter++;
        benchmark::ClobberMemory();
    }
    benchmark::DoNotOptimize(counter);
}

// 方案二：自旋锁
class SpinLock {
public:
    void lock()
    {
        while (locked_.test_and_set(std::memory_order_acquire)) {
            // 提示 CPU 当前处于自旋等待，减少功耗并改善超线程性能
            #if defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
            #else
            std::this_thread::yield();
            #endif
        }
    }

    void unlock()
    {
        locked_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag locked_ = ATOMIC_FLAG_INIT;  // C++11 起可用的初始化宏
};

static void bm_spinlock_counter(benchmark::State& state)
{
    int counter = 0;
    SpinLock spinlock;

    for (auto _ : state) {
        spinlock.lock();
        counter++;
        spinlock.unlock();
        benchmark::ClobberMemory();
    }
    benchmark::DoNotOptimize(counter);
}

// 方案三：atomic CAS
static void bm_atomic_cas_counter(benchmark::State& state)
{
    std::atomic<int> counter{0};

    for (auto _ : state) {
        // CAS 循环：乐观并发
        int expected = counter.load(std::memory_order_relaxed);
        while (!counter.compare_exchange_weak(
            expected, expected + 1,
            std::memory_order_acq_rel,
            std::memory_order_relaxed))
        {
            // CAS 失败，expected 已被更新为当前值，重试
        }
    }
    benchmark::DoNotOptimize(counter);
}

// 另一种 atomic 方案：fetch_add
static void bm_atomic_fetch_add_counter(benchmark::State& state)
{
    std::atomic<int> counter{0};

    for (auto _ : state) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
    benchmark::DoNotOptimize(counter);
}

// 注册所有 benchmark，测试 1/2/4/8 线程
BENCHMARK(bm_mutex_counter)->ThreadRange(1, 8)->UseRealTime();
BENCHMARK(bm_spinlock_counter)->ThreadRange(1, 8)->UseRealTime();
BENCHMARK(bm_atomic_cas_counter)->ThreadRange(1, 8)->UseRealTime();
BENCHMARK(bm_atomic_fetch_add_counter)->ThreadRange(1, 8)->UseRealTime();

BENCHMARK_MAIN();
```

让我们来分析一下你大致会看到的结果（具体数字因 CPU 而异，但趋势是通用的）。

单线程情况下，`fetch_add` 最快（通常 1-2ns），因为它直接映射到 CPU 的 `LOCK XADD` 指令，不需要循环。`mutex` 和 `spinlock` 的开销差不多（几十纳秒），因为只有一个线程没有竞争，mutex 的 fast path 也就是一次原子 CAS。CAS 循环介于两者之间。

多线程情况下，情况变得有趣了。`mutex` 的性能随线程数增加而退化，但退化幅度相对温和——因为 mutex 在竞争激烈时会将线程挂起（通过 futex 系统调用），让出 CPU 给其他线程使用。`spinlock` 在高竞争下表现最差——所有线程都在忙等，CPU 占用率拉满但有效工作很少，而且缓存行在核心间反复失效。CAS 循环的表现取决于竞争程度：低竞争时接近 `fetch_add`，高竞争时因为反复 CAS 失败而退化。`fetch_add` 始终是最快的，但退化的幅度取决于 CPU 的原子指令实现。

这个实验传达了一个重要的工程经验：**lock-free 不等于高性能**。CAS 循环在竞争激烈时可能比 mutex 还慢，因为每次 CAS 失败都是一次白费的 CPU 周期。`fetch_add` 快是因为硬件直接支持了这个操作——它不是"无锁"优化出来的，是 CPU 指令集帮你做了。选同步方案时，要看具体的访问模式和竞争程度，而不是简单地说"无锁更好"。

## 性能计数器：perf stat

benchmark 告诉你"有多快"，但不告诉你"为什么快"或"为什么慢"。要回答"为什么"的问题，我们需要性能计数器——CPU 硬件提供的统计信息，能告诉你缓存命中率、分支预测准确率、上下文切换次数等底层指标。Linux 的 `perf` 工具可以读取这些计数器。

### 基本用法

`perf stat` 的基本用法很简单：

```bash
# 直接运行程序
perf stat ./your_program

# 只关注特定事件
perf stat -e cache-misses,cache-references,context-switches,cpu-migrations ./your_program
```

对于一个并发程序，默认的 `perf stat` 输出大致是这样的：

```text
 Performance counter stats for './your_program':

          2345.67 msec  task-clock              #  3.821 CPUs utilized
                15      context-switches        #  6.395 /sec
                 2      cpu-migrations          #  0.852 /sec
             10457      page-faults             #  4.459 K/sec
     8,234,567,890      cycles                  #  3.510 GHz
     5,678,901,234      instructions            #  0.69  insn per cycle
       456,789,012      cache-references        # 194.857 M/sec
        12,345,678      cache-misses            #  2.70% of all cache refs

       0.614234567 seconds time elapsed

       0.520000000 seconds user
       1.890000000 seconds sys
```

### 关键指标解读

最值得关注的指标是 **cache-misses（缓存未命中）**，它告诉你 CPU 在访问数据时有多少次没在缓存里找到，不得不去主存取。2-3% 的 cache-miss 率对于顺序访问的程序来说算正常，但对于并发程序来说——如果你发现 cache-miss 率随线程数增加而飙升，几乎可以确定有 false sharing 或者数据布局问题。解决方案是检查热数据是否被多个线程频繁修改，如果是，用 `alignas(64)` 把它们分散到不同的缓存行。

另一个重要指标是 **context-switches（上下文切换）**，它反映线程被操作系统换入换出的频率。高上下文切换通常意味着线程在频繁阻塞——可能在等 mutex、等 I/O、或者线程数远超 CPU 核心数导致过度调度。如果一个 8 线程的程序跑在 4 核心上，上下文切换会非常频繁，这时候应该减少线程数或者用线程池控制并发度。

如果你注意到 **cpu-migrations（CPU 迁移）** 数字偏高，说明线程正在被操作系统从一个核心搬到另一个核心。CPU 迁移会导致 L1/L2 缓存全部失效（因为 L1/L2 是核心私有的），对性能影响很大。并发程序中，如果线程频繁被迁移，可以考虑用 `pthread_setaffinity_np` 或者 `taskset` 把线程绑到特定核心上：

```bash
# 只在核心 0-3 上运行
taskset -c 0-3 ./your_program
```

最后一个综合性的效率指标是 **instructions per cycle (IPC)**。现代超标量 CPU 在理想情况下可以每周期执行 4-6 条指令（IPC > 1），所以 IPC 接近或超过 1 意味着 CPU 的流水线利用率还不错；IPC 远低于 1（比如 0.3-0.5）说明 CPU 大量时间在等——等缓存、等内存、等分支解析。并发程序的 IPC 通常比同等的单线程程序低，因为同步操作（mutex lock、atomic CAS）会引入等待和流水线中断。

### 实战：分析一个并发程序的瓶颈

让我们把上面 benchmark 中的 `bm_spinlock_counter`（8 线程版本）单独拎出来用 perf 分析：

```bash
# 编译
clang++ -O2 -std=c++17 -pthread spinlock_bench.cpp -lbenchmark -lpthread -o spinlock_bench

# 用 perf 运行
perf stat -e cache-misses,cache-references,context-switches,cpu-migrations,\
L1-dcache-load-misses,llc-load-misses \
./spinlock_bench --benchmark_filter=bm_spinlock_counter/8
```

你可能会看到这样的输出：

```text
 Performance counter stats for './spinlock_bench --benchmark_filter=bm_spinlock_counter/8':

       234,567,890      cache-references
        45,678,901      cache-misses            # 19.5% of all cache refs
         1,234,567      context-switches
           345,678      cpu-migrations
        67,890,123      L1-dcache-load-misses   # 高 L1 未命中
         5,678,901      llc-load-misses

      12.345678 seconds time elapsed
```

19.5% 的 cache-miss 率对于这个简单的计数器来说非常高——正常情况下应该低于 5%。罪魁祸首是自旋锁在 8 个线程下的缓存行争用：所有线程都在忙等同一个 `atomic_flag` 的状态，每次某个线程获取或释放锁时，缓存行都会在其他 7 个核心之间来回失效，缓存一致性协议的开销占据了大部分执行时间。再看 L1-dcache-load-misses，数字同样很高——自旋锁的忙等循环不断读取锁状态，但每次锁被释放时缓存行都已经被其他核心的写入操作失效了。

作为对比，同样的测试换用 `fetch_add` 版本：

```bash
perf stat -e cache-misses,cache-references,context-switches,cpu-migrations \
./spinlock_bench --benchmark_filter=bm_atomic_fetch_add_counter/8
```

cache-miss 率会降到 5% 以下，因为 `fetch_add` 使用的 `LOCK XADD` 指令在硬件层面原子地完成读-改-写操作，不需要像自旋锁那样反复自旋读取锁状态。

这种 perf 分析让你不只是知道"哪个方案更快"，还知道"为什么更快"——是缓存效率更高？上下文切换更少？还是指令数更少？有了这个底层理解，你在面对新的优化问题时就有了判断的依据，而不是盲目尝试。

### perf 和 Google Benchmark 的联动

GBench 从 v1.7 开始支持通过 `--benchmark_perf_counters` 参数直接读取硬件性能计数器（仅限 Linux），不过更通用的做法是用外部包装的方式与 perf 联动。一个实用的技巧是把 GBench 的输出导入文件，然后用脚本解析：

```bash
# 输出到 CSV 格式
./your_bench --benchmark_format=csv > results.csv

# 同时用 perf 收集硬件计数器
perf stat -o perf_results.txt ./your_bench --benchmark_filter=your_benchmark
```

然后你可以把两份数据放在一起看：GBench 告诉你延迟和吞吐，perf 告诉你缓存和调度行为。

## 我们的位置

到这里，整个卷五的旅程就接近尾声了。让我们回顾一下这一路走来我们学了什么。

我们从"为什么需要并发"这个问题出发，理解了并发和并行的区别、Amdahl 定律和 Gustafson 定律、吞吐量与延迟的权衡。然后我们学习了线程的生命周期管理和 RAII 封装，用 `std::thread` 和 `std::jthread` 把线程管起来。接着是同步原语——mutex、condition variable、RAII 锁守卫——用来保护共享状态。我们深入了原子操作和内存模型，理解了 `memory_order` 背后的缓存一致性协议和 happens-before 关系。然后我们用这些知识构建了并发数据结构——线程安全队列、线程池。之后我们进入了异步 I/O 和协程的世界，用 C++20 协程把异步代码写得像同步一样清晰。再之后是 Actor 模型和 CSP 两种"不共享内存"的并发范式。最后这两篇，我们分别解决了并发编程的两个终极问题："怎么确保正确"（调试）和"怎么确认高效"（性能测试）。

卷五的脉络是一条清晰的学习路径：先理解问题（为什么需要并发、并发有什么坑），再掌握工具（线程、锁、原子、协程），再应用工具构建组件（数据结构、线程池），最后用方法论保障质量（调试和测试）。这条路径的每一步都建立在前一步的基础上，缺了任何一环都会在实际工程中踩坑。

但单机并发只是故事的开始。当你的一台机器不够用了——CPU 算力到顶、内存装不下、网络带宽打满——你需要把问题分布到多台机器上。这时候，"并发"就变成了"分布式"，而分布式环境下你面对的挑战会再升一个量级：网络不可靠、时钟不一致、节点可能随时挂掉。下一篇，也就是卷五的最后一章，我们会站在单机并发的肩膀上，看看当并发跨越网络边界时，我们之前学到的知识哪些还能用，哪些必须重新思考。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch08-debug-testing-perf/`。

## 参考资源

- [Google Benchmark — GitHub](https://github.com/google/benchmark) — 官方仓库和完整文档
- [perf stat — Linux Kernel Documentation](https://perf.wiki.kernel.org/index.php/Tutorial#Counting_with_perf_stat) — perf 工具的官方教程
- [Performance Analysis and Tuning of Linux Systems — Brendan Gregg](https://www.brendangregg.com/linuxperf.html) — Linux 性能分析的权威资源
- [False Sharing — Intel VTune Profiler Cookbook](https://www.intel.com/content/www/us/en/docs/vtune-profiler/cookbook/2023-0/false-sharing.html) — Intel 关于 false sharing 的识别与优化指南
- [C++ Atomic Operations and Performance — Fedor Pikus (CppCon 2017)](https://www.youtube.com/watch?v=ZQFzMfHIxng) — 深入分析原子操作在不同竞争程度下的性能特征
