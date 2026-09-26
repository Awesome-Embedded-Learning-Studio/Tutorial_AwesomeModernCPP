---
chapter: 1
cpp_standard:
- 14
- 17
description: The microbenchmark is the most-used tool in performance work and also the easiest one to fool you. This article tears open three classic deceptions (the compiler optimizing everything away, a deceptively hot cache, noise drowning the signal), names the uncomfortable truth that the very hand that makes a microbenchmark clean is the one that makes it unrealistic, and settles on Google Benchmark + nanobench.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Performance Mindset: efficiency is not performance'
- 'From "correct first" to "then fast": why sanitizers are the foundation of the performance volume'
reading_time_minutes: 9
related:
- How to write a credible microbenchmark
- 'Measurement pitfalls and environment readiness: a 16-item checklist'
- 'Statistics and reporting: turning a distribution into a conclusion'
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
  translated_at: '2026-09-26T05:37:32+00:00'
  engine: anthropic
  token_count: 2400
---
# Why microbenchmarks lie

## Performance is not a boolean

A feature either works or it doesn't, a program either runs or it doesn't — that is a boolean. Performance isn't. Performance is a **distribution**: the same code, the same input, run it twice and the numbers already differ; run it ten times and you can plot a scatter chart. Bakhvalov makes this point right at the top of Chapter 2 of *Performance Analysis and Tuning on Modern CPUs*: unzip a zip file and you get a byte-identical result every time (reproducible); but ask for "the identical performance curve reproduced", and it can't be done.

That single fact sets the direction of this entire volume's methodology. Since performance is a random variable, "measuring performance" is not "run it once and write down a number" — it is sampling a distribution and making a statistical inference. What we want is a statistic in the average sense.

This chapter is about how to do that hard thing right; it is the anchor chapter of the whole volume. Every later performance article opens by citing back to the discipline taught here, the way vol5 runs TSan through all of concurrency correctness.

But before that, you have to face one uncomfortable fact: the tool that sits most readily in your hand, the microbenchmark, is precisely the one most likely to lie to you. This article takes its tricks apart.

## Episode 1: the compiler optimizes your benchmark into nothing, or into code you never intended

The most classic kind, and the most embarrassing. You write a loop that looks like it's doing work — say you want to measure how fast the standard library string constructs and destructs, so you can compare it against your own string:

```cpp
// This "tests string creation performance" — and actually measures nothing
void foo() {
    for (int i = 0; i < 1000; ++i) {
        std::string s("hi");   // created, but never read
    }
}
```

`s` gets created and nobody ever uses it. The compiler takes one look, calls it dead code, and deletes it: the entire loop plus the `string` construction are eliminated (DCE). I confirmed this by actually running it on my machine (GCC 16.1.1): at `-O2`, the assembly of `foo` is a single `ret` — the whole loop has evaporated; the same code at `-O0` is 89 lines of assembly, with the loop and the `string` construction all present. You finish the run feeling pleased, jot down "0.3 nanoseconds", and conclude this function is really fast. What you measured is "do nothing".

> A reminder from me here: when you profile, beyond the moment of admiring your own perf graph, please do go look at the assembly. Assembly is a direct map of the machine code; reading it tells you roughly what your machine will actually execute.

We brushed against this pitfall from the side in ch00-02 when talking about UB (the `(x+1)>x` example that got folded into a constant); here is a more direct, performance-flavored version: **as long as nobody consumes the result your benchmark computes, the compiler has every legal right to delete the whole thing.** This is not a compiler "bug"; it is an allowed optimization.

What do you do? Force the result to be "used". The industry answer is a family of helpers like `DoNotOptimize`, which under the hood use a bit of inline assembly to pin the result to memory or a register; both Google Benchmark and JMH (Java's `Blackhole.consume`) ship one. It comes with real semantics and real pitfalls (the `volatile global_sink` in ch00-01 was a hand-rolled approximation of it), and the next article, ch01-02, takes it apart properly.

## Episode 2: the cache is always hot, real workloads aren't

The standard microbenchmark recipe is to run one function thousands upon thousands of times and take the average. The problem lives in the "repeatedly": when the same function runs over and over on the same (or similar) data, that data lounges in the L1/L2 cache from start to finish and never misses once. The 2 nanoseconds you measured is 2 nanoseconds under this "hot cache" condition.

In a real workload, between two calls to this function the system has run a pile of other things, and the cache has long since been swapped out for somebody else's data. When the function gets called again, it has to fetch fresh from L3 or even DRAM — 2 nanoseconds becomes 50, becomes 200. That is the famous order-of-magnitude gulf between micro and macro (the 2/50/200 here is an industry rule-of-thumb magnitude, not something this article measured; the point is to give you the intuition).

The more insidious version is the one Bakhvalov points out at the end of Chapter 2: a microbenchmark running on an idle machine **claims all the DRAM and cache for itself**. So suppose you compare two implementations: A is faster but hungrier for memory, B is slightly slower but memory-frugal. On the idle-system microbenchmark, A wins handsomely, because it has the capital to eat memory. But the moment it goes to production, crowded by a bunch of neighbor processes fighting over DRAM, A's extra memory gets squeezed out to disk swap, performance falls off a cliff, and the conclusion flips completely. **What makes A look faster is precisely the microbenchmark's unrealistic premise of "nobody else in the whole system is competing with me".**

This corollary matters enormously; it is the mirror at the measurement layer of ch00-01's "efficiency ≠ performance": do not use microbenchmark conclusions to vouch for production performance. What micro measures is "how fast this function can run under ideal conditions", not "how fast the user will actually experience it".

## Episode 3: system noise drowns the signal

Even if you survived the first two episodes (the result is consumed, and you've made peace with the hot cache), there is another class of deception, and it comes from the system itself: modern CPUs and OSes carry a pile of "for performance" features whose side effect is making measurement results unstable.

- **Dynamic frequency scaling (DFS / Turbo)**: the CPU temporarily raises or lowers its frequency based on temperature and load. A "cold" processor on the first round might spike to turbo frequency; by the second round it has warmed up and dropped back to base — the same code run twice can differ by a few percent to over ten percent. This is especially bad on laptops (limited cooling).
- **Filesystem cache**: the first run has to read the disk; by the second run the data is all in the cache, and the second run is much faster. You think your optimization paid off on the second run; in reality the disk just didn't need to be read again.
- **Memory layout bias**: the spookiest one. The classic 2009 paper by Mytkowicz et al. proved that **the total byte count of the UNIX environment variables, and the order of object files fed to the linker**, both change a program's performance, and in an unpredictable direction. You changed no code, only `LINK_ORDER`, and the numbers moved.
- **Even the monitoring tool you use**: you run `top` on another core to watch CPU usage, that core gets activated and re-scaled, and that can perturb the core running the benchmark. Bakhvalov specifically warns that even opening the Task Manager can affect the measurement.

These three tricks together all point to one conclusion: **a one-off, hand-rolled measurement carries almost no meaning.** The number you measured is "the number for this code, under this compiler flag, on this machine, at this temperature, this frequency, this memory layout, and this cache state" — change any one condition and it moves.

With the three tricks covered, it is worth stepping back to look at a deeper contradiction. To make a microbenchmark produce clean, stable, comparable numbers, our instinct is to **eliminate noise**: lock the CPU frequency, pin to a core, disable hyperthreading, warm up the cache, run enough rounds and take the median. None of that is wrong, and the rest of this volume will teach you how to do each one.

But stay clear-headed about one thing: **the process of eliminating noise is the process of pulling the measurement away from the real environment.** You locked the frequency, but the user's phone never locks its frequency; you pinned one core, but in production threads get scheduled on and off constantly; you pre-warmed the cache to its optimum, but real calls hit a cache as cold as ice. That is why Bakhvalov's advice matters so much: **when evaluating real performance, do not eliminate the system's nondeterminism — replicate the target environment.** Put another way, the very hand that makes a micro clean is the hand that makes it unrealistic.

Someone is slamming the table by now: that's a contradiction! It isn't — these are two different measurement scenarios, and they must be used separately.

On one side, the **microbenchmark** does **relative comparison**: the same function, two implementations, the same machine, the same set of controlled conditions — how much faster is A than B. Here you do want to eliminate noise, because what you want is a "clean signal-to-noise ratio". Its output is "is this change direction right". **Production measurement / macro benchmarks**, on the other side, do **absolute judgement**: how fast the user actually feels it, whether it can carry next month's traffic. Here you want the opposite — keep the noise, replicate reality, and then **use statistical methods** to process that noise. Its output is "can this number hold up".

A common and disastrous mistake is taking a relative micro conclusion and projecting it into an absolute macro judgement: "my function got 30% faster in micro, so the service will be 30% faster once it ships". Most likely it won't — part of that 30% is "the dividend the idle system handed over", which simply does not exist in production. These two kinds of measurement are two different languages and cannot be converted directly. Production measurement and CI regression are the dedicated subject of ch01-05.

## Don't hand-roll it, use a framework

Once you understand why it lies, tool selection becomes clear: do not hand-roll a loop around `std::chrono` and call it a benchmark (the `vector_vs_set` in ch00-01 of this volume deliberately used the most naive style to make a point — that one is the exception). A competent benchmark framework should handle the mechanical chores for you — "the result gets optimized away", "how many rounds to run", "how to compute statistics" — so you can focus on writing "what to measure". The mainstream options in the C++ ecosystem:

| Framework | Form | Anti-optimization mechanism | Statistical output | Positioning |
|---|---|---|---|---|
| **Google Benchmark** | static library | `DoNotOptimize` + `ClobberMemory` | mean / median / stdev, strongest chainable API | **main workhorse of this volume** |
| **ankerl::nanobench** | single header | `doNotOptimizeAway` | ns/op + err%, **built-in IPC / branch miss%** | lightweight supplement, instant feedback for the microarchitecture chapters |
| Catch2 `BENCHMARK` | built into Catch2 | return value as sink | mean + 95% CI | handy for projects already on Catch2 |
| picobench / nonius / Hayai | single header | varies | simple | not the default pick, mentioned for completeness |

This volume's pick is **Google Benchmark as the main workhorse + nanobench as a lightweight supplement**. The reason is GBench's chainable API: `BENCHMARK(f)->RangeMultiplier(2)->Range(8, 8<<10)->UseRealTime()->Repetitions(3)->ReportAggregatesOnly(true)` expresses "parameter sweep + wall-clock timing + multiple repetitions + report aggregates only" in a single line, a combination no other library manages; and nanobench ships hardware counters (IPC, branch miss%), which give you instant feedback when we reach the microarchitecture chapters — very convenient. From the next article on, our code examples switch to GBench.

## References

- Bakhvalov, D. *Performance Analysis and Tuning on Modern CPUs*, Chapter 2 *Measuring Performance* (noise sources, micro vs production, the DCE string example)
- Google Benchmark: [user_guide](https://github.com/google/benchmark/blob/main/docs/user_guide.md) (`DoNotOptimize` / `ClobberMemory` / `Range` / `UseRealTime` / `Repetitions`)
- easyperf.net: *How to get consistent results when benchmarking on Linux*
- Mytkowicz et al., *Producing Wrong Data Without Doing Anything Obviously Wrong*, ASPLOS 2009 (measurement bias: environment-variable size, link order)
- ankerl::nanobench: [README](https://github.com/martinus/nanobench)
