---
chapter: 1
cpp_standard:
- 14
- 17
description: From the "tricks" of ch01-01 to the countermeasures — use Google Benchmark
  to write a microbenchmark that doesn't (quite) lie to you, take apart the semantics
  and pitfalls of DoNotOptimize / ClobberMemory, plus parameter sweeps, repetition
  aggregation, and UseRealTime, with a minimal runnable example and real output.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- Why microbenchmarks lie
reading_time_minutes: 8
related:
- 'Measurement pitfalls and environment readiness: a 16-item checklist'
- 'Statistics and reporting: turning a distribution into a conclusion'
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
  translated_at: '2026-09-26T05:37:23+00:00'
  engine: anthropic
  token_count: 4400
---
# How to write a credible microbenchmark

## The problem the previous article left behind

ch01-01 laid out all three of the microbenchmark's routines: the compiler optimizes you into nothing, the cache is deceptively warm, noise drowns the signal. The tricks are on the table; this article hands out the antidote.

The antidote really only treats the first trick (the result getting optimized away); along the way it also gets a few habits you'll need immediately — parameter sweeps, repetition and aggregation, wall-clock timing — done right. The third trick (system noise) needs the environment checklist from ch01-03, and how a distribution becomes a conclusion is ch01-04's business; those two wait their turn. This article first nails down one thing: whether the thing you're measuring is the real thing.

## Don't write your own timing loop

You're probably tempted to do it this way: a `for` loop, `std::chrono::steady_clock` as the stopwatch, divide at the end. That's exactly how the `vector_vs_set` in ch00-01 was written — but that one **deliberately used the most bare-bones style to make a point**; don't learn from it. With a hand-rolled timing loop, "how many rounds should I run", "how do I compute the statistics", "how do I keep the result from being optimized away" are all on you, and each of those can bite you. A competent benchmark framework takes those three mechanical chores off your hands so you only write "what to measure". This volume's workhorse is Google Benchmark (GBench from here on).

Here's a minimal but complete example, measuring `std::vector::push_back`:

```cpp
// push_bench.cpp — minimal complete GBench example
#include <benchmark/benchmark.h>
#include <vector>

static void BM_PushBack(benchmark::State& state) {
    for (auto _ : state) {                       // timing loop: the framework controls the iteration count
        std::vector<int> v;
        for (int i = 0; i < state.range(0); ++i) {
            v.push_back(i);
            benchmark::DoNotOptimize(v.data());  // anti-DCE + memory barrier
        }
        benchmark::ClobberMemory();              // make sure writes really land in memory
    }
    state.SetComplexityN(state.range(0));        // tell the framework the big-O N; it auto-fits
}

BENCHMARK(BM_PushBack)
    ->RangeMultiplier(2)->Range(8, 8 << 6)       // parameter sweep: 8,16,32,...,512
    ->UseRealTime()                              // report wall time, not CPU time
    ->Repetitions(3)                             // run 3 rounds
    ->ReportAggregatesOnly(true);                // report only mean/median/stddev/cv

BENCHMARK_MAIN();
```

I actually ran it on my own machine (GCC 16.1.1, GBench v1.9.5, pulled via FetchContent); the output looks like this (a few representative lines):

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

How to read this table. `Time` is wall time (because we used `UseRealTime`), `CPU` is CPU time, and `Iterations` on an aggregate row shows the number of repetitions (3, the 3 from `Repetitions(3)`), not the actual iteration count of each round; each round ran however many iterations the framework estimated — they're just hidden by the aggregation under `ReportAggregatesOnly`. The `mean` / `median` / `stddev` / `cv` rows are statistics over those 3 rounds, and `cv` (coefficient of variation, `stddev/mean`) is the one to watch: it tells you how scattered this group of measurements is. The 44 ns row has a cv of 0.31% — very stable. The day cv spikes above 5%, don't trust that round; go hunt down the noise source first (ch01-03).

> When I first used GBench, I stared only at `mean`; it took a few bruises before I learned to glance at `cv` first. A `mean` with a big `cv` means nothing — drawing conclusions from a distribution where the noise outweighs the signal is pure self-deception.

Time grows with N (8→44 ns, 64→105 ns, 256→242 ns) — that's what `push_back` "getting more expensive with scale" actually looks like. Not the hollow shell from ch01-01, the one DCE shaved down to a single `ret`.

## DoNotOptimize: it saves you, but not all the way

This section is the one most worth thrashing out in the whole article, and it's the place beginners most often misuse. Put ch01-01's `foo()` next to this article's `BM_PushBack`: both "create/write things inside a loop", but `foo()` used no `DoNotOptimize`, so the compiler deleted the whole thing down to a single `ret`; `BM_PushBack` used it, actually ran, and its time scales with N. What `DoNotOptimize` does is pin the "result" to memory or a register so the compiler cannot rule it dead code.

But there's a big caveat. Let me quote the Google Benchmark `user_guide` directly: `benchmark::DoNotOptimize(expr)` stores the result of `expr` to memory or a register, and on GNU compilers it also acts as a read/write barrier over global memory (flushing pending writes); **but it does not prevent `expr` itself from being optimized**: if the result of `expr` can be computed at compile time, it may be folded away entirely, leaving only a constant.

Sounds contradictory, but it's a division of labor. `DoNotOptimize` protects against "the whole loop being deleted because nobody consumes the result" (the `foo()` case); it does **not** protect against "the loop body being folded through by constant propagation". So when writing a benchmark, the input data must be **produced at runtime** — from a random generator, from a file, from parameters — never a compile-time constant. Otherwise the compiler computes its way straight through, and `DoNotOptimize` can't save you. Bakhvalov stresses the same sentence in §2.6: **first make sure the scenario you want to measure actually executes at runtime.** (Which loops back to the tip from the previous article — do yourself a favor and look at the assembly.)

`benchmark::ClobberMemory()` is the companion piece: it forces all pending writes to actually land back in memory. `push_back` mutates the `vector`'s internal state (size, possibly a reallocation), and if the compiler decides "nobody looks at this `vector` afterwards", under certain boundary conditions it may skip part of the writes. `ClobberMemory` is the catch-all "don't skip, really write". A common safe pattern: inside the hot loop, after each write of the target data, pin the address with `DoNotOptimize`; when the loop ends, `ClobberMemory` as the safety net.

## Don't measure just one N

The line `BENCHMARK(BM_PushBack)->RangeMultiplier(2)->Range(8, 8 << 6)` makes the framework run the same benchmark automatically over the whole set of N values `8, 16, 32, 64, 128, 256, 512`. Why sweep an entire set of N instead of picking one convenient value and being done?

The true shape of a complexity curve only shows up when you sweep a set of N. `push_back` is amortized $O(1)$, but sweep it and you'll see the cache eating the cost at small N and reallocation spikes firing at large N; measure a single N and what you see might be the cache dividend or the reallocation penalty — pure luck of the draw. Crossover hides in scale, which is even nastier: in the ch00-01 `vector` vs. `set` comparison, if you only look at N=1024, `set` actually comes out slightly faster; only when you sweep to N=65536 do you see `vector` beating it 5-fold. Without sweeping the scale, that kind of flip is invisible.

And with `state.SetComplexityN(state.range(0))` thrown in, the framework will even auto-fit a big-O from the times you swept, adding a `Big O` section to the output so you can cross-check your complexity intuition. Much less work than computing slopes by hand.

## Run repetitions and report the median, not a lone mean

ch01-01 established that performance is a distribution, so a single measurement means nothing. GBench's answer is `Repetitions(n)`: run the same benchmark n rounds (the framework estimates each round's internal iteration count itself), then `ReportAggregatesOnly(true)` prints only the `mean` / `median` / `stddev` / `cv` aggregates instead of flooding the screen with each round's raw values.

Why the emphasis on the **median** rather than the mean alone: `push_back` occasionally runs into a reallocation; that's a legitimate amortized cost, but relative to the rest it's an outlier — the long tail drags the mean up while the median doesn't budge. ch01-04 devotes proper attention to when to use the median, when the mean, and how to report confidence intervals; for now remember one sentence: reporting the median + cv is far more honest than tossing out a lone mean. `ReportAggregatesOnly(true)` has a hidden bonus too: when benchmarks run in CI, aggregate output is better suited to trend comparison and regression detection (ch01-05 picks up that thread).

One more detail worth a word: `UseRealTime()`. GBench reports **CPU time** by default, and in multithreaded scenarios that counts the work running on other cores too — often not the "how long did this code take on the wall" you wanted. `UseRealTime()` switches the report to wall time. This is the same lineage as the `clock()` trap from ch00-02: `clock()` measures CPU time and distorts under multithreading, `steady_clock` measures wall time. For single-threaded measurement it doesn't matter; the moment your benchmark spawns threads (or you want to benchmark the latency a user actually feels), add `UseRealTime()`.

## How to build it

Two routes; pick one.

**GBench installed system-wide** (on Arch, `pacman -S benchmark`; on macOS, `brew install google-benchmark`):

```bash
g++ -O2 -std=c++17 push_bench.cpp -o push_bench -lbenchmark -lpthread
./push_bench
```

Watch what you link: `benchmark` (the library) or `benchmark::benchmark_main` (which provides its own `main`). With `BENCHMARK_MAIN()` written in your code, link `benchmark`; if you'd rather not write your own `main`, link `benchmark_main` and delete the `BENCHMARK_MAIN()` line.

**CMake + FetchContent** (the route this volume's code examples take — no preinstall needed; clone the repo and it runs):

```cmake
cmake_minimum_required(VERSION 3.20)
project(vol6_ch01_bench CXX)
set(CMAKE_CXX_STANDARD 17)
include(FetchContent)
FetchContent_Declare(benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.5)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)   # turn off benchmark's own test targets
FetchContent_MakeAvailable(benchmark)
add_executable(push_bench push_bench.cpp)
target_link_libraries(push_bench PRIVATE benchmark::benchmark_main)
target_compile_options(push_bench PRIVATE -O2 -Wall -Wextra)
```

> ⚠️ **A pit I stepped in personally**: the flag that turns off benchmark's own test targets is `BENCHMARK_ENABLE_TESTING` (not `BENCHMARK_ENABLE_TESTS`). Get the name wrong and FetchContent goes off to build benchmark's internal tests, which blow up when the gtest setup is missing — and even though your `push_bench` itself already compiled, `cmake --build` returns non-zero overall because a sibling target failed. Look for `Built target push_bench` in the `make` output: if it's there, your executable made it, and you can run `./build/push_bench` directly.

## References

- Google Benchmark: [user_guide](https://github.com/google/benchmark/blob/main/docs/user_guide.md) (the `DoNotOptimize` / `ClobberMemory` / `Range` / `UseRealTime` / `Repetitions` sections; for the precise semantics of `DoNotOptimize`, the original text there is authoritative)
- Bakhvalov, D., *Performance Analysis and Tuning on Modern CPUs*, §2.6 *Microbenchmarks* (the DCE'd `foo()` example; making sure the scenario executes at runtime)
- This volume's ch01-01, "Why microbenchmarks lie" (the three tricks; this article is the counter)
