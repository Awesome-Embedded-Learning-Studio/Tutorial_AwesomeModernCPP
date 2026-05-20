---
title: Concurrency Performance Testing and Benchmarks
description: Master the usage of Google Benchmark, avoid common pitfalls in concurrent
  benchmarking, and learn to use performance counters to pinpoint bottlenecks.
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
cpp_standard:
- 17
- 20
prerequisites:
- 并发程序调试技巧
- 线程池
related:
- CPU cache 与 OS 线程
translation:
  source: documents/vol5-concurrency/ch08-debug-testing-perf/02-concurrency-benchmarks.md
  source_hash: affc82a449231135acc36f7d7c00bc0aca70c6dc83b95c57c99da4ee44494c82
  translated_at: '2026-05-20T04:49:33.543564+00:00'
  engine: anthropic
  token_count: 4220
---
# Concurrency Performance Testing and Benchmarking

In the previous article, we tackled correctness—using TSan to catch data races, Helgrind to check lock ordering, and Clang TSA to prevent thread safety violations at compile time. However, a correct concurrent program is not necessarily an efficient one. We have seen too many scenarios like this: someone spends three days replacing a mutex with a lock-free queue, excitedly announces a "3x performance boost," but a look at the benchmark methodology reveals a single run, no warmup, the compiler almost optimized away the entire loop, and not even `UseRealTime` was added. The "3x boost" you measured might just be measurement noise.

The core question we will address in this article is: how do we scientifically measure the performance of concurrent programs? We will start with the basics of Google Benchmark, then dive into the design pitfalls of concurrent benchmarking (there are far more traps than you might imagine), walk through a real-world case comparing the actual performance differences of various synchronization schemes, and finally introduce `perf stat`, a Linux performance counter tool that can tell you exactly why your program is slow.

## Google Benchmark Basics

### Installation

Google Benchmark (referred to as GBench below) is the most mainstream microbenchmarking framework in the C++ ecosystem, open-sourced and maintained by Google. There are several ways to install it, but the easiest is using CMake's FetchContent:

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

If you prefer a system-level installation:

```bash
# Ubuntu/Debian
sudo apt install libbenchmark-dev

# macOS
brew install google-benchmark

# vcpkg
vcpkg install benchmark
```

### Your First Benchmark

The core idea behind GBench is: you write a function, and the framework automatically determines how many iterations to run to achieve statistically reliable results. Let's write a simplest example to get familiar with its API:

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

Compile and run:

```bash
clang++ -O2 -std=c++17 benchmark_demo.cpp -lbenchmark -lpthread -o demo
./demo
```

The output will look something like this:

```text
-------------------------------------------------------
Benchmark             Time           CPU      Iterations
-------------------------------------------------------
bm_vector_sum      1234 ns       1234 ns       567890
```

The meaning of each column: `Time` is the wall time, `CPU` is the CPU time (the actual time the process spent on the CPU, including user and kernel mode), and `Iterations` is how many iterations the framework ran. For single-threaded benchmarks, Time and CPU should be very close; but for multi-threaded benchmarks, CPU time is the sum of CPU times across all threads—which is why we need `UseRealTime`.

### Multi-threaded Benchmarks

GBench natively supports multi-threaded testing. We can specify the thread count using `Threads(n)`, or use `ThreadRange` to automatically iterate over different thread counts:

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

There are a few key points to explain here. `ThreadRange(1, 8)` makes the framework run this benchmark with 1, 2, 4, and 8 threads (powers of two). `UseRealTime()` is crucial—without it, the framework reports CPU time by default, and in multi-threaded scenarios, CPU time is the sum of all thread times. For example, if 4 threads run for 100ms of wall time, the CPU time might be 350ms (due to waiting and scheduling overhead). If you report CPU time, you might think it "got slower"—which is completely misleading. `ClobberMemory()` is a compiler-level memory barrier that tells the compiler "do not cache any memory state," preventing the optimizer from optimizing away our atomic operations.

The output will be similar to:

```text
------------------------------------------------------------------
Benchmark                        Time          CPU      Iterations
------------------------------------------------------------------
bm_atomic_counter/1           2.3 ns       2.3 ns    300000000
bm_atomic_counter/2           1.8 ns       3.5 ns    400000000
bm_atomic_counter/4           2.1 ns       7.8 ns    333333333
bm_atomic_counter/8           3.5 ns       25 ns     200000000
```

Notice the CPU column: the more threads, the higher the total CPU time, but the wall time (Time column) does not decrease linearly—going from 1 to 2 threads shows some speedup, but at 4 and 8 threads it actually gets slower. This is because all threads are performing write operations on the same atomic variable, causing the cache line to bounce back and forth between cores (this mechanism is similar to false sharing, but strictly speaking, it is cache line contention under true sharing). This is a very typical pattern in concurrent performance analysis: more threads does not mean faster execution.

## Concurrent Benchmark Design Pitfalls

Writing a correct benchmark is harder than writing a correct concurrent program—because you have to fight against compiler optimizations, CPU cache behavior, and OS scheduling policies. These factors cause trouble in single-threaded benchmarks, and become even more exacerbated in multi-threaded ones.

### Warmup: Cold Start vs. Steady State

The impact of the CPU cache hierarchy (L1, L2, L3) on performance is orders of magnitude. The first time you access a piece of data, it might need to be loaded from main memory (DRAM), taking 100-300 CPU cycles; the second time, it is already in the L1 cache, taking only 3-4 cycles. If your benchmark does not warm up, the data loading during the first iteration will significantly inflate the average time.

GBench's `KeepRunning()` loop does a certain degree of warmup by itself—the framework runs a few iterations first to "stabilize" the results. But if you allocate a large block of memory outside the loop, that memory might not be in the cache during the first iteration. If your goal is to measure "steady-state" performance, you can manually run a few iterations before the loop:

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

Conversely—if what you want to measure is "cold start" performance (like the first-execution latency of an operation), then you should not warm up. The key is to be clear about what you are measuring.

### Compiler Optimizations: Your Adversary

This is the easiest trap to fall into. The compiler's job is to make your code faster—but your goal is to measure the raw speed of the code. If the compiler finds that your calculation results are not used, it might optimize away the entire loop. If the compiler finds that you are doing the same calculation in every iteration, it might hoist the computation outside the loop and calculate it just once.

GBench provides two key tools to combat these issues:

```cpp
// benchmark::DoNotOptimize(expr)
// 告诉编译器 expr 的值"可能"被外部使用，不要优化掉
benchmark::DoNotOptimize(result);

// benchmark::ClobberMemory()
// 告诉编译器所有内存状态都可能被外部修改
// 相当于一个全局的读写屏障
benchmark::ClobberMemory();
```

A practical pattern is to use them together:

```cpp
for (auto _ : state) {
    int result = expensive_computation();
    benchmark::DoNotOptimize(result);
    benchmark::ClobberMemory();
}
```

`DoNotOptimize` ensures that `result` is not optimized away, and `ClobberMemory` ensures that memory reads in each iteration are not optimized to "it was already read last time, just reuse it." But be careful not to abuse `ClobberMemory`—it tells the compiler that all memory might be modified, so the compiler must conservatively reload all values cached in registers. In some scenarios, this introduces extra memory access overhead, making the performance you measure worse than actual conditions.

### False Sharing: The Invisible Performance Killer

False sharing is a concurrency performance killer—two threads each modify different variables, but these variables happen to reside on the same cache line (typically 64 bytes), causing every write to invalidate the other core's cache line. Let's use a benchmark to intuitively feel its power:

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

After compiling and running, you will see results similar to this (exact numbers depend on your CPU):

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

The version without padding gets slower as the number of threads increases—because every write by each core has to kick out the cache lines of other cores, and the overhead of the cache coherence protocol (MESI) grows super-linearly with the thread count (roughly O(n²), because each write needs to notify the other n-1 cores). With padding, each counter exclusively occupies a cache line, threads do not interfere with each other, and performance barely changes with thread count. This difference can reach nearly 8x at 8 threads—that is the real destructive power of false sharing.

### Thread Creation: Don't Create Threads Inside the Loop

Do not create and destroy threads inside the benchmark loop. Thread creation is an expensive operation—the kernel needs to allocate stack space for it, initialize the thread control block, and register it with the scheduler—on Linux, this usually takes 50-200 microseconds. If you `std::thread(...) + join()` in every iteration, most of the time you measure will be thread creation overhead rather than the logic you actually want to test:

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

The correct approach is to create threads outside the loop (for example, using a thread pool), and only submit tasks and wait for results inside the loop. GBench's `Threads(n)` already creates the threads for you outside the loop; you just need to do the actual work inside the loop body.

## Real-World Case: Comparing Different Synchronization Schemes

Enough theory—let's do a practical comparison experiment. We will use GBench to test the performance differences of three synchronization schemes under the same workload: `std::mutex`, spinlock, and a `std::atomic` CAS loop. The test scenario is multiple threads concurrently incrementing a shared counter—this is the simplest but most classic concurrent microbenchmark.

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

Let's analyze the results you will roughly see (exact numbers vary by CPU, but the trends are universal).

In the single-threaded case, `fetch_add` is the fastest (usually 1-2ns) because it directly maps to the CPU's `LOCK XADD` instruction and does not need a loop. The overhead of `mutex` and `spinlock` is similar (tens of nanoseconds), because with only one thread there is no contention, and the mutex fast path is just a single atomic CAS. The CAS loop falls somewhere in between.

In the multi-threaded case, things get interesting. The performance of `mutex` degrades as thread count increases, but the degradation is relatively mild—because when contention is fierce, the mutex suspends threads (via the futex system call), yielding the CPU to other threads. `spinlock` performs worst under high contention—all threads are busy-waiting, CPU utilization is maxed out but effective work is minimal, and the cache line bounces back and forth between cores. The CAS loop's performance depends on the contention level: close to `fetch_add` under low contention, but degrading due to repeated CAS failures under high contention. `fetch_add` is always the fastest, but the degradation magnitude depends on the CPU's atomic instruction implementation.

This experiment conveys an important engineering lesson: **lock-free does not mean high performance**. A CAS loop can be slower than a mutex under fierce contention, because every failed CAS is a wasted CPU cycle. `fetch_add` is fast because the hardware directly supports this operation—it is not "optimized" to be lock-free; the CPU instruction set does it for you. When choosing a synchronization scheme, look at the specific access patterns and contention levels, rather than simply saying "lock-free is better."

## Performance Counters: perf stat

Benchmarks tell you "how fast," but they don't tell you "why it's fast" or "why it's slow." To answer the "why" question, we need performance counters—statistics provided by CPU hardware that tell you about cache hit rates, branch prediction accuracy, context switch counts, and other low-level metrics. Linux's `perf` tool can read these counters.

### Basic Usage

The basic usage of `perf stat` is very simple:

```bash
# 直接运行程序
perf stat ./your_program

# 只关注特定事件
perf stat -e cache-misses,cache-references,context-switches,cpu-migrations ./your_program
```

For a concurrent program, the default `perf stat` output looks roughly like this:

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

### Interpreting Key Metrics

The metric most worth paying attention to is **cache-misses**, which tells you how many times the CPU failed to find the data in the cache when accessing it and had to go to main memory. A 2-3% cache-miss rate is normal for sequentially accessed programs, but for concurrent programs—if you find the cache-miss rate soaring as thread count increases, you can almost be certain there is false sharing or a data layout issue. The solution is to check whether hot data is being frequently modified by multiple threads, and if so, use `alignas(64)` to spread them across different cache lines.

Another important metric is **context-switches**, which reflects how frequently threads are being swapped in and out by the OS. High context switches usually mean threads are frequently blocking—maybe waiting on a mutex, waiting for I/O, or the thread count far exceeds the CPU core count causing over-scheduling. If an 8-thread program runs on 4 cores, context switches will be very frequent, and you should reduce the thread count or use a thread pool to control concurrency.

If you notice the **cpu-migrations** number is high, it means threads are being moved from one core to another by the OS. CPU migrations cause all L1/L2 cache to be invalidated (because L1/L2 are core-private), which has a huge impact on performance. In concurrent programs, if threads are frequently migrated, you can consider using `pthread_setaffinity_np` or `taskset` to pin threads to specific cores:

```bash
# 只在核心 0-3 上运行
taskset -c 0-3 ./your_program
```

The last comprehensive efficiency metric is **instructions per cycle (IPC)**. Modern superscalar CPUs can ideally execute 4-6 instructions per cycle (IPC > 1), so an IPC close to or exceeding 1 means the CPU's pipeline utilization is decent; an IPC well below 1 (like 0.3-0.5) means the CPU is spending a lot of time waiting—waiting for cache, waiting for memory, waiting for branch resolution. Concurrent programs typically have a lower IPC than equivalent single-threaded programs, because synchronization operations (mutex lock, atomic CAS) introduce waits and pipeline stalls.

### Real-World Case: Analyzing a Concurrent Program's Bottleneck

Let's take the `bm_spinlock_counter` (8-thread version) from the benchmark above and analyze it separately with perf:

```bash
# 编译
clang++ -O2 -std=c++17 -pthread spinlock_bench.cpp -lbenchmark -lpthread -o spinlock_bench

# 用 perf 运行
perf stat -e cache-misses,cache-references,context-switches,cpu-migrations,\
L1-dcache-load-misses,llc-load-misses \
./spinlock_bench --benchmark_filter=bm_spinlock_counter/8
```

You might see output like this:

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

A 19.5% cache-miss rate is extremely high for this simple counter—under normal circumstances, it should be below 5%. The culprit is the cache line contention of the spinlock under 8 threads: all threads are busy-waiting on the state of the same `atomic_flag`, and every time a thread acquires or releases the lock, the cache line bounces back and forth among the other 7 cores. The overhead of the cache coherence protocol dominates most of the execution time. Looking at L1-dcache-load-misses, the number is similarly high—the spinlock's busy-wait loop constantly reads the lock state, but every time the lock is released, the cache line has already been invalidated by other cores' write operations.

As a comparison, the same test using the `fetch_add` version:

```bash
perf stat -e cache-misses,cache-references,context-switches,cpu-migrations \
./spinlock_bench --benchmark_filter=bm_atomic_fetch_add_counter/8
```

The cache-miss rate drops below 5%, because the `LOCK XADD` instruction used by `fetch_add` atomically completes the read-modify-write operation at the hardware level, without needing to repeatedly spin and read the lock state like a spinlock.

This kind of perf analysis lets you know not just "which scheme is faster," but "why it's faster"—is it higher cache efficiency? Fewer context switches? Or fewer instructions? With this low-level understanding, you have a basis for judgment when facing new optimization problems, rather than blindly trying things.

### Integrating perf with Google Benchmark

Starting from v1.7, GBench supports reading hardware performance counters directly through the `--benchmark_perf_counters` parameter (Linux only), but a more universal approach is to integrate with perf via external wrapping. A practical trick is to redirect GBench's output to a file, then parse it with a script:

```bash
# 输出到 CSV 格式
./your_bench --benchmark_format=csv > results.csv

# 同时用 perf 收集硬件计数器
perf stat -o perf_results.txt ./your_bench --benchmark_filter=your_benchmark
```

Then you can look at both sets of data together: GBench tells you latency and throughput, while perf tells you cache and scheduling behavior.

## Where We Are

At this point, our journey through Volume 5 is drawing to a close. Let's look back at what we have learned along the way.

We started from the question "why do we need concurrency," understanding the difference between concurrency and parallelism, Amdahl's Law and Gustafson's Law, and the tradeoff between throughput and latency. Then we learned thread lifecycle management and RAII wrappers, using `std::thread` and `std::jthread` to manage threads. Next came synchronization primitives—mutex, condition variable, RAII lock guards—used to protect shared state. We dove deep into atomic operations and memory models, understanding the cache coherence protocol behind `memory_order` and happens-before relationships. Then we used this knowledge to build concurrent data structures—thread-safe queues, thread pools. After that, we entered the world of asynchronous I/O and coroutines, using C++20 coroutines to make asynchronous code as clear as synchronous code. Then came the Actor model and CSP, two "shared-nothing" concurrency paradigms. In these last two articles, we addressed the two ultimate problems of concurrent programming respectively: "how to ensure correctness" (debugging) and "how to confirm efficiency" (performance testing).

The thread of Volume 5 is a clear learning path: first understand the problem (why we need concurrency, what the pitfalls are), then master the tools (threads, locks, atomics, coroutines), then apply the tools to build components (data structures, thread pools), and finally use methodologies to guarantee quality (debugging and testing). Each step of this path builds on the previous one; missing any link will lead to pitfalls in actual engineering.

But single-machine concurrency is only the beginning of the story. When one machine is not enough—CPU compute power maxed out, memory can't hold it all, network bandwidth saturated—you need to distribute the problem across multiple machines. At that point, "concurrency" becomes "distributed," and the challenges you face in a distributed environment escalate by another order of magnitude: unreliable networks, inconsistent clocks, and nodes that can fail at any time. In the next article, the final chapter of Volume 5, we will stand on the shoulders of single-machine concurrency and see which of the knowledge we have learned still applies when concurrency crosses network boundaries, and what must be rethought.

> 💡 Complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch08-debug-testing-perf/`.

## References

- [Google Benchmark — GitHub](https://github.com/google/benchmark) — Official repository and complete documentation
- [perf stat — Linux Kernel Documentation](https://perf.wiki.kernel.org/index.php/Tutorial#Counting_with_perf_stat) — Official tutorial for the perf tool
- [Performance Analysis and Tuning of Linux Systems — Brendan Gregg](https://www.brendangregg.com/linuxperf.html) — Authoritative resource for Linux performance analysis
- [False Sharing — Intel VTune Profiler Cookbook](https://www.intel.com/content/www/us/en/docs/vtune-profiler/cookbook/2023-0/false-sharing.html) — Intel's guide to identifying and optimizing false sharing
- [C++ Atomic Operations and Performance — Fedor Pikus (CppCon 2017)](https://www.youtube.com/watch?v=ZQFzMfHIxng) — In-depth analysis of atomic operation performance characteristics under different contention levels
