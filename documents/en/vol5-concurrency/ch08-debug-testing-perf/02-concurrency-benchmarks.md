---
chapter: 8
cpp_standard:
- 17
- 20
description: Master the usage of Google Benchmark, avoid common pitfalls in concurrent
  benchmarking, and learn to use performance counters to pinpoint bottlenecks.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 并发程序调试技巧
- 线程池
reading_time_minutes: 20
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
  source: documents/vol5-concurrency/ch08-debug-testing-perf/02-concurrency-benchmarks.md
  source_hash: d6361b12d5e3130e6c6bfe20c582d1644de5e58d3a5eec54b073a5d0cfff7f63
  translated_at: '2026-06-16T04:07:03.357320+00:00'
  engine: anthropic
  token_count: 4244
---
# Concurrency Performance Testing and Benchmarking

> 📖 **Deep Dive**: This article focuses on benchmarking in concurrent scenarios. For more general performance engineering—benchmarking methodology, cache friendliness, SIMD/AVX, and reading assembly—see [Volume 6: Performance Engineering](../../vol6-performance/index.md).

In the previous article, we solved the correctness problem—using TSan to catch data races, Helgrind to check lock order, and Clang TSA to prevent thread safety violations at compile time. However, a correct concurrent program is not necessarily an efficient one. We have seen too many scenarios where someone spends three days replacing a mutex with a lock-free queue, excitedly announcing a "3x performance boost," only to find that the benchmark methodology was flawed: a single run, no warm-up, the compiler nearly optimized away the entire loop, and even `DoNotOptimize` was missing. The "3x boost" you measured might just be measurement error.

In this article, our core problem to solve is: how to scientifically measure the performance of concurrent programs. We will start with the basic usage of Google Benchmark, then dive into the design traps of concurrent benchmarking (there are more pitfalls than you can imagine), followed by a real-world case study comparing the real performance differences of different synchronization schemes. Finally, we will introduce `perf stat`, a performance counter tool on Linux that can tell you exactly where your program is slow.

## Google Benchmark Basics

### Installation

Google Benchmark (hereinafter referred to as GBench) is the most mainstream micro-benchmarking framework in the C++ ecosystem, maintained by Google. There are several ways to install it; the simplest is using CMake's FetchContent:

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
target_link_libraries(your_target PRIVATE benchmark::benchmark)
```

If you prefer a system-wide installation:

```bash
# Ubuntu/Debian
sudo apt-get install libbenchmark-dev

# Or build and install from source
git clone https://github.com/google/benchmark.git
cd benchmark
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
make -j$(nproc)
sudo make install
```

### Your First Benchmark

The core idea of GBench is: you write a function, and the framework automatically decides how many iterations to run to get statistically reliable results. Let's write a simple example to get familiar with its API:

```cpp
#include <benchmark/benchmark.h>
#include <vector>

static void BM_VectorPushBack(benchmark::State& state) {
  // Code inside this loop is measured repeatedly
  for (auto _ : state) {
    std::vector<int> v;
    v.reserve(100);
    for (int i = 0; i < 100; ++i) {
      v.push_back(i);
    }
    benchmark::DoNotOptimize(v.data());
  }
}
BENCHMARK(BM_VectorPushBack);

BENCHMARK_MAIN();
```

Compile and run:

```bash
g++ -O3 -std=c++20 -lbenchmark benchmark_example.cpp -o benchmark_example
./benchmark_example
```

The output will look something like this:

```text
-------------------------------------------------------------
Benchmark              Time             CPU   Iterations
-------------------------------------------------------------
BM_VectorPushBack    45 ns           44 ns     15800000
```

Meaning of each column: `Time` is the wall clock time, `CPU` is the CPU time (the actual time the process spent on the CPU, including user mode and kernel mode), and `Iterations` is how many times the framework ran the loop. For single-threaded benchmarks, Time and CPU should be very close; but for multi-threaded benchmarks, CPU time will be the sum of CPU times of all threads—which is why we need `->UseRealTime()`.

### Multi-threaded Benchmark

GBench natively supports multi-threaded testing. Use `->Threads(n)` to specify the thread count, or use `->ThreadRange(1, 8)` to automatically iterate through different thread counts:

```cpp
static void BM_AtomicIncrement(benchmark::State& state) {
  for (auto _ : state) {
    // Simulate some work
    benchmark::DoNotOptimize(state.iterations());
  }
}

// Run with 1, 2, 4, 8 threads
BENCHMARK(BM_AtomicIncrement)->ThreadRange(1, 8)->UseRealTime();
```

Here are a few key points to explain. `ThreadRange(1, 8)` tells the framework to run this benchmark with 1, 2, 4, and 8 threads (powers of two). `UseRealTime()` is critical—without it, the framework reports CPU time by default. Under multi-threading, CPU time is the sum of all threads' time. For example, if 4 threads run for 100ms of wall time, CPU time might be 350ms (due to waiting and scheduling overhead). If you report CPU time, you might think "it got slower"—which is completely misleading. `DoNotOptimize` acts as a compiler-level memory barrier, telling the compiler "do not cache any memory state," preventing the optimizer from optimizing away our atomic operations.

The output will be similar to:

```text
----------------------------------------------------------------------
Benchmark                        Time           CPU   Iterations
----------------------------------------------------------------------
BM_AtomicIncrement/1:real       12 ns          12 ns     58000000
BM_AtomicIncrement/2:real       15 ns          28 ns     46000000
BM_AtomicIncrement/4:real       22 ns          79 ns     31000000
BM_AtomicIncrement/8:real       35 ns         267 ns     20000000
```

Look at the CPU column: the more threads, the higher the total CPU time, but the wall time (Time column) does not decrease linearly—there is some speedup from 1 to 2 threads, but it actually gets slower at 4 and 8 threads. This is because all threads are performing write operations on the same atomic variable, causing cache lines to bounce back and forth between cores (similar to the false sharing mechanism, but strictly speaking, it is cache line contention under true sharing). This is a very typical pattern in concurrent performance analysis: more threads does not mean faster.

## Concurrent Benchmark Design Traps

Writing a correct benchmark is harder than writing a correct concurrent program—because you have to fight compiler optimizations, CPU cache behavior, and OS scheduling policies. These factors cause trouble in single-threaded benchmarks, and even more so in multi-threaded ones.

### Warm-up: Cold Start vs. Steady State

The CPU's cache hierarchy (L1, L2, L3) has an order-of-magnitude impact on performance. The first time you access data, it may need to be loaded from main memory (DRAM), taking 100-300 CPU cycles; the second time, it's already in L1 cache, taking only 3-4 cycles. If your benchmark doesn't warm up, the data load from the first iteration will severely skew the average time.

GBench's `for (auto _ : state)` loop does a certain amount of warm-up—the framework runs a few iterations first to "stabilize" results. But if you allocate a large block of memory outside the loop, that memory might not be in the cache during the first iteration. If your goal is to measure "steady-state" performance, you can manually run a few rounds before the loop:

```cpp
static void BM_CacheWarmup(benchmark::State& state) {
  std::vector<int> data(1024 * 1024); // Large data

  // Manual warm-up
  for (int i = 0; i < 10; ++i) {
    for (auto& val : data) benchmark::DoNotOptimize(val);
  }

  for (auto _ : state) {
    for (auto& val : data) benchmark::DoNotOptimize(val);
  }
}
BENCHMARK(BM_CacheWarmup);
```

Conversely—if you want to measure "cold start" performance (e.g., the latency of an operation's first execution), you should not warm up. The key is to know exactly what you are measuring.

### Compiler Optimization: Your Adversary

This is the easiest trap to fall into. The compiler's job is to make your code fast—but your goal is to measure the raw speed of the code. If the compiler finds that your calculation results are not used, it might optimize away the entire loop. If the compiler finds that you are doing the same calculation in every loop, it might hoist it out of the loop and calculate it only once.

GBench provides two key tools to combat these problems:

```cpp
benchmark::DoNotOptimize(x);  // Prevents variable elimination
benchmark::ClobberMemory();   // Prevents memory read caching
```

A practical pattern is to use them in combination:

```cpp
static void BM_CompilerTrick(benchmark::State& state) {
  int x = 0;
  for (auto _ : state) {
    x++; // Compiler might optimize this away
    benchmark::DoNotOptimize(x); // Keep x
    benchmark::ClobberMemory();  // Force reload of memory
  }
}
BENCHMARK(BM_CompilerTrick);
```

`DoNotOptimize` ensures `x` is not optimized away, and `ClobberMemory` ensures memory reads in each loop are not optimized to "I read it last time, just reuse it." But be careful not to abuse `ClobberMemory`—it tells the compiler that all memory may have been modified, so the compiler must conservatively reload all values cached in registers, which in some scenarios introduces extra memory access overhead, making the measured performance worse than the actual situation.

### False Sharing: The Invisible Performance Killer

False sharing is a killer of concurrent performance—two threads modify different variables, but these variables happen to be on the same cache line (usually 64 bytes), causing every write to invalidate the other core's cache line. Let's use a benchmark to intuitively feel its power:

```cpp
struct BadCounter {
  std::atomic<int> value;
};

struct PaddedCounter {
  alignas(64) std::atomic<int> value; // Force to separate cache lines
};

static void BM_FalseSharing(benchmark::State& state) {
  const int num_threads = state.range(0);
  std::vector<BadCounter> counters(num_threads);

  for (auto _ : state) {
    // Each thread increments its own counter
    counters[state.thread_index].value.fetch_add(1, std::memory_order_relaxed);
  }
}

static void BM_NoFalseSharing(benchmark::State& state) {
  const int num_threads = state.range(0);
  std::vector<PaddedCounter> counters(num_threads);

  for (auto _ : state) {
    counters[state.thread_index].value.fetch_add(1, std::memory_order_relaxed);
  }
}

BENCHMARK(BM_FalseSharing)->Range(1, 8)->Threads(8)->UseRealTime();
BENCHMARK(BM_NoFalseSharing)->Range(1, 8)->Threads(8)->UseRealTime();
```

After compiling and running, you will see results similar to this (specific numbers depend on your CPU):

```text
----------------------------------------------------------------------
Benchmark                           Time           CPU   Iterations
----------------------------------------------------------------------
BM_FalseSharing/1/real             10 ns          10 ns     70000000
BM_FalseSharing/2/real              45 ns          88 ns     16000000
BM_FalseSharing/4/real             180 ns         710 ns      3900000
BM_FalseSharing/8/real             650 ns        5100 ns      1100000
BM_NoFalseSharing/1/real            9 ns           9 ns     77000000
BM_NoFalseSharing/2/real           9 ns          18 ns     39000000
BM_NoFalseSharing/4/real           10 ns          38 ns     18000000
BM_NoFalseSharing/8/real           11 ns          85 ns      8200000
```

In the version without padding, the more threads, the slower it gets—because each core's write has to kick out the other cores' cache lines, and the cache coherence protocol (MESI) overhead grows super-linearly with the number of threads (roughly O(n²), because each write needs to notify the other n-1 cores). With padding, each counter occupies its own cache line, threads don't interfere with each other, and performance barely changes with thread count. This difference can reach nearly 8x at 8 threads—this is the real lethality of false sharing.

### Thread Creation: Don't Create Threads in the Loop

Do not create and destroy threads inside the benchmark loop. Thread creation is an expensive operation—the kernel needs to allocate stack space for it, initialize the thread control block, and register it with the scheduler—usually taking 50-200 microseconds on Linux. If you `std::thread t(...)` in every iteration, you are measuring thread creation overhead, not the logic you want to test:

```cpp
// WRONG: Creating threads in the loop
static void BM_BadThreadCreation(benchmark::State& state) {
  for (auto _ : state) {
    std::thread t([]{ /* do work */ });
    t.join();
  }
}
```

The correct way is to create threads outside the loop (e.g., using a thread pool), and inside the loop only submit tasks and wait for results. GBench's `->Threads(n)` has already created threads for you outside the loop; you just need to do the actual work inside the loop body.

## Real-world: Comparing Different Synchronization Schemes

Enough theory, let's do a real comparison experiment. We will use GBench to test the performance differences of three synchronization schemes under the same workload: `std::mutex`, spinlock, and `std::atomic` CAS loop. The test scenario is multiple threads concurrently incrementing a shared counter—the simplest but most classic concurrent micro-benchmark.

```cpp
#include <benchmark/benchmark.h>
#include <mutex>
#include <atomic>

std::mutex mtx;
std::atomic<int> atomic_counter(0);
int normal_counter = 0;

static void BM_Mutex(benchmark::State& state) {
  for (auto _ : state) {
    std::lock_guard<std::mutex> lock(mtx);
    normal_counter++;
  }
}

static void BM_Atomic(benchmark::State& state) {
  for (auto _ : state) {
    atomic_counter.fetch_add(1, std::memory_order_relaxed);
  }
}

static void BM_Spinlock(benchmark::State& state) {
  for (auto _ : state) {
    // Simple TAS spinlock
    while (atomic_flag.test_and_set(std::memory_order_acquire)) {
      // spin
    }
    normal_counter++;
    atomic_flag.clear(std::memory_order_release);
  }
}

BENCHMARK(BM_Mutex)->Threads(1)->Threads(2)->Threads(4)->Threads(8);
BENCHMARK(BM_Atomic)->Threads(1)->Threads(2)->Threads(4)->Threads(8);
BENCHMARK(BM_Spinlock)->Threads(1)->Threads(2)->Threads(4)->Threads(8);
```

Let's analyze the results you will likely see (specific numbers vary by CPU, but the trend is universal).

In the single-threaded case, `std::atomic` is fastest (usually 1-2ns) because it maps directly to the CPU's `lock inc` instruction, no loop needed. `std::mutex` and spinlock have similar overhead (tens of nanoseconds) because with only one thread there is no contention, and the mutex fast path is just one atomic CAS. The CAS loop is somewhere in between.

In the multi-threaded case, things get interesting. `std::mutex` performance degrades as thread count increases, but the degradation is relatively mild—because when contention is high, the mutex suspends threads (via the futex system call), yielding the CPU to other threads. Spinlock performs worst under high contention—all threads are busy waiting, CPU usage is maxed out but effective work is low, and cache lines bounce repeatedly between cores. The CAS loop performance depends on contention: close to `std::atomic` under low contention, degrading due to repeated CAS failures under high contention. `std::atomic` is always fastest, but the degree of degradation depends on the CPU's atomic instruction implementation.

This experiment conveys an important engineering lesson: **lock-free does not equal high performance**. A CAS loop can be slower than a mutex under high contention because every failed CAS is a wasted CPU cycle. `std::atomic` is fast because the hardware directly supports this operation—it's not "optimized" via lock-free techniques, the CPU instruction set does it for you. When choosing a synchronization scheme, look at the specific access pattern and contention level, rather than simply saying "lock-free is better."

## Performance Counters: perf stat

Benchmarks tell you "how fast," but not "why fast" or "why slow." To answer the "why" question, we need performance counters—statistics provided by CPU hardware that tell you cache hit rates, branch prediction accuracy, context switch counts, and other low-level metrics. Linux's `perf` tool can read these counters.

### Basic Usage

The basic usage of `perf stat` is simple:

```bash
perf stat ./your_benchmark
```

For a concurrent program, the default `perf stat` output looks roughly like this:

```text
 Performance counter stats for './your_benchmark':

          1.23 msec task-clock                #    0.001 CPUs utilized
                 1      context-switches          #    0.001 K/sec
                 0      cpu-migrations            #    0.000 K/sec
               102      page-faults               #    0.083 K/sec
     4,567,890      cycles                    #    3.712 GHz
     2,345,678      instructions              #    0.51  insn per cycle
       456,789      cache-misses              #    0.10% of all cache refs
     3,456,789      L1-dcache-loads
       123,456      L1-dcache-load-misses     #    3.57% of all L1-dcache hits
       12,345      LLC-loads
         1,234      LLC-load-misses           #   10.00% of all LL-cache hits

       0.123456 seconds time elapsed
```

### Interpreting Key Metrics

The metric most worth watching is **cache-misses**, which tells you how many times the CPU didn't find data in the cache and had to go to main memory. A 2-3% cache-miss rate is normal for sequentially accessing programs, but for concurrent programs—if you find the cache-miss rate soaring with thread count, you can almost be certain there is false sharing or a data layout problem. The solution is to check if hot data is being frequently modified by multiple threads; if so, use `alignas(64)` to spread them to different cache lines.

Another important metric is **context-switches**, which reflects how frequently threads are swapped in and out by the OS. High context switching usually means threads are frequently blocking—waiting for mutex, waiting for I/O, or the number of threads far exceeds CPU cores causing over-scheduling. If an 8-thread program runs on 4 cores, context switching will be very frequent; in this case, you should reduce the thread count or use a thread pool to control concurrency.

If you notice the **cpu-migrations** number is high, it means threads are being moved from one core to another by the OS. CPU migration causes all L1/L2 caches to be invalidated (because L1/L2 are core-private), which has a huge impact on performance. In concurrent programs, if threads are frequently migrated, consider using `pthread_setaffinity_np` or `taskset` to bind threads to specific cores:

```bash
# Bind benchmark to cores 0-3
taskset -c 0-3 ./your_benchmark
```

The last comprehensive efficiency metric is **instructions per cycle (IPC)**. Modern superscalar CPUs can ideally execute 4-6 instructions per cycle (IPC > 1), so an IPC close to or exceeding 1 means the CPU pipeline is utilized well; an IPC far below 1 (e.g., 0.3-0.5) means the CPU is spending a lot of time waiting—waiting for cache, waiting for memory, waiting for branch resolution. Concurrent programs usually have lower IPC than equivalent single-threaded programs because synchronization operations (mutex lock, atomic CAS) introduce waiting and pipeline stalls.

### Real-world: Analyzing a Concurrent Program's Bottleneck

Let's take the `BM_Spinlock` (8-thread version) from the benchmark above and analyze it with perf:

```bash
perf stat -e cache-misses,cache-references,L1-dcache-loads,L1-dcache-load-misses,context-switches,cpu-migrations ./benchmark --benchmark_filter=BM_Spinlock/8
```

You might see output like this:

```text
 Performance counter stats for './benchmark':

      78,234,567      cache-references
      15,234,567      cache-misses              #   19.47 % of all cache refs
     234,567,890      L1-dcache-loads
      45,678,912      L1-dcache-load-misses     #   19.47 % of all L1-dcache hits
               0      context-switches
               0      cpu-migrations
```

A 19.5% cache-miss rate is very high for this simple counter—normally it should be below 5%. The culprit is the cache line contention of the spinlock under 8 threads: all threads are busy waiting on the state of the same `std::atomic_flag`, and every time a thread acquires or releases the lock, the cache line bounces between the other 7 cores, and the cache coherence protocol overhead dominates the execution time. Looking at L1-dcache-load-misses, the number is similarly high—the spinlock's busy-wait loop constantly reads the lock state, but every time the lock is released, the cache line has already been invalidated by another core's write.

As a comparison, switch to the `std::atomic` version for the same test:

```bash
perf stat -e cache-misses,cache-references,L1-dcache-loads,L1-dcache-load-misses ./benchmark --benchmark_filter=BM_Atomic/8
```

The cache-miss rate will drop below 5%, because `std::atomic` uses the `lock xadd` instruction to complete the read-modify-write operation atomically at the hardware level, without needing to repeatedly spin-read the lock state like a spinlock.

This perf analysis lets you know not just "which solution is faster," but "why it's faster"—is it higher cache efficiency? Fewer context switches? Or fewer instructions? With this low-level understanding, you have a basis for judgment when facing new optimization problems, rather than blindly trying things.

### Linking perf and Google Benchmark

Since v1.7, GBench supports reading hardware performance counters directly via the `--benchmark_perf_counters` parameter (Linux only), but a more general approach is to use an external wrapper to link with perf. A practical trick is to pipe GBench's output to a file and parse it with a script:

```bash
./benchmark --benchmark_out=results.json --benchmark_out_format=json
perf stat -o perf.txt ./benchmark
```

Then you can look at the two datasets together: GBench tells you latency and throughput, perf tells you cache and scheduling behavior.

## Where We Are

At this point, the journey through Volume 5 is drawing to a close. Let's review what we have learned along the way.

We started with the question "why do we need concurrency," understanding the difference between concurrency and parallelism, Amdahl's Law and Gustafson's Law, and the trade-off between throughput and latency. Then we learned thread lifecycle management and RAII wrappers, using `std::thread` and `std::jthread` to manage threads. Next were synchronization primitives—mutex, condition variable, RAII lock guards—to protect shared state. We dove into atomic operations and the memory model, understanding the cache coherence protocol behind `std::atomic` and happens-before relationships. Then we used this knowledge to build concurrent data structures—thread-safe queues, thread pools. After that, we entered the world of async I/O and coroutines, using C++20 coroutines to make async code as clear as sync code. Then came the Actor model and CSP, two "shared-nothing" concurrency paradigms. Finally, in these last two articles, we solved the two ultimate problems of concurrent programming: "how to ensure correctness" (debugging) and "how to confirm efficiency" (performance testing).

The thread of Volume 5 is a clear learning path: first understand the problem (why concurrency, what are the pitfalls), then master the tools (threads, locks, atomics, coroutines), then apply the tools to build components (data structures, thread pools), and finally use methodology to guarantee quality (debugging and testing). Each step in this path builds on the previous one; missing any link will lead to pitfalls in actual engineering.

But single-machine concurrency is just the beginning of the story. When one machine isn't enough—CPU power tops out, memory can't fit, network bandwidth is saturated—you need to distribute the problem across multiple machines. At that point, "concurrency" becomes "distributed," and the challenges you face in a distributed environment rise another order of magnitude: unreliable networks, inconsistent clocks, nodes that can crash at any time. In the next article, the final chapter of Volume 5, we will stand on the shoulders of single-machine concurrency and see which of our previous knowledge still applies and what must be rethought when concurrency crosses network boundaries.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `examples/vol5_concurrency/benchmark`.

## Reference Resources

- [Google Benchmark — GitHub](https://github.com/google/benchmark) — Official repo and complete documentation
- [perf stat — Linux Kernel Documentation](https://perf.wiki.kernel.org/index.php/Tutorial#Counting_with_perf_stat) — Official tutorial for the perf tool
- [Performance Analysis and Tuning of Linux Systems — Brendan Gregg](https://www.brendangregg.com/linuxperf.html) — Authoritative resource for Linux performance analysis
- [False Sharing — Intel VTune Profiler Cookbook](https://www.intel.com/content/www/us/en/docs/vtune-profiler/cookbook/2023-0/false-sharing.html) — Intel's guide on identifying and optimizing false sharing
- [C++ Atomic Operations and Performance — Fedor Pikus (CppCon 2017)](https://www.youtube.com/watch?v=ZQFzMfHIxng) — Deep analysis of atomic operation performance characteristics under different contention levels
