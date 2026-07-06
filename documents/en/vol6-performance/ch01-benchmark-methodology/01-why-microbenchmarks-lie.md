---
chapter: 1
cpp_standard:
- 14
- 17
description: The microbenchmark is the most-used tool in performance work, and also the easiest one to lie to you. This article tears open three classic deceptions (compiler optimizes to nothing, cache is always hot, noise drowns the signal), names the uncomfortable truth that 'the very hand that makes a microbenchmark clean is the one that makes it unrealistic', and picks Google Benchmark + nanobench.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- Performance Mindset: efficiency is not performance
- From correctness to performance (why sanitizers are the foundation of the performance volume)
reading_time_minutes: 9
related:
- How to write a credible microbenchmark
- Measurement pitfalls and environment readiness
- Statistics and reporting
tags:
- host
- cpp-modern
- intermediate
- 优化
- 测试
title: Why microbenchmarks lie
translation:
  source: documents/vol6-performance/ch01-benchmark-methodology/01-why-microbenchmarks-lie.md
  source_hash: c9682c759425df5dc236c7f2429c56cb45a43d644dfebcf577b7503a27bc7ff2
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3300
---
# Why microbenchmarks lie

## Performance is not a boolean

A feature either works or it doesn't; it either runs or it doesn't — that's a boolean. Performance isn't. Performance is a **distribution**: the same code, the same input, run twice, you get two different numbers; run it ten times and you can plot a scatter chart. Bakhvalov makes this point right at the start of Chapter 2 of *Performance Analysis and Tuning on Modern CPUs*: unzipping a file gives you a byte-identical result every time (reproducible); but ask for "the identical performance curve" and you can't get it.

This single fact drives the methodology of the entire volume. Since performance is a random variable, "measuring performance" is never "run it once, write down the number" — it's sampling a distribution and doing statistical inference. What we need is a statistic on average.

This chapter is about how to do that hard thing right. It's the anchor chapter of the volume; every later performance article opens by referencing the discipline it teaches, the way vol5 runs TSan through all of concurrency correctness.

Before that, though, you have to swallow one uncomfortable fact: the tool you reach for most readily, the microbenchmark, is also the one most likely to lie to you. This article tears its tricks open.

## Episode 1: the compiler optimizes your benchmark into nothing, or into code you didn't intend

The most classic, and most embarrassing, kind. You write a loop that looks like it's doing work — say you want to measure how fast the standard library string constructs and destructs, so you can compare against your own string:

```cpp
// This "tests string creation performance" —— and measures nothing
void foo() {
    for (int i = 0; i < 1000; ++i) {
        std::string s("hi");   // created, but never read
    }
}
```

`s` is created and never used. The compiler takes one look, calls it dead code, and deletes it — the whole loop plus the `string` construction get eliminated (DCE). I confirmed this on my own machine (GCC 16.1.1): at `-O2` the assembly of `foo` is a single `ret`, the entire loop has evaporated; the same code at `-O0` is 89 lines of assembly, loop and `string` construction all present. You smugly finish the run, jot down "0.3 nanoseconds", and conclude the function is fast. What you measured is "do nothing".

> One reminder from me: when you profile, besides admiring your own perf graph, please go look at the assembly. Assembly is a direct map of the machine code; reading it tells you roughly what the machine is actually going to execute.

We brushed past this pitfall in ch00-02 when talking about UB (the `(x+1)>x` example that got folded into a constant). Here's the more direct, performance-flavored version: **as long as the result your benchmark computes is never consumed, the compiler has every legal right to delete the whole thing.** This isn't a compiler "bug"; it's an allowed optimization.

What do you do? Force the result to be "used". The industry calls these `DoNotOptimize`-style helpers — a bit of inline assembly pins the result to memory or a register, and both Google Benchmark and JMH (Java's `Blackhole.consume`) ship one. Its semantics and pitfalls are non-trivial (the `volatile global_sink` in ch00-01 is a hand-rolled approximation); the next article, ch01-02, takes it apart properly.

## Episode 2: the cache is always hot, real workloads aren't

The standard microbenchmark recipe is to run one function thousands or tens of thousands of times and take the mean. The problem lives in "thousands of times": when the same function keeps running on the same (or similar) data, that data sits in L1/L2 cache the whole time and never misses once. The 2 nanoseconds you measure is the 2 nanoseconds under this "hot cache" condition.

In a real workload, between two calls to this function the system runs a pile of other things, and the cache has long since been replaced by someone else's data. When the function gets called again, it has to fetch from L3 or even DRAM — 2 nanoseconds becomes 50, becomes 200. That's the famous order-of-magnitude gulf between micro and macro (the 2/50/200 here is an industry rule of thumb, not something this article measured; it's meant to give you the intuition).

Bakhvalov points out an even more insidious version at the end of Chapter 2: a microbenchmark running on an idle machine **grabs all the DRAM and cache for itself**. So when you compare two implementations, A is faster but eats more memory, B is slightly slower but saves memory. On the idle-system microbenchmark, A wins handsomely, because it can afford to eat memory. But the moment it goes to production, packed in with a bunch of neighbor processes fighting for DRAM, A's extra memory gets squeezed out to disk swap and performance falls off a cliff — the conclusion flips entirely. **What makes A look faster is exactly the microbenchmark's unrealistic premise that "nobody in the whole system is competing with me".**

This corollary matters a lot; it's the mirror of ch00-01's "efficiency ≠ performance" at the measurement layer: don't back production performance with microbenchmark conclusions. What micro measures is "how fast can this function run under ideal conditions", not "how fast the user will actually experience it".

## Episode 3: system noise drowns the signal

Even if you survived the first two episodes (the result is consumed, you've accepted the cache being hot), there's a third class of deception coming from the system itself: modern CPUs and OSes have a pile of "for performance" features whose side effect is to make measurements unstable.

- **Dynamic frequency scaling (DFS / Turbo)**: the CPU temporarily raises or lowers its frequency based on temperature and load. A "cold" processor on the first run might spike to turbo frequency; by the second run it's warmed up and dropped back to base — the same code run twice differs by a few percent to over ten percent. This is especially bad on laptops (cooling-limited).
- **Filesystem cache**: the first run reads from disk; the second time the data is all in the cache, and the second run is much faster. You think the second run shows your optimization paying off; really, the disk just didn't have to be read again.
- **Memory layout bias**: the spookiest kind. The classic Mytkowicz et al. 2009 paper proves that **the total byte count of UNIX environment variables, and the order of object files the linker reads**, both change a program's performance, in an unpredictable direction. You changed no code, only `LINK_ORDER`, and the numbers moved.
- **Even the monitoring tool itself**: you run `top` on another core to watch CPU usage, that core wakes up and re-scales frequency, and that can perturb the core running the benchmark. Bakhvalov specifically warns that even opening Task Manager can affect the measurement.

These three tricks together point to a single conclusion: **one-off, hand-rolled measurement is essentially meaningless.** The number you measured is "the number for this code, under this compile flag, on this machine, at this temperature, this frequency, this memory layout, this cache state" — change one condition and it shifts.

Having covered the three tricks, it's worth stepping back to look at a deeper contradiction. To make a microbenchmark produce clean, stable, comparable numbers, our instinct is to **eliminate noise**: lock the CPU frequency, pin to a core, disable hyperthreading, warm up the cache, run enough rounds and take the median. None of this is wrong, and the rest of this volume will teach you each one.

But you have to stay clear-headed: **the process of eliminating noise is the process of pushing the measurement away from the real environment.** You locked the frequency, but the user's phone never locks; you pinned one core, but production threads get scheduled on and off; you pre-warmed the cache to optimal, but real calls hit a cache cold as ice. So Bakhvalov's advice is key: **when evaluating real performance, don't eliminate the system's nondeterminism — replicate the target environment.** Put another way, the very hand that makes a micro clean is the hand that makes it unrealistic.

Someone's banging the table: that's a contradiction! No, it isn't — these are two different measurement scenarios, used separately.

On one side, **microbenchmarks** do **relative comparison**: the same function, two implementations, the same machine, the same set of control conditions, how much faster is A than B. Here you do want to eliminate noise, because what you want is a "clean signal-to-noise ratio". Its output is "is this change direction right". **Production measurement / macro benchmarks**, on the other side, do **absolute judgement**: how fast the user actually feels, whether it can survive next month's traffic. Here you want to keep the noise and replicate reality, and then **use statistics** to process that noise. Its output is "can this number hold up".

A common and disastrous mistake is to take a micro-style relative conclusion and project it onto a macro-style absolute judgement: "my function got 30% faster in micro, so the service will be 30% faster in production". Probably not — part of that 30% is "the dividend the idle system yielded", which doesn't exist in production at all. These two kinds of measurement are two languages and can't be directly converted. Production measurement and CI regression are the subject of ch01-05.

## Don't hand-roll, use a framework

Once you understand why it lies, tool selection gets clear: do not hand-roll a loop with `std::chrono` and start measuring (the `vector_vs_set` in ch00-01 deliberately used the most naive style to make a point — that one's the exception). A competent benchmark framework handles "result gets optimized away", "how many rounds to run", "how to compute statistics" for you, so you can focus on "what to measure". A few mainstream options in the C++ ecosystem:

| Framework | Form | Anti-optimization mechanism | Statistical output | Positioning |
|---|---|---|---|---|
| **Google Benchmark** | static lib | `DoNotOptimize` + `ClobberMemory` | mean / median / stdev, strongest chainable API | **main workhorse this volume** |
| **ankerl::nanobench** | single header | `doNotOptimizeAway` | ns/op + err%, **built-in IPC / branch miss%** | lightweight supplement, instant feedback when teaching microarchitecture |
| Catch2 `BENCHMARK` | built into Catch2 | return value as sink | mean + 95% CI | convenient if the project already uses Catch2 |
| picobench / nonius / Hayai | single header | varies | simple | not the default, mentioned for completeness |

This volume's pick is **Google Benchmark as the main workhorse + nanobench as a lightweight supplement**. The reason is GBench's chainable API — `BENCHMARK(f)->RangeMultiplier(2)->Range(8, 8<<10)->UseRealTime()->Repetitions(3)->ReportAggregatesOnly(true)` expresses "parameter sweep + wall-clock timing + multiple repetitions + report aggregates only" in one line, which no other library can do; and nanobench ships hardware counters (IPC, branch miss%), giving you instant feedback in the microarchitecture chapters. From the next article on, our code examples switch to GBench.

## References

- Bakhvalov, D. *Performance Analysis and Tuning on Modern CPUs*, Chapter 2 *Measuring Performance* (noise sources, micro vs production, the DCE string example).
- Google Benchmark: [user_guide](https://github.com/google/benchmark/blob/main/docs/user_guide.md) (`DoNotOptimize` / `ClobberMemory` / `Range` / `UseRealTime` / `Repetitions`).
- easyperf.net: *How to get consistent results when benchmarking on Linux*.
- Mytkowicz et al., *Producing Wrong Data Without Doing Anything Obviously Wrong*, ASPLOS 2009 (measurement bias: environment-variable size, link order).
- ankerl::nanobench: [README](https://github.com/martinus/nanobench).
