---
title: "Latency, Throughput, Cycles: What Are You Actually Measuring"
description: "CppCon 2025 notes — taking apart the fourth layer of microbenchmark liars: conflating latency with throughput. Measured on this machine with rdtsc, the same bit_mix function scores 37 cycles/op under latency measurement and 9.30 cycles/op under a data-dependent throughput measurement"
chapter: 7
order: 4
conference: cppcon
conference_year: 2025
talk_title: 'Why 99% of C++ Microbenchmarks Lie – and How to Write the 1% that Matter!'
speaker: Kris Jusiak
cpp_standard: [20]
difficulty: intermediate
platform: host
reading_time_minutes: 12
tags:
  - cpp-modern
  - host
  - intermediate
  - 优化
related:
  - "The Branch Predictor Is Helping You Cheat"
  - "A Faster Microbenchmark Is Not a Faster Program"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/07-microbenchmarks-that-lie/04-latency-throughput-cycles.md
  source_hash: 10c0f1a03693b6d428c16794b4ad01d1897c6bf9dd9632cea62ecb7e17e1e9f8
  translated_at: '2026-09-26T16:39:56+00:00'
  engine: anthropic
  token_count: 6200
---

# Latency, Throughput, Cycles: What Are You Actually Measuring

In the [first three parts](01-compiler-ate-your-benchmark.md) we took apart the compiler, noise, and the branch predictor — that lineup of liars. Even with all of them handled, the number you measure can still answer a question you never asked — because you may not even realize that you asked the wrong question in the first place. The phrase "how fast is this function" hides two completely different dimensions: latency and throughput. Stirring them together is the most insidious — and most common — conceptual mistake in microbenchmarking.

## First, nail down the two words

**Latency** is about a single operation: how long it takes from going in to coming out. Its units are nanoseconds per call, or cycles per call. It asks "how fast is a single one."

**Throughput** is about a time window: how many operations you can complete in total. Its units are operations per second, or gigabytes per second. It asks "how much work gets done in a second."

They sound like the same thing — if one call takes 2 nanoseconds, a second holds 500 million calls, so isn't throughput just the reciprocal of latency? But modern CPUs flatly refuse to follow that simple reciprocal. The reason is a counterintuitive fact.

## The CPU does not wait for you

We put this realization up front because it is the linchpin of this entire piece: **the CPU executes out of order, it has a deep pipeline, and inside your loop it charges ahead as hard as it can — it does not stop and wait just because one iteration is "logically done."**

Imagine you write a loop where each iteration calls some function `f` and stores the result. From where you sit, this is strictly serial: the 1st `f` finishes, and only then comes the 2nd. But through the CPU's eyes, as long as the result produced by the 1st `f` is not immediately needed by the 2nd, it is entirely free to **start early** on the 2nd, the 3rd, even the 10th, stuffing different parts of multiple iterations into different execution ports to run in parallel. That is instruction-level parallelism.

The direct consequence: **throughput can be far higher than the reciprocal of latency.** A single `f` might have a latency of 5 cycles, but through out-of-order execution and pipelining the CPU can manage one `f` per cycle — a throughput five times the reciprocal. Flip it around: if you only measure latency, you will think this function is "kind of slow"; but its real production performance may be much faster, because production makes continuous calls, and the CPU can parallelize them.

So the structure of your benchmark code has to change with the question you want to ask. Measuring latency calls for one way of writing it; measuring throughput calls for another. Write it wrong, and what you measure is not the quantity you had in mind.

## Same function, two ways to measure, a 4x gap

Let's grab a lightweight bit-mixing function as the target. It is purely a string of bit operations and multiplications with no memory access at all — well suited to keeping the attention on the CPU pipeline:

```cpp
static inline std::uint64_t bit_mix(std::uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}
```

First, the **latency measurement** approach. The idea: call it once at a time, time each call individually, run many iterations and take the median — doing our best to reflect "how long one call itself takes":

```cpp
// Read the TSC (hardware cycle counter) with rdtsc — more precise than chrono
static inline std::uint64_t rdtsc() {
    std::uint32_t hi, lo;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
}

constexpr int M = 20000;
std::uint64_t samples[M];
std::uint64_t x = 42;
for (int i = 0; i < M; ++i) {
    std::uint64_t a = rdtsc();
    std::uint64_t r = bit_mix(x + i);
    std::uint64_t b = rdtsc();
    samples[i] = b - a;
}
// Sort and take the median
```

Now the **throughput measurement** approach. Note how it looks nothing like the latency measurement: instead of timing each call individually, it runs five hundred million iterations back to back in one big loop and reads the TSC only at the start and at the end. The key is that the loop body builds a **data dependency chain** — each iteration's result feeds the next one:

```cpp
constexpr int N = 500'000'000;
std::uint64_t acc = 12345;
auto c0 = rdtsc();
for (int i = 0; i < N; ++i) acc = bit_mix(acc);  // Every iteration depends on the previous result
auto c1 = rdtsc();
double per_op = (double)(c1 - c0) / N;
```

The results on this machine (GCC 16.1.1, `-O2`):

```text
Throughput measurement (with data dependency): 9.30 cycles/op   (sink=7075159340691660003)
Latency measurement   (per-call timing): 37.00 cycles/op (median, including rdtsc overhead)
Minimum observed: 1.00 cycles/op (close to the bare overhead of two rdtsc calls)

Same bit_mix, throughput 9.30 vs latency measurement 37.00 — the gap comes from how it was measured
```

Same function, two ways of measuring, a 4x gap. That 4x is not noise, and it is not the compiler playing tricks — it is purely that the question you asked was different.

## Why the gap is so big, and one counterintuitive conclusion

First, that 9.30. `bit_mix` contains three multiplications (`x *= ...`), each with a latency of about 3 cycles, and the three are serially dependent (each one consumes the previous one's result), forming a critical path. So the minimum number of cycles to "finish one `bit_mix`" is roughly `3 × 3 = 9` cycles. The 9.30 cycles/op from the throughput measurement corresponds exactly to the length of this critical path.

Why can the throughput measurement be this accurate? Because that data dependency chain (`acc = bit_mix(acc)`) leaves the CPU no way to parallelize across iterations: iteration `i+1`'s input is iteration `i`'s output, so the CPU must wait for iteration `i` to finish before it can start iteration `i+1`. The parallelism in the pipeline is choked off by this dependency chain, and every iteration has to walk the critical path honestly, end to end. So what gets measured is **the real one-call latency** — how many cycles a single operation truly costs.

Which leads to a counterintuitive conclusion: **in this data-dependent formulation, the "throughput measurement" is the one that actually measures the real single-call latency.** It is named throughput, but because of the dependency chain, what it reflects is latency.

Then what is 37.00 about? Why does the "per-call timing" latency measurement come out so much higher than the real latency? Because per-call timing has two sources of contamination. First, the `rdtsc` instruction itself is not free: read the TSC twice with one `bit_mix` sandwiched in between, and the `b - a` you record includes at least the overhead of two TSC reads — that "minimum observed 1.00 cycles/op" is the floor you get when nothing at all happens between the two TSC reads, and the real overhead is higher. Second, on a single call the pipeline is cold: the function's instructions are not in the L1 instruction cache yet, and the front end has to fetch and decode them on the spot, which adds a few more beats. Stack those two contaminants together, and a function whose real latency is about 9 cycles gets measured as 37.

So you see, this is not "throughput and latency are inherently 4x apart" — it is "per-call timing is inaccurate in this scenario." If you go on to announce that "the latency of `bit_mix` is 37 cycles," you have been fooled by your own measurement method.

## So how do you measure it right

It depends on what you want to measure.

**To measure latency** (how long a single operation itself takes), the most reliable method is not per-call timing but **a loop measurement with a data dependency**: use a dependency chain like `acc = f(acc)` to force serial execution, run many iterations and take the average, and what you get is the critical-path length — the real latency. That is where the 9.30 earlier came from.

**To measure throughput** (how much work gets done per second), you turn it around: **actively break the dependency chain**, so the CPU can run multiple iterations in parallel. Take each call's input from independent sources (read from a pre-generated array, say), and never let this call's output become the next call's input:

```cpp
// Throughput measurement: independent inputs, the CPU can parallelize across iterations
auto c0 = rdtsc();
for (int i = 0; i < N; ++i) {
    do_not_optimize(bit_mix(inputs[i]));  // inputs[i] are mutually independent
}
auto c1 = rdtsc();
```

Under this formulation, the CPU will happily stuff different iterations' `bit_mix` into different execution ports and run them in parallel, and what gets measured is throughput (calls completed per cycle) — noticeably higher than the reciprocal of latency.

**Avoid one classic mistake**: mixing latency measurement and throughput measurement in a single benchmark. Say you write a loop that neither builds a dependency chain nor deliberately breaks one, with inputs half random and half correlated — the number you measure lands somewhere in between, neither latency nor throughput, an ill-defined hybrid quantity. That is exactly how a lot of people end up with meaningless conclusions like "my function takes about X nanoseconds."

## From wall clock to cycles

All along we have been describing performance in cycles rather than nanoseconds, and that is no accident. Wall-clock time (nanoseconds) mixes in far too much that does not belong to your code: OS scheduling, interrupts, cache state. Cycles are the heartbeat the CPU actually spends executing instructions — a far more faithful scale for "how much work your code actually did."

You read cycles with the TSC (that `rdtsc` inline assembly from earlier). A few points to watch:

- **The TSC is not necessarily perfectly synchronized across cores.** Cross-core migration can skew the reading, so it is best to pin to a core before measuring (see [the second part](02-noise-vs-bias.md)).
- **The TSC's frequency is not necessarily equal to the CPU's rated frequency.** Modern systems have an "invariant TSC" that ticks at a fixed rate, unaffected by CPU frequency scaling — which is actually a good thing, since it means you can get stable cycle counts without locking the frequency. But if you want to convert cycles back into nanoseconds, you need to know the TSC's actual frequency; you cannot just divide by the CPU's rated clock.
- **The TSC gives total cycles, not "the cycles your code used."** If an interrupt or a scheduling event lands inside the measurement window, its cycles are counted all the same. So the TSC beats the wall clock, but not by enough to shield you from noise entirely — you still have to run multiple rounds and take the median.

## Once you have cycles, how to dig deeper

Suppose you have measured accurately, and some code costs about 12 cycles per call. Then what? With that number alone you still do not know whether it is "fast" or "slow," let alone which direction to optimize in. Kris hammers one point throughout the talk: **getting the number is only the beginning — the measurement has value only when you can explain why the number is what it is.**

There are two directions to dig, and given this machine's limits we will only introduce them conceptually here, without hands-on measurements.

The first direction is computing the **IPC** (Instructions Per Cycle). Divide the number of instructions executed by the number of cycles and there it is. High IPC means the CPU is doing work most of the time; low IPC means it is waiting — on memory, on branch resolution, on an execution port. Contemporary x86 has a dispatch width of roughly 4-6, so the theoretical IPC ceiling is likewise 4-6; in practice anything above 2 counts as decent, and below 1 usually means it is stuck on memory. This machine has no hardware-counter reading tool (no perf installed), and precise IPC measurement needs one, so we will just point at it here.

The second direction is **static pipeline analysis**. LLVM ships a tool called MCA (Machine Code Analyzer): hand it a piece of assembly, and based on the CPU's scheduling model it tells you each instruction's latency, which execution port it uses, and how many cycles a full trip around the loop takes. Its value: it predicts performance without actually running the code. This machine does not have `llvm-mca` installed, so there is no live demonstration to show — but the idea is worth knowing. When the cycles you measure line up with what MCA predicts, your measurement process is trustworthy; when they do not, either you measured wrong, or MCA's scheduling model is off for your CPU — and either way, that is valuable information.

::: tip An even more important reminder
Do not fall into the "count assembly instructions" trap. Fewer instructions does not mean faster — an `imul` and an `add` each count as one instruction, but they differ several-fold in cost. What actually decides speed is the dependency relationships between instructions, the execution ports they occupy, and pipeline bubbles. That calls for resource-pressure analysis like MCA's, not counting lines. In the next part, on correlation, we will see a live example: under `-O2` the compiler turns a division into a multiplication, and you would never catch it by counting instructions alone.
:::

## The boundary of this layer

With latency and throughput told apart and the right measurement method in hand, the number you get is finally both meaningful and trustworthy. But one last hurdle remains — the insight Kris most wanted to convey across the whole talk: even if your microbenchmark is impeccable and its numbers real and credible, there can still be no relationship at all between it and the real performance of your whole program. That is a deeper problem than "measured inaccurately" — whether the thing you are measuring and the thing you want to optimize are even the same thing.

[Next part: A Faster Microbenchmark Is Not a Faster Program →](05-correlation-and-discipline.md)
