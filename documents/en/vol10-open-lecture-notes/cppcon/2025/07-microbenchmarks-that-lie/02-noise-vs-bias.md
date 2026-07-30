---
title: "Noise You Can Suppress — Bias Is the Nightmare"
description: "CppCon 2025 notes — peeling the second layer of microbenchmark liars: noise and bias. Locally measured a right-skewed distribution from 200 samples, and expose the trap that high_resolution_clock on libstdc++ is actually system_clock."
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
  - "The Compiler Lied to You: The Number-One Microbenchmark Lie"
  - "The Branch Predictor Is Helping You Cheat"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/07-microbenchmarks-that-lie/02-noise-vs-bias.md
  source_hash: c4eb6c203da3558e0115b80494607d904c9e26366bd0446552e003b00a105549
  translated_at: '2026-07-29T00:33:58.679864+00:00'
  engine: anthropic
  token_count: 3600
---

# Noise You Can Suppress — Bias Is the Nightmare

[Previous part](01-compiler-ate-your-benchmark.md) we took apart the compiler, that first-layer liar, and learned why loops get deleted and how `DoNotOptimize` nails them back in. But nailing the loop back is only the start — you've kept the loop, and the nanoseconds it prints can still be lying, because the machine running it is itself restless. This part is about two kinds of runtime interference with completely different natures: noise and bias. The first is manageable; the second is the kind that actually keeps you up at night.

## Run It Two Hundred Times — See What the Numbers Look Like

Let's tweak the accumulation loop from last part, run it 200 times in a row, re-timing each iteration, and look at the per-run latency distribution. The loop itself hasn't changed, and neither has the environment (this machine: GCC 16.1.1, WSL2 with no core pinning and no frequency locking):

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

Plot the 200 samples as a histogram (dropping the 5 most extreme values on each end; x-axis is latency in microseconds):

```text
200 samples (after trimming: 198..259 us), distribution shape:
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
mean > median, distribution is right-skewed (long tail on the slow side)
```

Let's stare at this chart for a few seconds. First thing: **152 of the 200 runs cram into the fastest bin** (around 197.8us) — that's "under normal conditions, this loop is just this fast." But a long tail drags out to the right, scattering all the way to 259us, with extremes reaching 313us — roughly 1.6x the fastest value.

Second thing: `mean=204.8` is bigger than `median=199.4`. If the data were a symmetric bell shape, mean and median would be nearly equal. Here the mean gets pulled up by the right-side long tail — the distribution is **right-skewed**.

Third thing, and the most damning: if you ran it just once and got a single point somewhere in this distribution, you'd have no idea whether you got the "typical value" packed in with those 152, or some 250us "unlucky value" from the long tail. A single measurement says nothing.

## Where the Noise Comes From

These jitters are called **noise**, and its defining trait is that it's **random**: the direction is unpredictable, sometimes big sometimes small, and over many runs it cancels out. The sources are everywhere:

- **OS scheduling.** Your process doesn't own the CPU. The kernel can preempt it at any moment to run something else, then switch back. While it's preempted, the timer keeps ticking.
- **Interrupts.** Hardware interrupts, timer interrupts, network-card receive interrupts all break into your loop.
- **CPU frequency scaling.** Modern CPUs scale frequency dynamically based on load. If the frequency drops mid-loop, the same instructions take longer. This WSL2 box has no frequency locking, so the noise is especially visible.
- **Cache state.** Your data is in L1 this time; next time another process has evicted it, and you have to fetch from a slower level.
- **Hyperthread contention.** If the other logical thread on the same physical core gets busy, it competes for execution resources.

Noise is random, so the countermeasure is direct: **run more, take statistics.** Run 30 times and take the median; run 200 times and look at the distribution — the long-tail effect gets pressed down. Computing over those 200 samples:

```text
30 samples (us): min=200.2  median=228.3  mean=242.1  max=458.2  stddev=52.1
max/min = 2.29x   stddev/mean = 21.5%
```

Note that `stddev/mean = 21.5%` (the coefficient of variation). What it says: on this un-tuned machine, the spread of measurements for the same code is roughly a fifth of the mean. If your optimization only buys you 10%, you **can't tell at all** whether it actually got faster or whether the noise just twitched.

The standard moves for suppressing noise, ranked by bang-for-buck:

1. **Run more, take the median — not the mean.** That earlier histogram already showed it: the distribution is skewed and the mean gets dragged by the long tail. The median is more robust to outliers. For more stability, drop the top and bottom few and then average.
2. **Pin to a core.** On Linux, `taskset -c 2 ./bench` pins the process to one core, avoiding the cache rebuild that comes with cross-core migration.
3. **Lock the frequency.** `cpupower frequency-set -g performance` sets the scaling governor to performance mode so the CPU doesn't sneak off into a lower frequency.
4. **Warm up.** Run a few rounds before the real measurement so the branch predictor, caches, and allocator all reach steady state. The first round is always slower.

::: tip How suppressed is "suppressed enough"?
A practical threshold: get the coefficient of variation (stddev/mean) under 1% — only then are the numbers stable enough to compare single-digit-percent differences. If you can't hit that, the environment isn't tuned yet, so hold off on conclusions — the ranking you get will most likely come out different every run. This un-tuned WSL2 box sits at 21.5%, an order of magnitude off — so it's only good for showing "what noise looks like," not for precise comparisons.
:::

## Pick the Wrong Clock, You've Lost From the Start

Before you suppress noise, there's a more fundamental question: which clock are you timing with? Get this wrong and everything after is wasted. Let's do a static check first, and see who several clocks actually are on this machine's libstdc++:

```cpp
#include <chrono>
#include <type_traits>
#include <cstdio>
int main() {
    if constexpr (std::is_same_v<std::chrono::high_resolution_clock,
                                 std::chrono::steady_clock>)
        std::printf("high_resolution_clock == steady_clock (monotonic, safe)\n");
    else if constexpr (std::is_same_v<std::chrono::high_resolution_clock,
                                      std::chrono::system_clock>)
        std::printf("high_resolution_clock == system_clock (wall clock, jumps when NTP adjusts!)\n");
    // ...
}
```

Running this on the local machine gives:

```text
high_resolution_clock == system_clock (wall clock, jumps when NTP adjusts!)
steady_clock is monotonic: yes
steady_clock tick resolution: 1.000 ns (1/1000000000 sec)
```

This is exactly the trap Kris keeps flagging in the talk. `std::chrono::high_resolution_clock` sounds like the most professional name, and it's the one online tutorials love most, but the standard defines it extremely loosely — it's just an alias that can resolve to different things on different implementations. On libstdc++ (what GCC uses by default), it's `system_clock`.

`system_clock` is a wall clock — it reflects the system's civil time. The problem: the system time gets adjusted forward or backward by NTP. If a measurement window happens to catch an NTP sync, the `t1 - t0` you compute could come out negative, or absurdly large. Imagine measuring a function's latency and the printout reads `-342 nanoseconds` — that's the kind of thing that drives you insane.

The right choice is **`std::chrono::steady_clock`**. It's monotonic — a later read is guaranteed to be no smaller than an earlier one — and it's unaffected by system-time adjustments. On this machine its resolution is 1ns, more than enough for the vast majority of microbenchmarks. So remember one rule: **use `steady_clock`, leave `high_resolution_clock` alone.**

If you care enough to want hardware-level cycle counts, then read the TSC (Time Stamp Counter) directly — it's a hardware register that increments once per clock cycle since the CPU powered on. The local machine doesn't have `<x86intrin.h>` installed, but a line of inline assembly reads it fine:

```cpp
static inline std::uint64_t rdtsc() {
    std::uint32_t hi, lo;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
}
```

TSC gives you cycle counts, not nanoseconds, but it comes straight from the hardware: highest precision, and also immune to system-time adjustments. [The next part on latency and throughput](04-latency-throughput-cycles.md) will use it.

## Bias: The One That's Still Wrong After Ten Thousand Runs

No matter how loud the noise, more runs will eventually press it down, because it's random. Bias is different — it's **systematic**, always pulling your results in the same direction. Run it ten thousand times and it's still wrong, just very stably, very "confidently" wrong.

One example that left a deep impression on me. There's a repeatedly cited study in the performance-measurement world about how Linux environment variables affect a process's stack alignment. The experiment found that just a different set of environment variables could produce a 30% to 300% performance difference in the same program. Three hundred percent — same binary, same code, swap the environment, three times slower.

Why? Because the length of the environment variables affects the initial stack alignment at program startup, and that alignment in turn shapes the cache behavior of every memory access after. It's a hidden chain running from "an apparently harmless config tweak" all the way to "instruction-cache hit rate." You sit at the terminal, type `./bench`, and get a number X; you drop the same code into systemd and get a number Y; X and Y can be tens of percent apart. So what exactly is that X measuring? Does it have anything to do with how your code actually behaves in production?

The sources of bias all share one trait: **they're not random, they're fixed by some configuration of the environment.** Common categories:

- **Stack alignment and code layout.** The environment-variable example above is stack alignment. Code layout works the same way: where the linker places functions inside the binary affects instruction-cache hit rate and branch-predictor behavior.
- **CPU initial frequency state.** Under a power-saving policy the CPU starts at a low frequency, so the first few iterations are slow — and that slowness is systematic, not random.
- **Initial state of the branch predictor.** The predictor "remembers" prior branch patterns, so the patterns from the first run affect how the next few runs behave.
- **Whether pages are resident.** The first access to a memory region triggers a page fault; subsequent accesses are fast. That "first time is slow" is systematic too.

::: warning Noise and bias call for completely different handling
Noise is solved by "running more"; bias is solved by "controlling variables" — these are two very different strategies. Seeing measurement jitter and reflexively bumping the iteration count only suppresses noise; it does nothing for bias. For bias you have to find the fixed factor pulling you off (core pinning, alignment, warm-up, initial state) and either lock it down or deliberately perturb it for a controlled comparison. Fail to tell the two apart and you get stuck in the "I ran it a hundred thousand times and it's still off" trap with no way out.
:::

## Don't Be Lazy About Statistics

One last statistical pitfall, related to both noise and bias. A lot of people finish a benchmark, habitually compute a mean and a standard deviation, and call it done. That habit rests on an assumption: the data is normally distributed. But that 200-sample histogram is already sitting up there — real microbenchmark data is **almost never normal.** It's usually skewed, and can even come out bimodal (half the samples hit cache, half don't — two peaks).

Averaging a skewed distribution gives you a middling value dragged around by the long tail, representing neither "the fast case" nor "the slow case." And the standard deviation loses that clean probabilistic meaning it has under a normal distribution.

So build one habit: **with a fresh set of measurements, look at the distribution first, then at the summary statistics.** Draw a histogram (like we did above — ASCII is enough) and see whether it's unimodal or bimodal, symmetric or skewed. If it's skewed, report the median instead of the mean; when comparing which of two approaches is faster, look at the full distribution or the empirical CDF (eCDF), not just the means.

If you absolutely must compare using means, at least bring confidence intervals or error bars. A bar chart with no error bars tells you nothing about how many runs are behind it or how big the variance is — it could be a single run, or a hundred runs hiding a huge variance. Error bars aren't decoration; they're the lifeline.

## The Edge of This Layer

That's it — we've taken apart the runtime's two liars. Noise gets pressed down by running more; bias gets smoked out by controlling variables. But there's a third liar, sneakier than either: your code hasn't changed, the environment is tuned, the statistics are done right, and the numbers are still lying to you — because the branch predictor and the cache hierarchy inside the CPU are putting on a "flawless run" just for you. That's the star of the next part.

[Next part: The branch predictor is helping you cheat →](03-branch-prediction-cheats.md)
