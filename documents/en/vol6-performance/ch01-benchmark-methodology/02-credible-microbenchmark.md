---
chapter: 1
cpp_standard:
- 14
- 17
description: From the "tricks" of ch01-01 to the countermeasures — write a not-(so)-deceptive microbenchmark with Google Benchmark, take apart the semantics and pitfalls of DoNotOptimize / ClobberMemory, parameter sweeps, repeat aggregation, UseRealTime, with a minimal runnable example and real output.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- Why microbenchmarks lie
reading_time_minutes: 8
related:
- Measurement pitfalls and environment readiness
- Statistics and reporting
tags:
- host
- cpp-modern
- intermediate
- 优化
- 测试
title: How to write a credible microbenchmark
translation:
  source: documents/vol6-performance/ch01-benchmark-methodology/02-credible-microbenchmark.md
  source_hash: a800c5e069a479290b4dc543fdf43dcdd3603f9e32cc1f270846685a0f7c39fe
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3600
---
# How to write a credible microbenchmark

## The problem the previous article left behind

ch01-01 laid out the microbenchmark's three deceptions: the compiler optimizes you into nothing, the cache is fake-hot, noise drowns the signal. The tricks are done; this article is the antidote.

The antidote really only covers the first deception (the result gets optimized away), and along the way gets parameter sweeps, repeat aggregation, and wall-clock timing right — the three pieces of posture you'll need immediately. The third (system noise) needs the environment checklist from ch01-03, and how a distribution becomes a conclusion is the business of ch01-04; those two wait. This article first nails down "is the thing you're measuring the real thing".

## Don't write your own timing loop

You're probably tempted to do this: a `for` loop, `std::chrono::steady_clock` for timing, divide at the end. The `vector_vs_set` in ch00-01 was written exactly that way, but it **deliberately used the most naive style to make a point** — don't copy it. With a hand-rolled timing loop, "how many rounds to run", "how to compute statistics", "how to keep the result from being optimized away" are all on you, and each of those has pitfalls. A competent benchmark framework takes those three mechanical chores off your hands, and you only write "what to measure". This volume's main workhorse is Google Benchmark (GBench from here on).

Here's a minimal but complete example, measuring `std::vector::push_back`:

```cpp
// push_bench.cpp —— minimal complete GBench example
#include <benchmark/benchmark.h>
#include <vector>

static void BM_PushBack(benchmark::State& state) {
    for (auto _ : state) {                       // timing loop: framework controls iteration count
        std::vector<int> v;
        for (int i = 0; i < state.range(0); ++i) {
            v.push_back(i);
            benchmark::DoNotOptimize(v.data());  // prevent DCE + memory barrier
        }
        benchmark::ClobberMemory();              // make sure writes really hit memory
    }
    state.SetComplexityN(state.range(0));        // tell the framework the big-O N, auto-fit
}

BENCHMARK(BM_PushBack)
    ->RangeMultiplier(2)->Range(8, 8 << 6)       // parameter sweep: 8,16,32,...,512
    ->UseRealTime()                              // report wall-clock, not CPU time
    ->Repetitions(3)                             // run 3 rounds
    ->ReportAggregatesOnly(true);                // only report mean/median/stddev/cv

BENCHMARK_MAIN();
```

I ran it on my own machine (GCC 16.1.1, GBench v1.9.5, pulled via FetchContent); the output looks like this (a few representative rows):

```text
Run on (14 X 3193.92 MHz CPU s)
CPU Caches:
  L1 Data 32 kiB (x7)  L2 Unified 512 kiB (x7)  L3 Unified 16384 kiB (x1)
-------------------------------------------------------------------------------------
Benchmark                                           Time             CPU   Iterations
-------------------------------------------------------------------------------------
BM_PushBack/8/repeats:3/real_time_mean           44.0 ns         44.0 ns            3
BM_PushBack/8/repeats:3/real_time_median         44.0 ns         44.0 ns            3
BM_PushBack/8/repeats:3/real_time_stddev        0.137 ns        0.137 ns            3
BM_PushBack/8/repeats:3/real_time_cv             0.31 %          0.31 %             3
BM_PushBack/64/repeats:3/real_time_mean           105 ns          105 ns            3
BM_PushBack/64/repeats:3/real_time_median         105 ns          105 ns            3
BM_PushBack/256/repeats:3/real_time_mean          242 ns          242 ns            3
BM_PushBack/256/repeats:3/real_time_median        242 ns          242 ns            3
```

How to read this table. `Time` is wall-clock (because we used `UseRealTime`), `CPU` is CPU time, and in the aggregate rows `Iterations` shows the repetition count (3, the one from `Repetitions(3)`), not the real per-round iteration count; the framework estimated many iterations per round, they're just hidden under `ReportAggregatesOnly` mode. `mean` / `median` / `stddev` / `cv` are statistics over those 3 rounds, and `cv` (coefficient of variation, `stddev/mean`) is the one to watch — it tells you "how scattered this group of measurements is". The cv on the 44ns row is 0.31%, very stable; the day cv spikes past 5%, don't trust this round, go hunt down the noise source first (ch01-03).

> The first time I used GBench I just stared at `mean`. It took a few losses before I learned to glance at `cv` first. A `mean` with a big `cv` is meaningless; drawing a conclusion from a distribution where noise is bigger than the signal is just fooling yourself.

Time scales with N (8→44ns, 64→105ns, 256→242ns) — this is what `push_back`'s "gets more expensive with scale" actually looks like. Not the empty shell that DCE deleted down to a single `ret` in ch01-01.

## DoNotOptimize: it saves you, but not all the way

This section is the one that most deserves to be thorough, and it's the one beginners most misuse. Put ch01-01's `foo()` next to this `BM_PushBack`: both "create/write things in a loop". `foo()` doesn't use `DoNotOptimize`, and the compiler deletes the whole thing into a single `ret`; `BM_PushBack` does, it actually runs, and time scales with N. What `DoNotOptimize` does is pin the "result" to memory or a register so the compiler can't decide it's dead code.

But there's a big catch. I'll quote the Google Benchmark `user_guide` directly: `benchmark::DoNotOptimize(expr)` stores the result of `expr` in memory or a register, and on GNU compilers it's also a global memory read/write barrier (flushing pending writes); **but it does not prevent `expr` itself from being optimized** — if `expr`'s result can be computed at compile time, it may get computed away entirely, leaving only a constant.

Sounds contradictory, but it's really a division of labor. `DoNotOptimize` prevents "the whole loop getting deleted because no one uses the result" (the `foo()` case); it does **not** prevent "the loop body getting punched through by constant propagation". So when writing a benchmark, the input data must be **produced at runtime** — from random numbers, from a file, from a parameter; it can't be a compile-time constant. Otherwise the compiler computes all the way through, and `DoNotOptimize` can't save you. Bakhvalov stresses this in §2.6 too: **first make sure "the scenario you want to measure" actually executes at runtime.** (That loops back to my reminder in the previous section — go look at the assembly.)

`benchmark::ClobberMemory()` is the companion piece, forcing all pending writes to actually land in memory. `push_back` mutates the `vector`'s internal state (size, possibly a reallocation); if the compiler decides "no one looks at this `vector` later", under some boundary conditions it may skip part of the writes. `ClobberMemory` is the finisher that says "don't skip, really write". A common safe pattern: in the hot loop, `DoNotOptimize` the address every time you write the target data; at the end of the loop, `ClobberMemory` as the safety net.

## Don't measure just one N

The line `BENCHMARK(BM_PushBack)->RangeMultiplier(2)->Range(8, 8 << 6)` makes the framework run the same benchmark with the set `8, 16, 32, 64, 128, 256, 512` for N. Why sweep a whole set of N instead of picking one handy value?

The true shape of complexity only shows up when you sweep a set of N. `push_back` is amortized $O(1)$, but a sweep reveals that small N gets eaten by the cache while large N triggers reallocation spikes; if you measure only one N, what you see might be a cache dividend or a reallocation penalty, depending entirely on luck. Worse, crossovers hide inside the scale: in the ch00-01 `vector` vs `set`, looking only at N=1024, `set` is actually slightly faster; sweeping up to N=65536 is where you see `vector` beat it 5×. Without sweeping the scale, you simply can't see these flips.

Throw in `state.SetComplexityN(state.range(0))` and the framework will also auto-fit a big-O from the times you swept, adding a `Big O` column to the output so you can sanity-check against your complexity intuition. Easier than computing the slope by hand.

## Repeat several rounds, report the median, not the single-pass mean

ch01-01 said performance is a distribution, so a single measurement is meaningless. GBench's answer is `Repetitions(n)`: run the same benchmark n rounds (the framework estimates the inner iteration count each round), then `ReportAggregatesOnly(true)` outputs only the `mean` / `median` / `stddev` / `cv` aggregates, instead of flooding the screen with each round's raw value.

Why stress the **median** and not just the mean: `push_back` occasionally hits a reallocation — that's a legitimate amortized cost, but relative to the mean it's an outlier; the mean gets dragged up by that long tail, while the median doesn't budge. ch01-04 covers when to use the median, when the mean, and how to report a confidence interval; for now remember one line: reporting the median + cv is far more honest than throwing out a single mean. `ReportAggregatesOnly(true)` has an invisible bonus too: when running benchmarks in CI, aggregate output is more suitable for trend comparison and regression detection (ch01-05 picks up that thread).

One more detail to mention: `UseRealTime()`. GBench reports **CPU time** by default, which under multithreading also counts work done on other cores, and is often not "how long did this code take on the wall" that you want. `UseRealTime()` switches the report to wall-clock. This thread is continuous with the `clock()` pitfall in ch00-02: `clock()` measures CPU time and distorts under multithreading; `steady_clock` measures wall-clock. Single-threaded doesn't matter; the moment your benchmark spawns threads (or you want to compare against the latency the user feels), add `UseRealTime()`.

## How to compile

Two paths, pick one.

**System has GBench installed** (Arch: `pacman -S benchmark`, macOS: `brew install google-benchmark`):

```bash
g++ -O2 -std=c++17 push_bench.cpp -o push_bench -lbenchmark -lpthread
./push_bench
```

Note whether you link `benchmark` (the library) or `benchmark::benchmark_main` (with its own `main`): if the code has `BENCHMARK_MAIN()`, link `benchmark`; if you don't want to write `main` yourself, link `benchmark_main` and delete the `BENCHMARK_MAIN()` line.

**With CMake + FetchContent** (this is what this volume's code examples use, so readers don't have to pre-install; clone the repo and run):

```cmake
cmake_minimum_required(VERSION 3.20)
project(vol6_ch01_bench CXX)
set(CMAKE_CXX_STANDARD 17)
include(FetchContent)
FetchContent_Declare(benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.5)
set(BENCHMARK_ENABLE_TESTING OFF CACHE_BOOL "" FORCE)   # turn off its own test targets
FetchContent_MakeAvailable(benchmark)
add_executable(push_bench push_bench.cpp)
target_link_libraries(push_bench PRIVATE benchmark::benchmark_main)
target_compile_options(push_bench PRIVATE -O2 -Wall -Wextra)
```

> ⚠️ **A pit I stepped in**: to turn off benchmark's own test targets, the flag is `BENCHMARK_ENABLE_TESTING` (not `BENCHMARK_ENABLE_TESTS`). Get the name wrong and FetchContent will go build benchmark's internal tests, blow up on missing gtest config, and even though your `push_bench` itself already compiled, `cmake --build` will return non-zero overall because a sibling target failed. Look for `Built target push_bench` in the `make` output; if it's there, your executable made it, just run `./build/push_bench`.

## References

- Google Benchmark: [user_guide](https://github.com/google/benchmark/blob/main/docs/user_guide.md) (the `DoNotOptimize` / `ClobberMemory` / `Range` / `UseRealTime` / `Repetitions` sections; the precise semantics of `DoNotOptimize` are authoritative from the original text here).
- Bakhvalov, D. *Performance Analysis and Tuning on Modern CPUs* §2.6 *Microbenchmarks* (the `foo()` DCE example, making sure the scenario actually executes at runtime).
- This volume's ch01-01 "Why microbenchmarks lie" (the three tricks; this article is the countermeasure).
