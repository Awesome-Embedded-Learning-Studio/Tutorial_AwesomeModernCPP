---
title: "Benchmark Tools and Three Variables: Benchmark, Data Size, Granularity, Thread Count"
description: "How to use Google Benchmark, measured readings for the three variables of data size, task granularity, and thread count, plus reading hardware counters with perf stat."
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
translation:
  source: documents/vol5-concurrency/chapter06-validating-designs/04-benchmark-and-variables.md
  source_hash: 3be2e4bf407fa9d86b15c8128189f8a2b9d3efc03aa7f75c4ea7a741d1dbc012
  translated_at: '2026-11-22T13:44:00+00:00'
  engine: glm-5.3
  token_count: 13757
---

# Benchmark Tools and Three Variables: Benchmark, Data Size, Granularity, Thread Count

The conditions are pinned down; time to bring in the tools. Google Benchmark usage, data size, task granularity, thread count — four names this article reads against one another, ending with perf stat for the hardware counters.

## Getting Started with Google Benchmark

### Install It and Run the Simplest Benchmark

Google Benchmark, the most widely used microbenchmark framework in the C++ ecosystem. Two ways to install it: the system package — the Debian and Ubuntu package name is `libbenchmark-dev` — or CMake's FetchContent to pull the source straight into the build. The FetchContent setup is below. The version is v1.10.0, checked on 2026-11-17, which was the latest release on the GitHub releases page at the time. Check again when you use it:

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

The way you use it is to write a function taking `benchmark::State&`; the framework runs the loop inside repeatedly, decides the iteration count dynamically, and reports the average at the end. Hand our summation to it:

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

`for (auto _ : state)` is the timed loop; every pass of the loop body records one iteration. The official guide recommends the ranged-for form first, because the traditional `KeepRunning()` adds a memory read and write per iteration. Once installed and running, you will see a three-column table: Time, CPU, Iterations. The column format comes from the v1.10.0 user guide, with the readings swapped for our benchmark just now — here is the look of it:

```text
-------------------------------------------------------
Benchmark                    Time       CPU      Iterations
-------------------------------------------------------
bm_sum_range/100000000     51.5 ms    51.5 ms           10
```

The values are illustrative; they line up with our measured 51 milliseconds, and the iteration count folded out of the default 0.5-second floor is around ten too. Time is wall-clock time, CPU is CPU time, and Iterations is the number of iterations actually run. In a single-threaded benchmark the two time columns nearly coincide; in a multithreaded benchmark they split: CPU time is the sum over all threads — 8 threads running 100 milliseconds of wall clock can come out as 700 milliseconds of CPU time. Which column to watch depends on which question you are answering.

### Multithreaded Benchmarks: Threads and UseRealTime

To measure thread count, the framework offers two routes. One is to entrust it to the framework: write `Threads(8)` or `ThreadRange(1, 8)` at registration, the latter stepping through powers of two — 1, 2, 4, 8 — one pass each. The framework guarantees that timing starts only when every thread has reached the loop's starting point and ends only when all have exited, so the starting line is even — the very job the gate does in our concurrent experiment code. The other route is building the threads inside the function yourself, matching our scaffolding's approach. The official documentation recommends pairing self-built-thread benchmarks with `UseRealTime()`, because by default the framework measures the main thread's CPU time; the wall-clock time the worker threads spend working is invisible to it, and the reading skews beyond all bounds.

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

`state.threads()` hands you the current tier's thread count; one copy of the code runs all five tiers — 1, 2, 4, 8, 16 — and the readings for the thread-count variable line up this way.

### The Timing Harness This Article Uses

All readings in this article come from the timing harness we built by hand. It is small; the full picture is below, the concepts line up with the framework's, and once you have the library installed you can switch back at any time:

<OnlineCompilerDemo
  title="A hand-built timing harness: one round at a time, readings delivered round by round"
  source-path="code/examples/vol5/chapter06/14_timing_harness.cpp"
  description="The three pieces do_not_optimize, clobber_memory, and measure_ms, with a main that runs the one-hundred-million summation six rounds in a row; each round's milliseconds are printed as-is, with no averaging."
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

The harness's timed loop measures one round at a time, the caller arranges multiple rounds, readings are handed over round by round with no averaging — we chose it precisely to get every round's raw reading in hand. `do_not_optimize` corresponds to `benchmark::DoNotOptimize`; following the v1.10.0 spelling, we wrote it as a non-const reference plus read/write constraints. `clobber_memory` corresponds to `benchmark::ClobberMemory`, which the officials changed to `atomic_signal_fence` in v1.10.0 — a fence constraining only the memory ordering on the signal-handler path — while what we lay out here is still the old-style empty-assembly spelling. It has none of the framework's dynamic iterations and no warm-up at all; what that buys is visibility of every round's reading, exactly what this article wants: raw readings listed in full, and the order of magnitude speaks for itself.

## Data Size: Where the Payoff Starts

Of the three variables, we start by moving data size. The same summation job, 8 threads, size stepping from one million up to one hundred million, with three rounds of readings gathered into one table:

| Elements | Serial (ms) | 8 threads (ms) | Speedup |
|---|---|---|---|
| 1 million | 0.50 - 0.54 | 0.23 - 0.60 | 0.91 - 2.17x |
| 10 million | 5.14 - 5.68 | 1.31 - 1.36 | 3.78 - 4.17x |
| 100 million | 51.22 - 51.68 | 11.60 - 12.80 | 4.00 - 4.46x |

The table gathers the ranges of the three rounds. The three raw readings for the one-million and one-hundred-million tiers were already listed in full in Before Measuring, Pin the Conditions Down; the ten-million tier we supply here: serial 5.14, 5.68, 5.26 milliseconds, 8 threads 1.36, 1.36, 1.31 milliseconds, speedups 3.78, 4.17, 4.03x.

As the size goes up, the speedup goes with it, then flattens out past 10 million. A job of one million elements is not enough for 8 threads to share; entry and exit eat it all. In real concurrent workloads, size affects the payoff even more directly: as data grows, the serial fraction (summing up, validating, writing results) gets diluted instead — which is also the starting point of Gustafson's law, as an extension section of [When Is Concurrency Worth Introducing](../chapter01-start-concurrency/08-when-concurrency) covered. When you measure sizes for your own program, set the starting point where the serial time and the thread entry overhead are of the same order of magnitude, and search upward from there for the payoff's turning point.

## Task Granularity: How Fine to Cut

We fix the total work at two hundred million additions, 8 threads, with the chunk count stepping from 8 up to twenty million, three rounds per tier:

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

Look at the readings: from 8 chunks to 800, the time drops from 22 milliseconds to around 18. Eight chunks means each thread claims one chunk at the start and clocks out when it finishes — one chunk is twenty-five million additions, so whoever drew the heavy load carries it to the end alone; nobody can help. Eight hundred chunks let faster threads claim a few more, and the load's fluctuation gets spread out. At eighty thousand chunks the readings stop moving: 2,500 additions per chunk, and the cost of claiming a task is still just a rounding error.

Twenty million chunks flips the cost onto the table. Each chunk holds only 10 additions, yet the `fetch_add` for claiming work does not shrink one step; 8 threads fight over the same atomic index again and again, and the work won by fighting is not enough to pay for the fight — the reading climbs from 18 milliseconds to 258, fourteen times slower. Both ends of granularity have their price: too coarse, and someone idles when the load is uneven; too fine, and coordination itself becomes the main act. We saw the lock version of the same story in Section 2.3 on lock granularity, and the criterion is the same: watch at which scale the coordination cost crosses the work cost. The crossing point differs from job to job. Swap the summation for the contended workload in the perf stat section — 8 threads fighting over one atomic variable — and the crossing point moves at once; run it yourself and you can watch the numbers flip.

## Thread Count: Where to Stop Adding

Thread count is the last variable. Still two hundred million additions, the chunk count following the thread count, one chunk per thread per tier, three rounds each:

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

One thing disclosed up front: the readings in the granularity section and the ones above were taken in two separate sessions, and the same 8 threads running two hundred million additions do not line up between the two groups — load drifts, so compare orders of magnitude and do not quibble over single digits. Folded into speedups, with one serial round around 103 milliseconds: 2 threads 1.8x, 4 threads 3.0x, 8 threads 4.2x, 16 threads 5.2x. Every doubling, the payoff shrinks. Past 8 threads there is still another fifth. My machine's 16 logical cores are 8 physical cores plus hyperthreading; with a pure arithmetic job in hand, the ALU (arithmetic logic unit, the part of the CPU that does additions and subtractions) has slack, so two threads squeezing onto one core actually fill the gaps — that is a candidate explanation; I did not run a control experiment with hyperthreading disabled, and the reading itself says only one thing: on my machine, going to 16 did not get slower.

Change the job, and the verdict flips. In the contended experiment the next section uses, 8 threads fight over one atomic variable; the more threads, the fiercer the contention, the speedup drops below 1x, the payoff is paid out entirely, and the contended version trails the private-slot version by 108 times. So when you judge thread count, you must measure it together with the workload — a recommendation spoken in a vacuum means nothing.

## perf stat: What to Look for in Hardware Counters (Extension)

This section carries the (Extension) marker; skipping it does not affect the unit's main line. To understand the reasons behind fast and slow, we keep walking.

A benchmark answers how fast; hardware counters answer why. Inside the CPU sits a set of counters counting cycles, instructions, and cache misses; Linux's `perf` tool reads them, and `perf stat` runs the program once and reports the counts along the way. The perf on my machine is 7.3.1, the hardware counters are available, and the readings below are measured for real. Whether perf can read hardware counters under WSL2 depends on the kernel configuration; if your environment cannot read them, perf marks the counters as unsupported — just follow the reasoning in this section.

We prepare two comparison programs for perf, both with 8 threads each doing 6.25 million increments, fifty million in total. One has everyone writing their own: each thread's accumulated result goes into a 64-byte slot it owns exclusively — 64 bytes is the width of a common cache line, and `alignas(64)` separates neighboring threads' counters so no two of them squeeze onto the same cache line (what happens when they squeeze together belongs to Section 6.5, [How to Adjust the Design](05-adjusting-design), which covers false sharing specifically). The other has all threads fighting over the same `std::atomic<long long>` with relaxed `fetch_add`; the reasoning about memory orders was covered in Section 5.4, [The Four Tiers of Memory Ordering: From Sequential Consistency to relaxed](../chapter05-atomic-memory-model/04-choosing-memory-orders). In form, I merged the two programs into one executable switched by a command-line argument; the source is below:

<OnlineCompilerDemo
  title="Two comparisons for perf stat: private writes vs. fighting over one atomic"
  source-path="code/examples/vol5/chapter06/15_exp_f_counter.cpp"
  description="8 threads each increment 6.25 million times: no argument (or p) takes the exclusive 64-byte slot path, in milliseconds; s takes the fight-over-one-atomic path, two orders of magnitude slower. The perf stat counter readings are in the body table."
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

Compile the snippet together with the harness header into `exp_f_counter` (`g++ -std=c++20 -O2 -pthread exp_f_counter.cpp -o exp_f_counter`, with main and the thread-gathering part completed per the snippet); the command-line argument p takes the private-writes path, s the fight-over-shared path. We run it, and the readings are:

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

The `:u` after the counter names is perf's event modifier, meaning only events in user mode are counted. Put the two readings together and do the math:

| Metric | Private writes | Fighting over one atomic | Difference |
|---|---|---|---|
| Wall-clock time | 4.5 ms | 486 ms | 108x |
| Instructions | 303 million | 357 million | 18% more |
| Cycles | 67 million | 19.71 billion | 295x |
| IPC | 4.5 | 0.018 | 250x |
| Cache miss rate | 23.3% | 70.9% | 47.6 points higher |

Read the readings together and the story is complete. Instructions are only 18% more, which says the two sides do roughly the same work. Cycles differ by 295 times — all the extra time went into waiting. IPC is instructions per cycle: 4.5 says the private-writes version's pipeline runs at full load, while 0.018 says the contended version spins idle nearly every cycle, 8 cores queuing to wait for the same cache line to be handed over from another core. The cache miss rate jumps from 23.3% to 70.9%; what keeps getting invalidated is precisely the cache line of the shared counter everyone is incrementing. The private-writes version's miss-rate base is actually tiny — barely over a hundred thousand cache references, with twenty-some thousand misses, no burden at all. The contended version is different: 37 million cache references, misses climbing to 26 million; of the three hundred fifty-some million instructions, the bulk of memory access is spent fighting over the same cache line. The gap between 108x on wall clock and 295x on cycles comes from parallelism: in the contended version all 8 threads are spinning, burning 3.79 seconds of CPU time, while the private-writes version burns only 16 milliseconds.

There are two counters we did not use, but their readings are worth knowing how to read. Context switches and CPU migrations are zero on both sides: the threads all spin in user mode, never block, and the scheduler has no reason to move them. If those two counters run high in your readings, the meaning changes: threads are blocking frequently (waiting on locks, waiting on I/O), or the thread count far exceeds the core count and threads are being swapped in and out. Pinning threads to cores can reduce migrations — `taskset -c 0-7 ./your_program` is a one-command affair; this article never needed core pinning, so I do not demonstrate it.

perf and the benchmark framework can also work together. Newer Google Benchmark provides the `--benchmark_perf_counters` argument (Linux, depending on libpfm, a library for reading hardware counters), merging the counters directly into the benchmark report — run the benchmark once and you get time and counts together. The more general approach is still the wrapping shown above: `perf stat` wraps the whole benchmark from outside.

## Your Turn

The harness and the full set of experiments are at hand; feed your own work in, and a few steps suffice:

1. Write the most expensive stretch of your program as a function, give it a serial counterpart, and use the same compile options on both sides.
2. Measure at least three rounds with `measure_ms` or the installed Google Benchmark, record all the raw readings, and write the machine, core count, and date into the record as well.
3. Move the three variables one at a time: data size, chunk count, thread count — change only one each time, set your expectation before the change, and read the readings against the expectation after.
4. When readings misbehave (slower than serial, rising with thread count, or jumping more than thirty percent between rounds), pull out cycles and cache misses with perf stat and see where the time went.

When expectation and readings disagree, do not rush to change the design — that gap is exactly the seam between your mental picture of the program and the machine's actual behavior. Section 6.5, [How to Adjust the Design](05-adjusting-design), covers how to tune the design along that seam; the entrance is in the next section.

## Next Step

Before turning the page, swap this article's summation job for your own most expensive function, run a serial baseline, and see which of the three variables moves the payoff the most.

## References

While writing this article, the approach drew on the measurement parts of two books: Williams' C++ Concurrency in Action, and Paul E. McKenney's perfbook (full title in the link below) on the philosophy of measurement and validation. The examples and the words are my own, and the readings were measured on the machine described in the text. Google Benchmark usage was checked against the v1.10.0 user guide. Standard behavior was checked against cppreference.

- [Google Benchmark user guide][gb-guide] — the sources for DoNotOptimize, multithreaded benchmarks, and warm-up options; v1.10.0, verified 2026-11-17.
- [Google Benchmark releases page][gb-releases] — where the v1.10.0 version number used by FetchContent comes from; verified 2026-11-17.
- [Is Parallel Programming Hard, And, If So, What Can You Do About It? (perfbook)][perfbook] — Paul E. McKenney's free online book, kernel.org mirror.
- [std::chrono::steady_clock (cppreference)][cppref-steady] — the monotonic clock since C++11; use it for measuring intervals.
- [std::latch (cppreference)][cppref-latch] — the one-shot gate since C++20; the concurrent experiment code uses it to line up the starting line.
- [std::atomic (cppreference)][cppref-atomic] — the source for `fetch_add` and the memory-order parameter.
- [perf stat tutorial (perf wiki)][perf-wiki] — the official tutorial on hardware counters.

[gb-guide]: https://github.com/google/benchmark/blob/main/docs/user_guide.md
[gb-releases]: https://github.com/google/benchmark/releases
[perfbook]: https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook-e1.html
[cppref-steady]: https://en.cppreference.com/w/cpp/chrono/steady_clock
[cppref-latch]: https://en.cppreference.com/w/cpp/thread/latch
[cppref-atomic]: https://en.cppreference.com/w/cpp/atomic/atomic
[perf-wiki]: https://perf.wiki.kernel.org/index.php/Tutorial#Counting_with_perf_stat
