---
chapter: 8
cpp_standard:
- 17
- 20
description: Master Google Benchmark, steer clear of the common pitfalls of concurrent
  benchmarking, and learn to locate bottlenecks with performance counters
difficulty: intermediate
order: 2
platform: host
prerequisites:
- Debugging Techniques for Concurrent Programs
- Thread Pool Design
reading_time_minutes: 20
related:
- CPU Cache and OS Threads
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
  translated_at: '2026-09-26T06:44:04+00:00'
  engine: anthropic
  token_count: 11000
---
# Concurrency Performance Testing and Benchmarking

> 📖 **Further reading**: This article covers benchmarking only in concurrent scenarios. More general performance engineering—benchmark methodology, cache friendliness, SIMD/AVX, reading assembly—is the home turf of [Volume 6: Performance Engineering](../../vol6-performance/index.md).

In the previous article we settled the correctness question—catching data races with TSan, checking lock ordering with Helgrind, and preventing thread-safety violations at compile time with Clang TSA. But a correct concurrent program is not the same thing as an efficient concurrent program. We have seen this scene play out far too many times: someone spends three days swapping a mutex for a lock-free queue, excitedly announces a "3x performance improvement"—and then you look at the benchmark methodology: a single run, no warm-up, a compiler that very nearly optimized the entire loop away, and not even `UseRealTime` in sight. That "3x improvement" you measured might be nothing more than measurement error.

The core question of this article is: how do we measure the performance of a concurrent program scientifically? We will start with the basics of Google Benchmark, then dig into the design pitfalls of concurrent benchmarking (there are far more of them than you would imagine), then compare the real performance differences between synchronization strategies through a hands-on experiment, and finally introduce `perf stat`, the Linux performance-counter tool—it can tell you where your program is actually slow.

## Google Benchmark Basics

### Installation

Google Benchmark (GBench from here on) is the most widely used microbenchmarking framework in the C++ ecosystem, open-sourced and maintained by Google. There are several ways to install it; the simplest is CMake's FetchContent:

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
# Don't let GBench run its own tests; saves compile time
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

The core idea behind GBench is this: you write a function, and the framework automatically decides how many iterations to run to obtain statistically reliable results. Let's write the simplest possible example first to get familiar with its API:

```cpp
#include <benchmark/benchmark.h>
#include <vector>
#include <numeric>

// A simple summation benchmark
static void bm_vector_sum(benchmark::State& state)
{
    // Setup phase: not inside the timed region
    std::vector<int> data(10000, 42);

    // Timing loop: the framework executes this code repeatedly
    for (auto _ : state) {
        int sum = std::accumulate(data.begin(), data.end(), 0);
        // Prevent the compiler from optimizing sum away
        benchmark::DoNotOptimize(sum);
    }

    // Optional: report extra information
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

The output looks roughly like this:

```text
-------------------------------------------------------
Benchmark             Time           CPU      Iterations
-------------------------------------------------------
bm_vector_sum      1234 ns       1234 ns       567890
```

What each column means: `Time` is wall time, `CPU` is CPU time (the time the process actually spends on the CPU, including both user mode and kernel mode), and `Iterations` is how many iterations the framework ran. For a single-threaded benchmark, Time and CPU should be very close; for a multithreaded benchmark, however, the CPU time is the sum of the CPU time across all threads—which is why we need `UseRealTime`.

### Multithreaded Benchmarks

GBench supports multithreaded benchmarking natively. You specify the thread count with `Threads(n)`, or use `ThreadRange` to sweep through different thread counts automatically:

```cpp
#include <benchmark/benchmark.h>
#include <atomic>

// A multithreaded benchmark of an atomic counter
static void bm_atomic_counter(benchmark::State& state)
{
    std::atomic<int> counter{0};
    const int num_threads = state.threads();

    for (auto _ : state) {
        // Each thread performs one atomic increment
        counter.fetch_add(1, std::memory_order_relaxed);
        benchmark::ClobberMemory();
    }

    // Use wall time; otherwise CPU time accumulates across threads
    state.SetItemsProcessed(state.iterations());
}

// Test with 1/2/4/8 threads
BENCHMARK(bm_atomic_counter)
    ->ThreadRange(1, 8)
    ->UseRealTime();

BENCHMARK_MAIN();
```

A few key points deserve explanation. `ThreadRange(1, 8)` makes the framework run the benchmark with 1, 2, 4, and 8 threads (powers of two). `UseRealTime()` is critically important—without it, the framework reports CPU time by default, and under multithreading the CPU time is the sum across all threads. Say 4 threads run for 100 ms of wall time; the CPU time might be 350 ms (because of waiting and scheduling overhead), and if that is what gets reported you would think things "got slower"—purely misleading. `ClobberMemory()` is a compiler-level memory barrier that tells the compiler "do not cache any memory state", preventing the optimizer from optimizing our atomic operations away.

The output will look something like this:

```text
------------------------------------------------------------------
Benchmark                        Time          CPU      Iterations
------------------------------------------------------------------
bm_atomic_counter/1           2.3 ns       2.3 ns    300000000
bm_atomic_counter/2           1.8 ns       3.5 ns    400000000
bm_atomic_counter/4           2.1 ns       7.8 ns    333333333
bm_atomic_counter/8           3.5 ns       25 ns     200000000
```

Watch the CPU column: the more threads, the higher the total CPU time, but the wall time (the Time column) does not drop linearly—going from 1 to 2 threads buys some speedup, yet at 4 and 8 threads things actually get slower. That is because every thread is performing writes on the same atomic variable, so the cache line keeps being invalidated back and forth between cores (the mechanism resembles false sharing, but strictly speaking this is cache-line contention under true sharing). This is a very typical pattern in concurrency performance analysis: more threads does not mean faster.

## Design Pitfalls in Concurrent Benchmarking

Writing a correct benchmark is even harder than writing a correct concurrent program—because you have to fight the compiler's optimizations, the CPU's caching behavior, and the operating system's scheduling policy. These factors already make trouble in single-threaded benchmarks, and in multithreaded benchmarks they get even worse.

### Warm-up: Cold Start and Steady State

The CPU's cache hierarchy (L1, L2, L3) affects performance by orders of magnitude. The first time you access a piece of data, it may have to be loaded from main memory (DRAM), costing 100-300 CPU cycles; on the second access it is already in the L1 cache and takes only 3-4 cycles. If your benchmark does no warm-up, the data loading in the first iteration will seriously inflate the average time.

GBench's `KeepRunning()` loop performs a certain amount of warm-up itself—the framework first runs a small number of iterations to "settle" the results. But if you allocate a large block of memory outside the loop, that memory may not be in the cache during the first iteration. If your goal is to measure "steady-state" performance, you can run a few manual passes before the loop:

```cpp
static void bm_with_warmup(benchmark::State& state)
{
    std::vector<int> data(10000);

    // Warm-up: pull the data into the cache
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

But flip it around—if what you want to measure is "cold-start" performance (say, the first-execution latency of some operation), then you should not warm up at all. The key is to be clear about what you are measuring.

### Compiler Optimizations: Your Opponent

This is the easiest trap to fall into. The compiler's job is to make your code faster—but your goal is to measure the code's raw speed. If the compiler discovers that your computation's result is never used, it may optimize the entire loop away. If it discovers that every iteration performs the same computation, it may hoist that computation out of the loop and calculate it only once.

GBench provides two key tools to fight these problems:

```cpp
// benchmark::DoNotOptimize(expr)
// Tell the compiler the value of expr "might" be used externally; do not optimize it away
benchmark::DoNotOptimize(result);

// benchmark::ClobberMemory()
// Tell the compiler that all memory state may be modified externally
// Equivalent to a global read-write barrier
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

`DoNotOptimize` guarantees that `result` is not optimized away, and `ClobberMemory` guarantees that each iteration's memory reads are not optimized into "we already read this last time, just reuse it". But do not overuse `ClobberMemory`—it tells the compiler that all memory may have been modified, so the compiler must conservatively reload every value it had cached in registers. In some scenarios this introduces extra memory-access overhead and makes the performance you measure worse than it really is.

### False Sharing: The Invisible Performance Killer

False sharing is a concurrency performance killer—two threads each modify different variables, but those variables happen to sit on the same cache line (usually 64 bytes), so every write has to invalidate the other core's copy of the line. Let's feel its destructive power firsthand with a benchmark:

```cpp
#include <benchmark/benchmark.h>
#include <vector>
#include <thread>

// Version that suffers from false sharing
struct alignas(64) PaddedCounter {
    int value{0};
    // Padding out to 64 bytes to avoid false sharing
    char padding[60];
};

static void bm_false_sharing(benchmark::State& state)
{
    const int num_threads = state.threads();

    // Deliberately pack the counters tightly—manufacturing false sharing
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

    // Each counter owns a full cache line
    auto* counters = new PaddedCounter[num_threads];

    for (auto _ : state) {
        int idx = state.thread_index();
        counters[idx].value++;
        benchmark::ClobberMemory();
    }

    delete[] counters;
}

// Compare under multiple threads
BENCHMARK(bm_false_sharing)->ThreadRange(2, 16)->UseRealTime();
BENCHMARK(bm_no_false_sharing)->ThreadRange(2, 16)->UseRealTime();

BENCHMARK_MAIN();
```

After compiling and running, you will see results along these lines (exact numbers depend on your CPU):

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

The unpadded version gets slower as the thread count grows—because every write from every core has to kick the other cores' cache lines out, and the cost of the cache-coherence protocol (MESI) grows superlinearly with the thread count (roughly O(n²), since every write has to notify the other n-1 cores). With padding, each counter owns a cache line, threads stop interfering with one another, and performance is nearly independent of the thread count. At 8 threads the difference can approach 8x—that is the real destructive power of false sharing.

### Thread Creation: Don't Spawn Threads Inside the Loop

Do not create and destroy threads inside the benchmark loop. Thread creation is an expensive operation—the kernel has to allocate a stack for it, initialize the thread control block, and register it with the scheduler—and on Linux it typically costs 50-200 microseconds. If every iteration does `std::thread(...) + join()`, the bulk of what you measure is thread-creation overhead rather than the logic you wanted to measure:

```cpp
// A bad example: thread creation inside the loop
static void bm_bad(benchmark::State& state)
{
    for (auto _ : state) {
        // Create and destroy a thread every iteration—you are measuring thread creation, not your business logic
        std::thread t([]() { /* do something trivial */ });
        t.join();
    }
}
```

The right approach is to create threads outside the loop (with a thread pool, for instance) and only submit tasks and wait for results inside the loop. GBench's `Threads(n)` already creates the threads for you outside the loop; all you need to do inside the loop body is the actual work.

## Hands-On: Comparing Synchronization Strategies

Enough theory—let's run an actual comparison experiment. We will use GBench to measure how three synchronization strategies differ in performance under the same workload: `std::mutex`, a spinlock, and a CAS loop over `std::atomic`. The test scenario is multiple threads concurrently incrementing a shared counter—the simplest yet most classic concurrency microbenchmark there is.

```cpp
#include <benchmark/benchmark.h>
#include <mutex>
#include <atomic>
#include <thread>

// Strategy 1: std::mutex
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

// Strategy 2: spinlock
class SpinLock {
public:
    void lock()
    {
        while (locked_.test_and_set(std::memory_order_acquire)) {
            // Hint to the CPU that we are spin-waiting; reduces power draw and improves hyperthread performance
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
    std::atomic_flag locked_ = ATOMIC_FLAG_INIT;  // Initialization macro available since C++11
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

// Strategy 3: atomic CAS
static void bm_atomic_cas_counter(benchmark::State& state)
{
    std::atomic<int> counter{0};

    for (auto _ : state) {
        // CAS loop: optimistic concurrency
        int expected = counter.load(std::memory_order_relaxed);
        while (!counter.compare_exchange_weak(
            expected, expected + 1,
            std::memory_order_acq_rel,
            std::memory_order_relaxed))
        {
            // CAS failed; expected has been updated to the current value, retry
        }
    }
    benchmark::DoNotOptimize(counter);
}

// Another atomic strategy: fetch_add
static void bm_atomic_fetch_add_counter(benchmark::State& state)
{
    std::atomic<int> counter{0};

    for (auto _ : state) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
    benchmark::DoNotOptimize(counter);
}

// Register all benchmarks; test with 1/2/4/8 threads
BENCHMARK(bm_mutex_counter)->ThreadRange(1, 8)->UseRealTime();
BENCHMARK(bm_spinlock_counter)->ThreadRange(1, 8)->UseRealTime();
BENCHMARK(bm_atomic_cas_counter)->ThreadRange(1, 8)->UseRealTime();
BENCHMARK(bm_atomic_fetch_add_counter)->ThreadRange(1, 8)->UseRealTime();

BENCHMARK_MAIN();
```

Let's walk through the results you will roughly see (exact numbers vary by CPU, but the trends are universal).

Single-threaded, `fetch_add` is the fastest (usually 1-2 ns), because it maps directly to the CPU's `LOCK XADD` instruction—no loop needed. `mutex` and `spinlock` cost about the same (tens of nanoseconds), because with only one thread there is no contention, and the mutex's fast path is just one atomic CAS. The CAS loop lands in between.

With multiple threads, things get interesting. `mutex` performance degrades as the thread count grows, but the degradation is relatively mild—under heavy contention a mutex suspends threads (via the futex syscall) and yields the CPU for other threads to use. `spinlock` performs worst under high contention—all threads are busy-waiting, CPU utilization is maxed out while little useful work gets done, and the cache line is invalidated back and forth between cores. The CAS loop's behavior depends on the contention level: close to `fetch_add` at low contention, degrading under high contention because of repeated CAS failures. `fetch_add` is always the fastest, though how much it degrades depends on the CPU's atomic-instruction implementation.

This experiment delivers an important engineering lesson: **lock-free does not mean high performance**. Under heavy contention a CAS loop can be slower than a mutex, because every failed CAS is a wasted CPU cycle. `fetch_add` is fast because the hardware supports this operation directly—it is not a speedup "lock-free" tricks optimized into existence; the CPU instruction set does it for you. When choosing a synchronization strategy, look at the concrete access pattern and contention level instead of simply declaring "lock-free is better".

## Performance Counters: perf stat

A benchmark tells you "how fast", but not "why it is fast" or "why it is slow". To answer the "why" questions we need performance counters—statistics provided by the CPU hardware that expose low-level metrics such as cache hit rates, branch-prediction accuracy, and context-switch counts. On Linux, the `perf` tool can read these counters.

### Basic Usage

The basic usage of `perf stat` is simple:

```bash
# Run the program directly
perf stat ./your_program

# Focus on specific events only
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

### Interpreting the Key Metrics

The metric most worth watching is **cache-misses**, which tells you how many times the CPU failed to find data in the cache and had to fetch it from main memory. A 2-3% cache-miss rate is normal for a program with a sequential access pattern, but for a concurrent program—if you notice the cache-miss rate soaring as the thread count grows, you can be almost certain there is false sharing or a data-layout problem. The fix is to check whether hot data is modified frequently by multiple threads, and if so, use `alignas(64)` to spread it across different cache lines.

Another important metric is **context-switches**, which reflects how often the operating system swaps threads in and out. A high context-switch count usually means threads are blocking frequently—waiting on a mutex, waiting on I/O, or being overscheduled because the thread count far exceeds the number of CPU cores. If an 8-thread program runs on 4 cores, context switches will be very frequent; at that point you should reduce the thread count or use a thread pool to control the degree of concurrency.

If you notice the **cpu-migrations** number running high, it means the operating system is moving threads from one core to another. CPU migration invalidates the entire L1/L2 cache (because L1/L2 are private to each core), which hurts performance significantly. In concurrent programs where threads are migrated frequently, consider pinning threads to specific cores with `pthread_setaffinity_np` or `taskset`:

```bash
# Run on cores 0-3 only
taskset -c 0-3 ./your_program
```

The final, overarching efficiency metric is **instructions per cycle (IPC)**. A modern superscalar CPU can ideally execute 4-6 instructions per cycle (IPC > 1), so an IPC close to or above 1 means the CPU's pipeline utilization is decent; an IPC far below 1 (say 0.3-0.5) means the CPU is spending much of its time waiting—on caches, on memory, on branch resolution. A concurrent program's IPC is usually lower than that of an equivalent single-threaded program, because synchronization operations (mutex lock, atomic CAS) introduce waiting and pipeline breaks.

### Hands-On: Analyzing a Concurrent Program's Bottleneck

Let's pull `bm_spinlock_counter` (the 8-thread version) out of the benchmark above and analyze it with perf:

```bash
# Compile
clang++ -O2 -std=c++17 -pthread spinlock_bench.cpp -lbenchmark -lpthread -o spinlock_bench

# Run under perf
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
        67,890,123      L1-dcache-load-misses   # high L1 misses
         5,678,901      llc-load-misses

      12.345678 seconds time elapsed
```

A 19.5% cache-miss rate is very high for such a simple counter—normally it should stay below 5%. The culprit is cache-line contention on the spinlock with 8 threads: every thread is busy-waiting on the state of the same `atomic_flag`, and every time some thread acquires or releases the lock, the cache line is invalidated back and forth across the other 7 cores, so the cache-coherence protocol's overhead occupies most of the execution time. Look at L1-dcache-load-misses too—the number is likewise high, because the spinlock's busy-wait loop keeps reading the lock state, but by the time the lock is released, the cache line has already been invalidated by another core's writes.

For comparison, run the same test with the `fetch_add` version:

```bash
perf stat -e cache-misses,cache-references,context-switches,cpu-migrations \
./spinlock_bench --benchmark_filter=bm_atomic_fetch_add_counter/8
```

The cache-miss rate drops below 5%, because the `LOCK XADD` instruction that `fetch_add` uses completes the read-modify-write atomically at the hardware level—no repeatedly spinning on the lock state the way a spinlock does.

This kind of perf analysis tells you not only "which strategy is faster" but also "why it is faster"—better cache efficiency? Fewer context switches? Fewer instructions? With that low-level understanding in hand, you have grounds for judgment when a new optimization problem comes along, instead of guessing blindly.

### Combining perf with Google Benchmark

Since v1.7, GBench supports reading hardware performance counters directly through the `--benchmark_perf_counters` flag (Linux only), but the more general approach is to pair it with perf through external wrapping. A practical trick is to redirect GBench's output to a file and then parse it with a script:

```bash
# Output in CSV format
./your_bench --benchmark_format=csv > results.csv

# Meanwhile collect hardware counters with perf
perf stat -o perf_results.txt ./your_bench --benchmark_filter=your_benchmark
```

Then you can read the two datasets side by side: GBench tells you latency and throughput, perf tells you cache and scheduling behavior.

## Where We Are

At this point, our journey through Volume 5 is nearing its end. Let's look back at what we have learned along the way.

We started from the question "why do we need concurrency" and came to understand the difference between concurrency and parallelism, Amdahl's law and Gustafson's law, and the trade-off between throughput and latency. Then we studied thread lifecycle management and RAII wrappers, keeping threads under control with `std::thread` and `std::jthread`. Next came synchronization primitives—mutex, condition variable, RAII lock guard—for protecting shared state. We dug into atomic operations and the memory model, and understood the cache-coherence protocols and happens-before relationships behind `memory_order`. Then we used that knowledge to build concurrent data structures—a thread-safe queue, a thread pool. After that we entered the world of asynchronous I/O and coroutines, using C++20 coroutines to make asynchronous code as clear as synchronous code. Then came two "no shared memory" concurrency paradigms: the Actor model and CSP. And in these last two articles, we tackled the two ultimate questions of concurrent programming: "how do I ensure correctness" (debugging) and "how do I confirm efficiency" (performance testing).

The through-line of Volume 5 is a clear learning path: first understand the problem (why concurrency, and what pitfalls it holds), then master the tools (threads, locks, atomics, coroutines), then apply the tools to build components (data structures, thread pools), and finally safeguard quality with methodology (debugging and testing). Each step of this path builds on the previous one; miss any single link and you will stumble in real-world engineering.

But single-machine concurrency is only the beginning of the story. When one machine is no longer enough—CPU compute maxed out, memory stretched beyond capacity, network bandwidth saturated—you need to spread the problem across multiple machines. At that point, "concurrency" becomes "distribution", and the challenges you face in a distributed setting go up another order of magnitude: unreliable networks, inconsistent clocks, nodes that can drop dead at any moment. In the next article—the final chapter of Volume 5—we will stand on the shoulders of single-machine concurrency and see which of what we learned still holds when concurrency crosses network boundaries, and what we have to rethink.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); visit `code/volumn_codes/vol5/ch08-debug-testing-perf/`.

## References

- [Google Benchmark — GitHub](https://github.com/google/benchmark) — the official repository and full documentation
- [perf stat — Linux Kernel Documentation](https://perf.wiki.kernel.org/index.php/Tutorial#Counting_with_perf_stat) — the official tutorial for the perf tool
- [Performance Analysis and Tuning of Linux Systems — Brendan Gregg](https://www.brendangregg.com/linuxperf.html) — the authoritative resource on Linux performance analysis
- [False Sharing — Intel VTune Profiler Cookbook](https://www.intel.com/content/www/us/en/docs/vtune-profiler/cookbook/2023-0/false-sharing.html) — Intel's guide to identifying and optimizing away false sharing
- [C++ Atomic Operations and Performance — Fedor Pikus (CppCon 2017)](https://www.youtube.com/watch?v=ZQFzMfHIxng) — a deep analysis of how atomic operations behave at different contention levels
