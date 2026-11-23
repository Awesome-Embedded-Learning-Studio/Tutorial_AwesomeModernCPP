---
title: "Before Measuring, Pin the Conditions Down: The Serial Baseline and the Traps of Measurement"
description: "The serial baseline and measurement conditions, plus three common benchmark traps: the compiler deleting your benchmark, measuring only the synchronization, and warm-up with tail error."
chapter: 5
order: 3
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
  source: documents/vol5-concurrency/chapter06-validating-designs/03-measuring-benefits.md
  source_hash: 5a50cb033d88e85c08bf29cdfbd6291eeef7581d7ad3df99464f55822089aeb3
  translated_at: '2026-11-22T13:39:30+00:00'
  engine: glm-5.3
  token_count: 10129
---

# Before Measuring, Pin the Conditions Down: The Serial Baseline and the Traps of Measurement

Once the concurrent version in your hands has passed the checks, another question arrives right away: how much faster is it than the serial version, really? The payoff of a concurrency retrofit is often waved through with a "feels faster", and then overturned by one honest measurement. This article trades the feeling for readings: before touching the design, we measure a serial baseline, pin the measurement conditions down, and walk through the traps of measurement one by one. How to run benchmarks and how to measure the payoff is the next article's job — Benchmark Tools and Three Variables takes over there.

In this tutorial, the word benchmark is reserved for programs that measure the performance of the program itself: fixed input, fixed steps, measuring only time and counts, carrying no business logic. We write one so that the concurrent version and the serial version can be ranked on the same work.

Every multithreaded reading is the reading of a single run; when we run several rounds, we list all the raw readings from all rounds, without cherry-picking the best-looking one.

## Why You Need a Serial Baseline

We need a serial baseline in hand before the concurrent version's readings have anything to be compared against. The principle sounds obvious, yet in practice it gets skipped all the time: the concurrent version is finished, runs once, three seconds, and the author is satisfied. Is three seconds fast or slow? Without a serial version, the question has no answer.

We demonstrate with the same summation job. The program accumulates `i % 7` for every integer from 0 to n minus 1; the serial version computes it in one pass, and the concurrent version slices the range into chunks while 8 threads claim work dynamically through an atomic index — whoever is free takes the next chunk. Both sides do the same number of additions, and the code recurs in the articles that follow. The `do_not_optimize` in the code is a function that keeps the compiler from deleting the result; what exactly it defends against is disclosed in the scaffolding section of Benchmark Tools and Three Variables.

<OnlineCompilerDemo
  title="Serial baseline vs. 8 threads claiming work dynamically: the small-data first lesson"
  source-path="code/examples/vol5/chapter06/12_sum_range.cpp"
  description="n is one million; the same summation job for three rounds: serial sits around half a millisecond, the 8-thread first round is often slower than serial (entering and exiting costs more than the job), and only the last two rounds come back to around 2x — read the speedup's order of magnitude, not its trailing digits."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// sum_range.cpp —— 同一份求和活：串行基线对 8 线程动态领活
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

// 对应 benchmark::DoNotOptimize 的写法：非 const 引用，读写约束
template <class T>
inline void do_not_optimize(T& value) {
  asm volatile("" : "+m,r"(value) : : "memory");
}

long long sum_range(long long from, long long to) {
  long long s = 0;
  for (long long i = from; i < to; ++i) s += i % 7;
  do_not_optimize(s);
  return s;
}

constexpr int kThreads = 8;
constexpr int kChunks = 8;

// 并发版：区间切成若干块，8 条线程用原子下标动态领活，谁空谁领
double run_parallel8(long long n) {
  using clock = std::chrono::steady_clock;
  std::atomic<int> next{0};
  long long slots[kThreads] = {};
  std::vector<std::thread> pool;
  auto t0 = clock::now();   // 计时含线程进出场，正文小数据读数就是这么量的
  for (int t = 0; t < kThreads; ++t) {
    pool.emplace_back([&, t] {
      long long s = 0;
      for (;;) {
        int c = next.fetch_add(1, std::memory_order_relaxed);
        if (c >= kChunks) break;
        long long from = c * n / kChunks;
        long long to = (c + 1) * n / kChunks;
        for (long long i = from; i < to; ++i) s += i % 7;
      }
      slots[t] = s;
    });
  }
  for (auto& t : pool) t.join();
  auto t1 = clock::now();
  long long total = 0;
  for (long long v : slots) total += v;
  do_not_optimize(total);
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

double run_serial(long long n) {
  using clock = std::chrono::steady_clock;
  auto t0 = clock::now();
  long long s = sum_range(0, n);
  auto t1 = clock::now();
  do_not_optimize(s);
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
  const long long n = 1000000;
  for (int r = 0; r < 3; ++r) {
    double s = run_serial(n);
    double p = run_parallel8(n);
    std::printf("n=%lld round=%d serial=%.2fms parallel8=%.2fms speedup=%.2fx\n",
                n, r, s, p, s / p);
  }
  return 0;
}
```

</OnlineCompilerDemo>

The machine is the same one as in Unit 2 and Unit 5: an AMD Ryzen 7 9700X, 8 cores and 16 threads.

The small-data readings teach us the first lesson. With n equal to one million, serial at half a millisecond, and 8 threads entering and exiting, the three rounds line up:

```text
n=1000000 round=0 serial=0.54ms parallel8=0.60ms speedup=0.91x
n=1000000 round=1 serial=0.50ms parallel8=0.25ms speedup=2.02x
n=1000000 round=2 serial=0.50ms parallel8=0.23ms speedup=2.17x
```

Look at the first round's speedup: 0.91x — the concurrent version is slower than the serial one. The last two rounds jump back to around 2x. For a half-millisecond job, spawning the threads, opening the gate, and summing up the results — every one of these steps costs more than the job itself. At this moment you have exactly one honest reading: no visible benefit in order of magnitude, with trailing digits fighting between 2x and 0.9x — and trailing digits are not something you advertise.

Let's scale the data up to one hundred million and see whether the payoff stands:

```text
n=100000000 round=0 serial=51.68ms parallel8=11.60ms speedup=4.46x
n=100000000 round=1 serial=51.22ms parallel8=12.80ms speedup=4.00x
n=100000000 round=2 serial=51.55ms parallel8=11.88ms speedup=4.34x
```

Serial holds steady at 51 to 52 milliseconds, the concurrent version lands at 11 to 13, and a speedup around 4x holds across all three rounds. It did not reach 8x; several candidate explanations exist: with all 8 threads running, each core's frequency and resources get shared out, the atomic operations used for claiming work fight over the same cache line (the smallest unit a cache moves), and WSL2's scheduling adds its own hand too. I did not verify these one by one; what can be verified is only the reading itself: around 4x, consistent across three rounds. An extension section of [When Is Concurrency Worth Introducing](../chapter01-start-concurrency/08-when-concurrency) covered Amdahl's law — the ceiling on speedup is decided by the serial fraction. The summation job in this article is almost entirely parallel section, yet the 4x reading is still far from the ceiling; where the gap lies, the perf stat section of Benchmark Tools and Three Variables keeps chasing along the thread of cache-line contention.

From these rounds of readings, we lay the habit out in three steps:

1. Keep the serial version and bring it into the benchmark alongside the concurrent one; every claimed benefit speaks against it.
2. Write the measurement conditions down as a record: machine, core count, compile options, data size, round count, date — each missing item costs the reading a notch of credibility.
3. Trust only orders of magnitude and factors in readings, not trailing digits. To make a call, run more rounds and list all the raw readings.

## How to Pin Down the Measurement Conditions

The condition easiest to fiddle with is the compile options. This article uses `-O2` throughout. We already saw this while counting counters in Section 2.2, [How Do We Get the Lost Numbers Back? — std::mutex and the Three Scoped Locks](../chapter02-sharing-data/02-mutex-and-locks): once the optimization level changes, the readings differ on the compile options — the two sides being compared must be in the same tier, and not one set of options may differ.

Timing uses `std::chrono::steady_clock`. Available since C++11, cppreference positions it as a monotonic clock: time points only move forward, tick intervals are constant, and it has nothing to do with the clock on the wall — best suited for measuring intervals. An administrator changing the time, or network time sync, cannot touch it. Whenever we measure how long a program runs, we use it.

Machine and load also go into the record. All my readings come from the 8-core, 16-thread WSL2 described above, with an editor and a browser still open in the background. When you rerun, your readings will certainly differ; by how much is hard to say, because the machine differs and the load differs. With the machine untouched, I started three processes in a row to time the same summation; the first-round readings were 50.0, 51.9, and 51.8 milliseconds — under four percent between processes. So a single reading is not treated as the true value; the rounds of readings we lay out as they came.

One condition cannot be fully pinned down: CPU frequency. I did not disable frequency scaling when setting up the machine. Google Benchmark's documentation warns you specifically: with frequency scaling on, readings from wall-time measurements will jitter. Disabling it means changing system settings, and each platform does it differently; I left it alone and relied on running more rounds to spread the jitter out. If you can lock the frequency, readings will be steadier once locked.

## The Compiler Deleted Your Benchmark

Sit down to write a benchmark, and the first opponent is the compiler. By the standard, its task is to emulate the observable behavior of the abstract machine — Section 5.3, [How One Thread Observes Another Thread's Modifications](../chapter05-atomic-memory-model/03-happens-before), already showed us how ruthless it is when we discussed the as-if rule. In a single-threaded program, it is entitled to delete intermediate computations that nobody observes, entirely. A benchmark is precisely nothing but unobserved intermediate computation: the result comes out, gets thrown away, done.

Let's get it to really delete one for us. Two functions with identical loops, differing only in how the result is handled:

<OnlineCompilerDemo
  title="The compiler deletes an unwatched benchmark down to 0.000 milliseconds"
  source-path="code/examples/vol5/chapter06/13_sum_ignored_kept.cpp"
  description="Two functions with identical loops: the one whose return value nobody looks at gets deleted wholesale at -O2, 0.000ms in all three rounds. The one whose result gets a glance from do_not_optimize runs as usual, around 50ms."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// sum_ignored_kept.cpp —— 编译器把没人看的基准删了个干净
#include <chrono>
#include <cstdio>

// 对应 benchmark::DoNotOptimize 的写法：非 const 引用，读写约束
template <class T>
inline void do_not_optimize(T& value) {
  asm volatile("" : "+m,r"(value) : : "memory");
}

static long long sum_ignored(long long n) {
  long long s = 0;
  for (long long i = 0; i < n; ++i) s += i % 7;
  return s;                          // 调用方不要返回值
}

static long long sum_kept(long long n) {
  long long s = 0;
  for (long long i = 0; i < n; ++i) s += i % 7;
  do_not_optimize(s);                // 结果交给防优化函数看一眼
  return s;
}

int main() {
  const long long n = 100000000LL;
  using clock = std::chrono::steady_clock;
  for (int r = 0; r < 3; ++r) {
    auto a0 = clock::now();
    sum_ignored(n);                  // 返回值看都不看
    auto a1 = clock::now();
    sum_kept(n);
    auto a2 = clock::now();
    std::printf("round=%d ignored=%.3fms kept=%.3fms\n", r,
                std::chrono::duration<double, std::milli>(a1 - a0).count(),
                std::chrono::duration<double, std::milli>(a2 - a1).count());
  }
  return 0;
}
```

</OnlineCompilerDemo>

At the call site we do not so much as glance at `sum_ignored`'s return value. A loop of one hundred million iterations, three rounds of readings:

```text
round=0 ignored=0.000ms kept=51.9ms
round=1 ignored=0.000ms kept=50.9ms
round=2 ignored=0.000ms kept=51.5ms
```

Look at the readings: `ignored` comes in at 0.000 milliseconds — the compiler deleted the entire loop, clean. The evidence is in the assembly: in the file generated by `-S`, the mangled name of `sum_ignored` (the internal name the compiler encodes from the function name and the parameter types) appears zero times; even the function label is gone, leaving only an empty slot at the call site. `kept`'s three rounds still sit around 51 milliseconds, because the anti-optimization function kept the result under watch.

The principle of anti-optimization hides inside `do_not_optimize`'s implementation. It is a function from our own scaffolding: the implementation is a single empty assembly statement with constraints, tricking the compiler into believing the value might be read by the outside world, or written by it, so leave it alone. Google Benchmark's same-named tool `benchmark::DoNotOptimize` does the same job, and its documentation flags a sneakier scenario: when an expression can be computed at compile time, `DoNotOptimize(foo(0))` degrades into `DoNotOptimize(42)` — the computation is gone all the same. That is why the v1.10.0 user guide suggests storing the result in a local variable and handing the variable to it. That is exactly how we wrote it above.

Beside it sits `benchmark::ClobberMemory()`, which tells the compiler that memory may have been rewritten by the outside world, blocking optimizations in the read/write caching direction. The documentation notes it is available only for GNU and MSVC-family compilers. It is cruder than `DoNotOptimize`: abuse it, and values that should have stayed in registers get herded into memory, making the readings uglier instead — use it only where it is truly needed.

## You Measured the Synchronization, Not the Work

Another failure easy to step into is mistaking synchronization overhead for the work itself: you measure for a good long while, and everything you measured was waiting and entering, while the logic you meant to measure never showed up at all. Thread creation and destruction is the most common contaminant. Let's price it once: build 200 threads in a loop and join them one by one, versus doing the same work with no threads at all, run side by side:

```text
round=0 with_spawn=16.046ms (80.2 us per thread) plain=0.0001ms
round=1 with_spawn=16.650ms (83.2 us per thread) plain=0.0001ms
round=2 with_spawn=16.229ms (81.1 us per thread) plain=0.0001ms
```

On my machine, one thread from creation to collection costs around 80 microseconds. If the work under test is shorter than 80 microseconds, the star of the reading is thread entry and exit overhead, with no relation at all to the logic you meant to measure. So draw the timing boundary clearly: start timing only after the threads are built and placed, and stop the watch only after the work is done and the results are collected. Google Benchmark's multithreaded benchmarks draw the boundary inside the framework — threads are managed by the framework outside the timed loop. Our own concurrent experiment code uses a gate, arranging the same setup: worker threads queue at the gate (C++20's `std::latch`, the one-shot countdown covered in Section 3.7, [Choosing Tools by the Collaboration You Need](../chapter03-wait-and-coordinate/07-choosing-tools)), and the gate opens only after the main thread presses the stopwatch.

Lock overhead is another object easy to misread. Section 2.3 measured the difference between one lock and four: readings of 216 milliseconds versus 152 milliseconds — what was measured there was contention, not locking itself. Without contention, one lock-and-unlock is on the order of tens of nanoseconds, comparable to an atomic operation. Measure a lock on single-threaded code and you get the latter. Four threads fighting over one lock is what gets you the former. Whichever kind a reading speaks of, the report must say clearly which one it is.

## Warm-Up and Tail Error

Run the same program several rounds in a row, and the readings find their steady state on their own. Put the one-hundred-million-iteration summation into one process and run six rounds:

```text
round=0 50.6ms
round=1 51.0ms
round=2 50.2ms
round=3 49.9ms
round=4 50.5ms
round=5 50.3ms
```

Six rounds squeeze into 49.9 to 51.0 — a pure arithmetic loop offers no surprises: the data lives in the cache, and branch prediction long since learned the path by heart. But the parallel first round is not so polite; look back at the small-data readings at the start: with n equal to one million, the first round took 0.60 milliseconds, slower than serial, while the last two rounds needed barely over 0.2. In that first round, the eight threads had just been brought up, the scheduler had not yet settled them onto their cores, and the caches were cold. First-round readings should be marked separately; do not let them slip into the verdict.

Tail error hides inside the average. Google Benchmark decides the iteration count dynamically: a few trial runs to estimate the time, then it decides how many iterations to run — the official documentation states at least one iteration and no more than one billion, stopping when CPU time exceeds a floor or wall time reaches five times the floor, with the floor defaulting to 0.5 seconds. The reading you see is total time divided by total iterations — an average into which the opening trial runs and the ragged ending are all spread. The shorter the benchmark, the bigger the share of startup overhead in that average, the same principle as the half-millisecond summation losing to thread entry and exit above. We write our scaffolding dumber: it measures exactly one round per call, the caller arranges multiple rounds, every round's reading is handed over as-is, and averaging is left to the person reading them.

Warm-up is off by default in Google Benchmark; the v1.10.0 documentation states the warm-up duration defaults to zero, and getting a steady state requires setting `--benchmark_min_warmup_time` explicitly or calling `MinWarmUpTime` in code. Want to measure cold start? Then flip it around: no warm-up at all, and measure the first execution on its own. Want the steady state? Set the warm-up duration explicitly — and do not mix the two readings into the same table.

## References

While writing this article, the approach drew on the measurement parts of two books: Williams' C++ Concurrency in Action, and Paul E. McKenney's perfbook (full title in the link below) on the philosophy of measurement and validation. The examples and the words are my own, and the readings were measured on the machine described in the text. Google Benchmark usage was checked against the v1.10.0 user guide. Standard behavior was checked against cppreference and the draft clauses on eel.is.

- [Google Benchmark user guide][gb-guide] — the sources for DoNotOptimize's expression degradation, ClobberMemory's boundaries, dynamic iteration counts, and warm-up options; v1.10.0, verified 2026-11-17.
- [std::chrono::steady_clock (cppreference)][cppref-steady] — the monotonic clock since C++11; use it for measuring intervals.
- [std::latch (cppreference)][cppref-latch] — the one-shot gate since C++20; the concurrent experiment code uses it to line up the starting line.
- [C++ working draft: the as-if rule (intro.abstract)][draft-abstract] — the boundary of compiler rewriting, covered in Section 5.3; this article's deleted-benchmark evidence lands on it.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams] — the measurement-related chapters.
- [Is Parallel Programming Hard, And, If So, What Can You Do About It? (perfbook)][perfbook] — Paul E. McKenney's free online book, kernel.org mirror.

[gb-guide]: https://github.com/google/benchmark/blob/main/docs/user_guide.md
[cppref-steady]: https://en.cppreference.com/w/cpp/chrono/steady_clock
[cppref-latch]: https://en.cppreference.com/w/cpp/thread/latch
[draft-abstract]: https://eel.is/c++draft/intro.abstract
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
[perfbook]: https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook-e1.html
