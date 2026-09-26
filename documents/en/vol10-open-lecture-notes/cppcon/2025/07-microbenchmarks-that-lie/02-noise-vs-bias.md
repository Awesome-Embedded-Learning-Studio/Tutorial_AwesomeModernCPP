---
title: "Noise Can Be Suppressed: Bias Is the Real Nightmare"
description: "CppCon 2025 notes — taking apart the second layer of microbenchmark liars: noise and bias. A local measurement of 200 samples shows a right-skewed distribution, and exposes the trap that high_resolution_clock on libstdc++ is actually system_clock"
chapter: 7
order: 2
conference: cppcon
conference_year: 2025
talk_title: 'Why 99% of C++ Microbenchmarks Lie – and How to Write the 1% that Matter!'
speaker: Kris Jusiak
cpp_standard: [20]
difficulty: intermediate
platform: host
reading_time_minutes: 13
tags:
  - cpp-modern
  - host
  - intermediate
  - 优化
related:
  - "The Compiler Lied to You: The #1 Microbenchmark Lie"
  - "The Branch Predictor Is Helping You Cheat"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/07-microbenchmarks-that-lie/02-noise-vs-bias.md
  source_hash: c4eb6c203da3558e0115b80494607d904c9e26366bd0446552e003b00a105549
  translated_at: '2026-09-26T16:34:13+00:00'
  engine: anthropic
  token_count: 2500
---

# Noise Can Be Suppressed: Bias Is the Real Nightmare

In [the previous part](01-compiler-ate-your-benchmark.md) we took apart the compiler, the first layer of liars, and learned why loops get deleted and how to nail them back down with `DoNotOptimize`. But nailing the loop back down is only the start — you've kept the loop, and the nanosecond number it prints can still be lying, because the machine running it is itself restless. This part covers two kinds of runtime interference with completely different natures: noise and bias. The former is easy to deal with; the latter is the kind that genuinely keeps you up at night.

## First, Run It Two Hundred Times and See What the Numbers Look Like

Let's tweak the accumulation loop from the previous part: run it 200 times in a row, re-timing every run, and look at the distribution of per-run latency. The loop itself hasn't changed, and neither has the environment (local machine: GCC 16.1.1, WSL2 with no core pinning and no frequency locking):

```cpp
constexpr int N = 1'000'000, R = 200;
double samples[R];
volatile long sink = 0;
for (int r = 0; r < R; ++r) {
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) sink += i;
    auto t1 = std::chrono::steady_clock::now();
    samples[r] = std::chrono::duration<double, std::micro>(t1 - t0).count();
}
```

Plot the 200 samples as a histogram (dropping the 5 most extreme values at each end; the x-axis is elapsed time in microseconds):

```text
200 samples (198..259 us after removing extremes), distribution shape:
 197.8 | ######################################## (152)
 200.5 |  (1)
 203.2 | # (4)
 205.8 | # (5)
 208.5 | ### (12)
 211.2 | # (5)
 213.9 |
 216.6 |  (1)
 219.3 |  (1)
  ...  |  (a few scattered samples in this range)
 235.3 |  (3)
 251.4 |  (1)
 259.5 | # (5)

min=197.7  median=199.4  mean=204.8  max=313.8 us
mean > median, meaning the distribution is right-skewed (long tail on the slow side)
```

Let's stare at this chart for a few seconds. First thing: **152 of the 200 runs are crammed into the fastest bin** (around 197.8us) — that's "under normal conditions, this loop is simply this fast." But a long tail drags out to the right, scattering all the way to 259us, with the extreme value even reaching 313us — roughly 1.6x the fastest value.

Second thing: `mean=204.8` is larger than `median=199.4`. If the data followed a symmetric bell-shaped distribution, the mean and the median would be nearly equal. Here the mean is dragged up by the long tail on the right, which tells us the distribution is **right-skewed**.

Third thing, and the most lethal one: if you ran it only once, the number you got is some point in this distribution, and you have no way of knowing whether it's the "typical value" packed in with those 152, or some 250us "unlucky value" from the long tail. A single measurement says nothing.

## Where the Noise Comes From

These fluctuations are called **noise**, and their defining trait is being **random**: unpredictable in direction, sometimes big and sometimes small, and over many runs they cancel each other out. The sources are everywhere:

- **Operating-system scheduling.** Your process doesn't get the CPU to itself; the kernel can preempt it at any moment to run some other task, then switch it back. During the time it's switched out, the timer keeps ticking.
- **Interrupts.** Hardware interrupts, timer interrupts, and network-card receive interrupts all break into your loop.
- **CPU frequency scaling.** Modern CPUs adjust their frequency dynamically based on load; if the frequency drops halfway through your loop, the same instructions take longer. This WSL2 machine doesn't lock its frequency, so the noise is especially visible.
- **Cache state.** This time your data is in L1; next time another process has evicted it, and you have to fetch it from a slower level.
- **Hyperthread contention.** If the other logical thread on the same physical core gets busy, it competes for execution resources.

Noise is random, so the countermeasure is just as direct: **run more, take statistics.** Run 30 times and take the median; run 200 times and look at the distribution — the influence of the long tail gets pressed down. Computing over those 200 samples above:

```text
30 samples (us): min=200.2  median=228.3  mean=242.1  max=458.2  stddev=52.1
max/min = 2.29x   stddev/mean = 21.5%
```

Note that `stddev/mean = 21.5%` (the coefficient of variation). What it says: on this untuned machine, the spread of measurements for the same piece of code is roughly one fifth of the mean. If your optimization only buys a 10% improvement, you **simply cannot tell** whether it really got faster or whether the noise just twitched.

The standard moves for suppressing noise, ranked by bang for the buck:

1. **Run more and take the median, not the mean.** That histogram above already made the point: the distribution is skewed, and the mean gets dragged off by the long tail. The median is more resistant to outliers. For extra stability, drop the highest and lowest few samples and then average.
2. **Pin to a core.** On Linux, `taskset -c 2 ./bench` nails the process to a specific core, avoiding the cache rebuild that comes with cross-core migration.
3. **Lock the frequency.** `cpupower frequency-set -g performance` sets the frequency-scaling policy to performance mode so the CPU can't slack off into lower frequencies.
4. **Warm up.** Run a few rounds before the real measurement so the branch predictor, caches, and allocator all settle into steady state. The first round is always on the slow side.

::: tip How Much Noise Suppression Is Enough
A rule-of-thumb threshold: press the coefficient of variation (stddev/mean) below 1% — only then are the numbers stable enough to support comparisons at the few-percentage-point level. If you can't get there, the environment isn't tuned yet, so don't rush to conclusions: the ranking you measure will most likely come out different every run. This untuned WSL2 machine sits at 21.5%, an order of magnitude off — so it's only good for showing what noise looks like, not for precise comparisons.
:::

## Pick the Wrong Clock and You've Lost From the Start

Before you suppress noise, there's a more fundamental question: which clock are you timing with? Get this one wrong and everything downstream is wasted. Let's first do a static check and see who the various clocks on this machine's libstdc++ actually are:

```cpp
#include <chrono>
#include <type_traits>
#include <cstdio>
int main() {
    if constexpr (std::is_same_v<std::chrono::high_resolution_clock,
                                 std::chrono::steady_clock>)
        std::printf("high_resolution_clock == steady_clock (单调, 安全)\n");
    else if constexpr (std::is_same_v<std::chrono::high_resolution_clock,
                                      std::chrono::system_clock>)
        std::printf("high_resolution_clock == system_clock (日历时钟, NTP 调时会跳!)\n");
    // ...
}
```

Running it on the local machine gives:

```text
high_resolution_clock == system_clock (日历时钟, NTP 调时会跳!)
steady_clock 单调: 是
steady_clock tick 分辨率: 1.000 ns (1/1000000000 秒)
```

This is exactly the trap Kris flags over and over in the talk. The name `std::chrono::high_resolution_clock` sounds the most professional, and it's the one online tutorials love best, but the standard defines it extremely loosely — it's merely an alias, and it can point to different things on different implementations. On libstdc++ (the implementation GCC uses by default), it is `system_clock`.

`system_clock` is a calendar clock; it reflects the system's wall time. The problem: the system time gets adjusted forward or backward by the NTP service. If your measurement window happens to catch an NTP sync, the `t1 - t0` you compute can come out negative, or absurdly large. Imagine measuring a function's latency and the printout says `-342 nanoseconds` — that's the kind of thing that can drive a person mad.

The right choice is **`std::chrono::steady_clock`**. It increases monotonically — a later reading is guaranteed never to be smaller than an earlier one — and it's unaffected by system-time adjustments. On this machine its resolution is 1ns, more than enough for the vast majority of microbenchmarks. So remember one rule: **use `steady_clock`, and don't touch `high_resolution_clock`**.

If you care enough to want hardware-level cycle counts, go straight to the TSC (Time Stamp Counter) — a hardware register that increments once per clock cycle since the CPU powered on. `<x86intrin.h>` isn't installed on this machine, but a snippet of inline assembly reads it fine:

```cpp
static inline std::uint64_t rdtsc() {
    std::uint32_t hi, lo;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
}
```

TSC gives you cycle counts, not nanoseconds, but it comes straight from the hardware: the highest precision, and no exposure to system-time adjustments. [The next part, on latency and throughput](04-latency-throughput-cycles.md), will put it to use.

## Bias: The One That's Still Wrong After Ten Thousand Runs

No matter how loud the noise gets, more runs will eventually press it down, because it's random. Bias is different — it's **systematic**, always pulling your results in the same direction. Run it ten thousand times and it's still wrong; it just stays wrong very steadily, very "confidently".

Here's an example that left a deep impression on me. In the performance-measurement field there's a repeatedly cited study on how Linux environment variables affect a process's stack alignment. The experiment found that merely having a different set of environment variables could produce a 30% to 300% performance difference in the very same program. Three hundred percent — same binary, same code, swap the environment variables, and it runs three times slower.

Why? Because the length of the environment variables affects the stack's initial alignment at program startup, and that alignment in turn shapes the cache behavior of every memory access that follows. It's a hidden chain running from "a seemingly harmless bit of environment configuration" all the way to "instruction-cache hit rate". You sit at your terminal, type `./bench`, and get a number X; you drop the same code into systemd and get a number Y; X and Y can be tens of percentage points apart. So what exactly is that X of yours measuring? Does it have anything to do with how your code actually behaves in production?

The sources of bias all share one trait: **they aren't random — they're determined by some fixed configuration of the environment.** The common categories:

- **Stack alignment and code layout.** The environment-variable example above is stack alignment. Code layout works the same way: where the linker places functions within the binary affects instruction-cache hit rates and branch-predictor behavior.
- **The CPU's initial frequency state.** Under a power-saving policy the CPU starts at a low frequency, so the first few iterations run slow — and that slowness is systematic, not random.
- **The branch predictor's initial state.** The predictor "remembers" earlier branch patterns, so the patterns from the first run influence how the following runs behave.
- **Whether pages are resident.** The first access to a region of memory triggers a page fault; subsequent accesses are fast. That "first time is slow" is systematic too.

::: warning Noise and bias demand completely different handling
Noise is solved by "running more"; bias is solved by "controlling variables" — two completely different strategies. Seeing jitter in your measurements and reflexively cranking up the iteration count only suppresses noise; it does nothing for bias. For bias you must find the fixed factor dragging you off (core pinning, alignment, warm-up, initial state) and either nail it down or deliberately perturb it to build a controlled comparison. If you can't tell the two apart, you'll be stuck in the confusion of "I clearly ran it a hundred thousand times and it's still off" with no way out.
:::

## Don't Get Lazy About Statistics

One last statistical pitfall, one that involves both noise and bias. Plenty of people finish a benchmark, habitually compute a mean, glance at the standard deviation, and call it a day. That habit carries a premise: the data is normally distributed. But the 200-sample histogram above is already sitting right there — real microbenchmark data is **almost never normally distributed**. It's usually skewed, and it can even come out bimodal (half the samples hit in cache, half don't — two peaks).

Compute the mean over a skewed distribution and you get a middling value dragged around by the long tail — it represents neither "the fast case" nor "the slow case". The standard deviation, meanwhile, loses the tidy probabilistic meaning it has under a normal distribution.

So build one habit: **with a fresh batch of measurements, look at the distribution first and the summary statistics second.** Draw a histogram (like we did above — ASCII is perfectly adequate) and check whether it's unimodal or bimodal, symmetric or skewed. If it's skewed, report the median rather than the mean; and when comparing which of two approaches is faster, look at the full distribution or the empirical cumulative distribution function (eCDF), not just the means.

If you absolutely must compare means, at least attach confidence intervals or error bars. With a bar chart that has no error bars, you have no idea how many runs are behind it or how large the spread is — it could be a single run, or a hundred runs hiding an enormous variance. Error bars aren't decoration; they're the lifeline.

## The Boundary of This Layer

At this point we've taken apart the runtime's two liars. Noise gets pressed down by running more; bias gets smoked out by controlling variables. But there's one more liar, sneakier than either: your code itself hasn't changed, the environment is tuned, the statistics are done right — and the numbers are still lying to you, because the branch predictor and the cache hierarchy inside the CPU are putting on a "flawless performance" for you together. That's the star of the next part.

[Next part: The Branch Predictor Is Helping You Cheat →](03-branch-prediction-cheats.md)
