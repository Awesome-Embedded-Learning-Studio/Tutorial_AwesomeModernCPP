---
chapter: 8
cpp_standard:
- 17
- 20
description: Master the usage of Google Benchmark, avoid common pitfalls in concurrent
  benchmarking, and learn to use performance counters to locate bottlenecks.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 并发程序调试技巧
- 线程池
reading_time_minutes: 17
related:
- CPU cache 与 OS 线程
tags:
- host
- cpp-modern
- intermediate
- atomic
- mutex
- 优化
- 进阶
title: Concurrency Performance Testing and Benchmarking
translation:
  engine: anthropic
  source: documents/vol5-concurrency/ch08-debug-testing-perf/02-concurrency-benchmarks.md
  source_hash: cec196de6157ff04ef51de1d14d828f4ea56457f99f926eaa2d0894e6f54d349
  token_count: 4251
  translated_at: '2026-06-13T11:52:15.714726+00:00'
---
# Concurrency Performance Testing and Benchmarking

> 📖 **Deep Dive**: This article focuses on benchmarking in concurrent scenarios. For more general performance engineering—benchmarking methodology, cache friendliness, SIMD/AVX, and assembly reading—check out [Volume 6: Performance Engineering](../../vol6-performance/index.md).

In the previous article, we solved the correctness problem—using TSan to catch data races, Helgrind to check lock order, and Clang TSA to prevent thread safety violations at compile time. However, a correct concurrent program is not necessarily an efficient concurrent program. I have seen too many scenarios where someone spends three days replacing a mutex with a lock-free queue, excitedly announcing a "3x performance boost," only to find the benchmark methodology flawed: a single run, no warm-up, the compiler optimizing away the entire loop, and even missing `DoNotOptimize`. The "3x boost" you measured might just be measurement error.

In this article, our core problem to solve is: how to scientifically measure the performance of concurrent programs. We will start with the basic usage of Google Benchmark, then dive into the design traps of concurrent benchmarking (there are more pitfalls than you can imagine), followed by a real-world case study comparing the real performance differences of different synchronization schemes. Finally, we will introduce `perf stat`, a performance counter tool on Linux that can tell you exactly where your program is slow.

## Google Benchmark Basics

### Installation

Google Benchmark (hereinafter referred to as GBench) is the most mainstream micro-benchmarking framework in the C++ ecosystem, open-sourced and maintained by Google. There are several ways to install it; the simplest is using CMake's FetchContent:

```cmake
# In your CMakeLists.txt
include(FetchContent)
FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.8.3
)
FetchContent_MakeAvailable(benchmark)

# Link to your target
target_link_libraries(your_target benchmark::benchmark benchmark::benchmark_main)
```

If you prefer a system-level installation:

```bash
# Ubuntu/Debian
sudo apt-get install libbenchmark-dev

# macOS (brew)
brew install google-benchmark
```

### Your First Benchmark

The core idea of GBench is: you write a function, and the framework automatically decides how many iterations to run to get statistically reliable results. Let's write a simple example to get familiar with its API:

```cpp
#include <benchmark/benchmark.h>

static void BM_StringCreation(benchmark::State& state) {
  for (auto _ : state) {
    std::string create_string("Hello, World!"); // This code gets timed
  }
}
BENCHMARK(BM_StringCreation);

BENCHMARK_MAIN();
```

Compile and run:

```bash
g++ -O3 -std=c++23 -lbenchmark main.cpp -o benchmark
./benchmark
```

The output will look something like this:

```text
--------------------------------------------------------------
Benchmark                  Time             CPU   Iterations
--------------------------------------------------------------
BM_StringCreation       5.3 ns         5.3 ns    100000000
```

Meaning of each column: `Time` is the wall clock time, `CPU` is the CPU time (the actual time the process spent on the CPU, including user and kernel mode), and `Iterations` is how many times the framework ran the loop. For single-threaded benchmarks, Time and CPU should be very close; but for multi-threaded benchmarks, CPU time will be the sum of CPU time across all threads—which is why we need `->UseRealTime()`.

### Multi-threaded Benchmarks

GBench natively supports multi-threaded testing. You can specify the thread count via `->Threads()`, or use `->ThreadRange()` to automatically iterate through different thread counts:

```cpp
static void BM_MultiThreaded(benchmark::State& state) {
  for (auto _ : state) {
    // Simulate some work
    benchmark::DoNotOptimize(state.iterations());
  }
}
BENCHMARK(BM_MultiThreaded)->ThreadRange(1, 8)->UseRealTime();
```

Here are a few key points to explain. `ThreadRange(1, 8)` tells the framework to run this benchmark with 1, 2, 4, and 8 threads (powers of two). `UseRealTime()` is critical—without it, the framework reports CPU time by default. Under multi-threading, CPU time is the sum of all threads' time. For example, if 4 threads run for 100ms of wall time, CPU time might be 350ms (due to waiting and scheduling overhead). If you report CPU time, you might think "it got slower"—which is completely misleading. `DoNotOptimize` is a compiler-level memory barrier that tells the compiler "don't cache any memory state," preventing the optimizer from optimizing away our atomic operations.

The output will be similar to:

```text
--------------------------------------------------------------
Benchmark                  Time             CPU   Iterations
--------------------------------------------------------------
BM_MultiThreaded/1:1   10.2 ms         9.8 ms          68
BM_MultiThreaded/2:1   5.5 ms         10.6 ms         126
BM_MultiThreaded/4:1   3.1 ms         12.1 ms         224
BM_MultiThreaded/8:1   3.5 ms         27.8 ms         201
```

Look at the CPU column: the more threads, the higher the total CPU time, but the wall time (Time column) doesn't decrease linearly—there's some speedup from 1 to 2 threads, but it actually gets slower at 4 and 8 threads. This is because all threads are performing write operations on the same atomic variable, causing cache lines to bounce between cores (similar to the false sharing mechanism, but strictly speaking, it's cache line contention under true sharing). This is a very typical pattern in concurrent performance analysis: more threads doesn't always mean faster.

## Concurrent Benchmark Design Traps

Writing a correct benchmark is harder than writing a correct concurrent program—because you have to fight compiler optimizations, CPU cache behavior, and OS scheduling policies. These factors cause trouble in single-threaded benchmarks, but they get even worse in multi-threaded ones.

### Warm-up: Cold Start vs. Steady State

The CPU's cache hierarchy (L1, L2, L3) has an order-of-magnitude impact on performance. The first time you access data, it might need to be loaded from main memory (DRAM), taking 100-300 CPU cycles; the second time, it's already in L1 cache, taking only 3-4 cycles. If your benchmark doesn't warm up, the data load from the first iteration will severely skew the average time.

GBench's internal loop does a certain amount of warm-up—the framework runs a few iterations first to "stabilize" the results. But if you allocate a large block of memory outside the loop, that memory might not be in the cache during the first iteration. If your goal is to measure "steady-state" performance, you can manually run a few loops before the main loop:

```cpp
static void BM_WithWarmup(benchmark::State& state) {
  std::vector<int> data(1024);

  // Manual warm-up
  for (int i = 0; i < 1000; ++i) {
    benchmark::DoNotOptimize(data[i % 1024]);
  }

  for (auto _ : state) {
    benchmark::DoNotOptimize(data[state.range(0)]);
  }
}
BENCHMARK(BM_WithWarmup)->Range(64, 4096);
```

Conversely—if you want to measure "cold start" performance (e.g., the latency of an operation's first execution), then you shouldn't warm up. The key is to know exactly what you are measuring.

### Compiler Optimizations: Your Adversary

This is the easiest trap to fall into. The compiler's job is to make your code fast—but your goal is to measure the raw speed of the code. If the compiler realizes your calculation results aren't used, it might optimize away the entire loop. If it sees you doing the same calculation every loop, it might hoist it out of the loop and calculate it just once.

GBench provides two key tools to combat these issues:

```cpp
benchmark::DoNotOptimize(x); // Prevents the compiler from optimizing away 'x'
benchmark::ClobberMemory();  // Forces the compiler to reload memory from registers
```

A practical pattern is to use them together:

```cpp
static void BM_AtomicIncrement(benchmark::State& state) {
  std::atomic<int> counter{0};
  for (auto _ : state) {
    counter.fetch_add(1, std::memory_order_relaxed);
    benchmark::ClobberMemory(); // Prevent hoisting the loop
  }
  benchmark::DoNotOptimize(counter); // Prevent optimizing away the result
}
BENCHMARK(BM_AtomicIncrement);
```

`DoNotOptimize` ensures `counter` isn't optimized away, and `ClobberMemory` ensures memory reads in each loop aren't optimized to "I read this last time, just reuse it." But be careful not to abuse `ClobberMemory`—it tells the compiler that all memory might have been modified, forcing it to conservatively reload all values cached in registers. In some scenarios, this introduces extra memory access overhead, making your measured performance worse than reality.

### False Sharing: The Invisible Performance Killer

False sharing is a killer of concurrent performance—two threads modifying different variables, but those variables happen to be on the same cache line (usually 64 bytes), causing every write to invalidate the other core's cache line. Let's use a benchmark to intuitively feel its power:

```cpp
struct BadCounter {
  std::atomic<int> val;
};

struct PaddedCounter {
  alignas(64) std::atomic<int> val;
};

static void BM_NoPadding(benchmark::State& state) {
  static BadCounter counter;
  for (auto _ : state) {
    counter.val.fetch_add(1, std::memory_order_relaxed);
  }
}
BENCHMARK(BM_NoPadding)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

static void BM_WithPadding(benchmark::State& state) {
  static PaddedCounter counter;
  for (auto _ : state) {
    counter.val.fetch_add(1, std::memory_order_relaxed);
  }
}
BENCHMARK(BM_WithPadding)->Threads(1)->Threads(2)->Threads(4)->Threads(8);
```

After compiling and running, you will see results similar to this (specific numbers depend on your CPU):

```text
--------------------------------------------------------------
Benchmark                  Time             CPU   Iterations
--------------------------------------------------------------
BM_NoPadding/1:1         8.5 ns         8.5 ns     80000000
BM_NoPadding/2:1        12.3 ns         6.1 ns     56000000
BM_NoPadding/4:1        18.7 ns         4.7 ns     37000000
BM_NoPadding/8:1        35.2 ns         4.4 ns     20000000
BM_WithPadding/1:1       8.6 ns         8.6 ns     81000000
BM_WithPadding/2:1        8.9 ns         4.5 ns     78000000
BM_WithPadding/4:1        9.1 ns         2.3 ns     76000000
BM_WithPadding/8:1        9.3 ns         1.2 ns     75000000
```

Without padding, the more threads, the slower it gets—because every core's write has to kick out other cores' cache lines. The overhead of the cache coherence protocol (MESI) grows super-linearly with thread count (roughly O(n²), because each write needs to notify the other n-1 cores). With padding, each counter occupies its own cache line, threads don't interfere with each other, and performance barely changes with thread count. This difference can reach nearly 8x at 8 threads—this is the real lethality of false sharing.

### Thread Creation: Don't Create Threads in the Loop

Do not create and destroy threads inside the benchmark loop. Thread creation is an expensive operation—the kernel needs to allocate stack space, initialize the thread control block, and register it with the scheduler—usually taking 50-200 microseconds on Linux. If you `std::thread` in every iteration, you are mostly measuring thread creation overhead, not the logic you want to test:

```cpp
// BAD: Creating threads inside the loop
static void BM_BadThread(benchmark::State& state) {
  for (auto _ : state) {
    std::thread t([]{ /* work */ });
    t.join();
  }
}
BENCHMARK(BM_BadThread);
```

The correct way is to create threads outside the loop (e.g., using a thread pool) and only submit tasks and wait for results inside the loop. GBench's `->Threads()` has already created the threads for you outside the loop; you just need to do the actual work inside the loop body.

## Real-World Combat: Comparing Different Synchronization Schemes

Enough theory, let's do a real comparison experiment. We will use GBench to test the performance differences of three synchronization schemes under the same workload: `std::mutex`, spinlock, and `std::atomic` CAS loop. The test scenario is multiple threads concurrently incrementing a shared counter—the simplest but most classic concurrent micro-benchmark.

```cpp
// ... (Code for benchmarking Mutex, Spinlock, and Atomic) ...
```

Let's analyze the results you will likely see (specific numbers vary by CPU, but the trend is universal).

In single-threaded cases, `std::atomic` is fastest (usually 1-2ns) because it maps directly to the CPU's `inc` instruction without a loop. `std::mutex` and spinlock have similar overhead (tens of nanoseconds) because there is no contention; the mutex fast path is just one atomic CAS. The CAS loop is somewhere in between.

In multi-threaded cases, things get interesting. `std::mutex` performance degrades with thread count, but the degradation is relatively mild—because mutex suspends threads (via futex system calls) under high contention, yielding the CPU to other threads. Spinlock performs worst under high contention—all threads are busy waiting, CPU usage is maxed out but effective work is low, and cache lines bounce between cores. The CAS loop performance depends on contention: close to `std::atomic` under low contention, degrading due to repeated CAS failures under high contention. `std::atomic` is always fastest, but the degradation depends on the CPU's atomic instruction implementation.

This experiment conveys an important engineering lesson: **lock-free does not equal high performance**. A CAS loop can be slower than a mutex under high contention because every failed CAS is a wasted CPU cycle. `std::atomic` is fast because the hardware directly supports this operation—it's not "optimized" from being lock-free, the CPU instruction set does it for you. When choosing a synchronization scheme, look at the specific access pattern and contention level, not simply saying "lock-free is better."

## Performance Counters: perf stat

Benchmarks tell you "how fast," but not "why it's fast" or "why it's slow." To answer the "why," we need performance counters—statistics provided by CPU hardware that tell you about cache hit rates, branch prediction accuracy, context switches, and other low-level metrics. Linux's `perf stat` tool can read these counters.

### Basic Usage

The basic usage of `perf stat` is simple:

```bash
perf stat ./your_benchmark
```

For a concurrent program, the default `perf stat` output looks roughly like this:

```text
Performance counter stats for './benchmark':

      1024.23 msec task-clock                #    0.999 CPUs utilized
                 1      context-switches          #    0.001 K/sec
                 0      cpu-migrations            #    0.000 K/sec
           12,345      page-faults               #    0.012 M/sec
     4,123,456,789      cycles                    #    4.027 GHz
     8,234,567,890      instructions              #    2.00  insn per cycle
       567,890,123      cache-references          #  554.502 M/sec
        12,345,678      cache-misses              #    2.178 % of all cache refs
```

### Interpreting Key Metrics

The metric most worth watching is **cache-misses**. It tells you how many times the CPU failed to find data in the cache and had to go to main memory. A 2-3% cache-miss rate is normal for sequentially accessing programs, but for concurrent programs—if you find the cache-miss rate soaring with thread count, you can almost be certain there is false sharing or a data layout issue. The solution is to check if hot data is frequently modified by multiple threads, and if so, use `alignas(64)` to spread them to different cache lines.

Another important metric is **context-switches**, reflecting how frequently threads are swapped in and out by the OS. High context switches usually mean threads are frequently blocking—waiting for mutex, waiting for I/O, or thread count far exceeding CPU cores causing over-scheduling. If an 8-thread program runs on 4 cores, context switches will be very frequent; at this point, you should reduce thread count or use a thread pool to control concurrency.

If you notice the **cpu-migrations** number is high, it means threads are being moved by the OS from one core to another. CPU migration causes all L1/L2 cache to invalidate (because L1/L2 are core-private), which has a huge performance impact. In concurrent programs, if threads migrate frequently, you can consider using `pthread_setaffinity_np` or `std::thread::native_handle` to bind threads to specific cores:

```cpp
cpu_set_t cpuset;
CPU_ZERO(&cpuset);
CPU_SET(0, &cpuset); // Bind to core 0
pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t), &cpuset);
```

The last comprehensive efficiency metric is **instructions per cycle (IPC)**. Modern superscalar CPUs can ideally execute 4-6 instructions per cycle (IPC > 1), so IPC close to or exceeding 1 means CPU pipeline utilization is decent; IPC far below 1 (e.g., 0.3-0.5) means the CPU is spending a lot of time waiting—waiting for cache, waiting for memory, waiting for branch resolution. Concurrent programs usually have lower IPC than equivalent single-threaded programs because synchronization operations (mutex lock, atomic CAS) introduce waiting and pipeline stalls.

### Real-World Combat: Analyzing a Concurrent Program's Bottleneck

Let's take the `BM_Spinlock` (8-thread version) from the benchmark above and analyze it with perf:

```bash
perf stat -e cache-misses,cache-references,instructions,cycles,L1-dcache-load-misses ./benchmark --benchmark_filter=BM_Spinlock/8
```

You might see output like this:

```text
       123,456,789      cache-misses              #   19.5% of all cache refs
       678,901,234      cache-references
     3,456,789,012      cycles
     6,789,012,345      instructions              #    1.96  insn per cycle
        98,765,432      L1-dcache-load-misses     #   14.3% of all L1-dcache hits
```

A 19.5% cache-miss rate is very high for this simple counter—normally it should be below 5%. The culprit is the cache line contention of the spinlock under 8 threads: all threads are busy waiting on the same atomic flag state. Every time a thread acquires or releases the lock, the cache line invalidates between the other 7 cores. The overhead of the cache coherence protocol takes up most of the execution time. Looking at L1-dcache-load-misses, the number is similarly high—the spinlock's busy-wait loop constantly reads the lock state, but every time the lock is released, the cache line has already been invalidated by other cores' writes.

In contrast, switching to the `std::atomic` version for the same test:

```bash
perf stat -e cache-misses,cache-references,instructions,cycles ./benchmark --benchmark_filter=BM_Atomic/8
```

The cache-miss rate will drop below 5%, because `std::atomic` uses the `lock xadd` instruction (on x86) to complete the read-modify-write operation atomically at the hardware level, without needing to repeatedly spin-read the lock state like a spinlock.

This perf analysis lets you know not just "which solution is faster," but "why it's faster"—is it higher cache efficiency? Fewer context switches? Or fewer instructions? With this low-level understanding, when facing new optimization problems, you have a basis for judgment, rather than blindly trying things.

### Linking perf and Google Benchmark

Since v1.7, GBench supports reading hardware performance counters directly via the `--benchmark_perf_counters` flag (Linux only), but a more general approach is to use an external wrapper to link with perf. A practical trick is to pipe GBench output to a file and parse it with a script:

```bash
./benchmark --benchmark_out=results.json
perf stat -o perf.stats ./benchmark
```

Then you can look at the two datasets together: GBench tells you latency and throughput, perf tells you cache and scheduling behavior.

## Where We Are

At this point, the journey through Volume 5 is drawing to a close. Let's review what we've learned along the way.

We started with the question "why do we need concurrency," understanding the difference between concurrency and parallelism, Amdahl's Law and Gustafson's Law, and the trade-off between throughput and latency. Then we learned thread lifecycle management and RAII wrappers, using `std::jthread` and `std::stop_token` to manage threads. Next were synchronization primitives—mutex, condition variable, RAII lock guards—to protect shared state. We dove into atomic operations and the memory model, understanding the cache coherence protocol behind `std::atomic` and happens-before relationships. Then we used this knowledge to build concurrent data structures—thread-safe queues, thread pools. After that, we entered the world of async I/O and coroutines, using C++20 coroutines to make async code as clear as sync code. Then came the Actor model and CSP, two "shared-nothing" concurrency paradigms. Finally, in these last two articles, we solved the two ultimate problems of concurrent programming: "how to ensure correctness" (debugging) and "how to confirm efficiency" (performance testing).

The thread of Volume 5 is a clear learning path: first understand the problem (why concurrency, what are the pitfalls), then master the tools (threads, locks, atomics, coroutines), then apply tools to build components (data structures, thread pools), and finally use methodology to guarantee quality (debugging and testing). Every step of this path builds on the previous one; missing any link will lead to pitfalls in actual engineering.

But single-machine concurrency is just the beginning of the story. When one machine isn't enough—CPU compute power tops out, memory can't fit, network bandwidth is maxed—you need to distribute the problem across multiple machines. At this point, "concurrency" becomes "distributed," and the challenges you face in a distributed environment rise another order of magnitude: unreliable networks, inconsistent clocks, nodes that can crash at any time. In the next article, the final chapter of Volume 5, standing on the shoulders of single-machine concurrency, we will see which of our learned knowledge still applies when concurrency crosses network boundaries, and what must be rethought.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `examples/vol5_concurrency`.

## References

- [Google Benchmark — GitHub](https://github.com/google/benchmark) — Official repo and complete documentation
- [perf stat — Linux Kernel Documentation](https://perf.wiki.kernel.org/index.php/Tutorial#Counting_with_perf_stat) — Official tutorial for the perf tool
- [Performance Analysis and Tuning of Linux Systems — Brendan Gregg](https://www.brendangregg.com/linuxperf.html) — Authoritative resource for Linux performance analysis
- [False Sharing — Intel VTune Profiler Cookbook](https://www.intel.com/content/www/us/en/docs/vtune-profiler/cookbook/2023-0/false-sharing.html) — Intel's guide on identifying and optimizing false sharing
- [C++ Atomic Operations and Performance — Fedor Pikus (CppCon 2017)](https://www.youtube.com/watch?v=ZQFzMfHIxng) — Deep analysis of atomic operation performance characteristics under different contention levels
