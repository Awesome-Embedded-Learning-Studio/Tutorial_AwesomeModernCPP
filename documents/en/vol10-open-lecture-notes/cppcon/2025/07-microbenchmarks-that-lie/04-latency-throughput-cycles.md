---
title: "Latency, Throughput, Cycles: What Are You Actually Measuring"
description: "CppCon 2025 Notes — Breaking down the fourth layer of microbenchmark liars: conflating latency and throughput. Measuring the same bit_mix function on this machine with rdtsc: latency measurement gives 37 cycles/op, throughput measurement with a data dependency gives 9.30 cycles/op"
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
  - "A Faster Microbenchmark Doesn't Mean a Faster Program"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/07-microbenchmarks-that-lie/04-latency-throughput-cycles.md
  source_hash: 10c0f1a03693b6d428c16794b4ad01d1897c6bf9dd9632cea62ecb7e17e1e9f8
  translated_at: '2026-07-29T00:33:58.679864+00:00'
  engine: anthropic
  token_count: 3724
---

# Latency, Throughput, Cycles: What Are You Actually Measuring

[The first three pieces](01-compiler-ate-your-benchmark.md) took apart the compiler, noise, and the branch predictor — these layers of liars. Even when you've handled all of them, the number you measure can still answer a question you didn't ask — because you may not even realize you asked the wrong one. The phrase "how fast is this function" hides two completely different dimensions: latency and throughput. Conflating them is the most insidious — and most common — conceptual mistake in microbenchmarking.

## First, nail down the two words

**Latency** is about a single operation: how long it takes from going in to coming out. The unit is nanoseconds per call, or cycles per call. It asks "how fast is one."

**Throughput** is about a time window: how many operations you can complete in total. The unit is operations per second, or gigabytes per second. It asks "how much work gets done per second."

They sound like the same thing — if one call is 2 nanoseconds, then a second holds 500 million calls, and the throughput is just the reciprocal of latency, right? But modern CPUs refuse to follow that simple reciprocal. The reason lies in a counterintuitive fact.

## The CPU does not wait for you

I put this realization first because it is the linchpin of this entire piece: **the CPU executes out of order, it has a deep pipeline, and inside your loop it will charge ahead as hard as it can — it does not stop and wait just because one iteration is "logically done."**

Imagine you write a loop, each iteration calls some function `f`, and you store the result. From your view, this is strictly serial: the 1st `f` finishes, then comes the 2nd. But in the CPU's eyes, as long as the result of the 1st `f` is not immediately needed by the 2nd, it can **start early** on the 2nd, the 3rd, even the 10th — stuffing different parts of multiple iterations into different execution ports to run in parallel. This is instruction-level parallelism.

The direct consequence: **throughput can be far higher than the reciprocal of latency.** A single `f` might have a latency of 5 cycles, but through out-of-order execution and pipelining the CPU might complete one `f` per cycle — throughput is five times the latency. Conversely, if you only measure latency, you'll think this function is "kind of slow"; but its real production performance may be much faster, because production is continuous calls and the CPU can parallelize multiple calls.

So depending on the question you want to ask, your benchmark code structure has to differ. Measuring latency has one way of writing it; measuring throughput has another. Write it wrong, and what you measure is not the quantity you meant.

## Same function, two measurement methods, four times apart

Let's take a lightweight bit-mixing function as our target. It's purely a string of bit operations and multiplications, with no memory access — well suited to keep the focus on the CPU pipeline:

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

First, the **latency measurement** approach. The idea: call it once at a time, time each call individually, run many iterations and take the median — try to reflect "how long one call itself takes":

```cpp
// Read TSC (hardware cycle counter) with rdtsc — more precise than chrono
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

Now the **throughput measurement** approach. Note it looks nothing like the latency measurement: instead of timing each call individually, it runs continuously for five hundred million iterations in one big loop and reads the TSC only at the start and the end. The key is that the loop body builds a **data dependency chain** — the result of each iteration feeds into the next:

```cpp
constexpr int N = 500'000'000;
std::uint64_t acc = 12345;
auto c0 = rdtsc();
for (int i = 0; i < N; ++i) acc = bit_mix(acc);  // Each iteration depends on the previous result
auto c1 = rdtsc();
double per_op = (double)(c1 - c0) / N;
```

The result on this machine (GCC 16.1.1, `-O2`):

```text
Throughput measurement (with data dependency): 9.30 cycles/op   (sink=7075159340691660003)
Latency measurement   (per-call timing): 37.00 cycles/op (median, including rdtsc overhead)
Minimum observed: 1.00 cycles/op (close to the overhead of two rdtsc calls alone)

Same bit_mix, throughput 9.30 vs latency measurement 37.00 — the difference comes from how you measure
```

Same function, two measurement methods, four times apart. That 4x is not noise, and it's not the compiler playing tricks — it's purely that the question you asked is different.

## Why such a gap, and one counterintuitive conclusion

First, that 9.30. `bit_mix` has three multiplications (`x *= ...`), each with a latency of about 3 cycles, and the three multiplications are serially dependent (each one uses the previous one's result), forming a critical path. So the minimum number of cycles to "finish one `bit_mix`" is roughly `3 × 3 = 9` cycles. The 9.30 cycles/op from the throughput measurement is exactly the length of this critical path.

Why can the throughput measurement be this accurate? Because that data dependency chain (`acc = bit_mix(acc)`) forces the CPU into having no way to parallelize across iterations: the input of iteration `i+1` is the output of iteration `i`, so the CPU must wait for iteration `i` to finish before it can start iteration `i+1`. The parallelism in the pipeline is choked off by this dependency chain, and each iteration has to honestly walk the critical path. So what gets measured is **the real one-call latency** — how many cycles a single operation actually costs.

This leads to a counterintuitive conclusion: **in this data-dependent formulation, the "throughput measurement" actually measures the real single-call latency.** It's named throughput, but because of the dependency chain, what it reflects is latency.

So what about 37.00? Why does the "per-call timing" latency measurement come out so much higher than the real latency? Because the per-call-timing method has two sources of contamination. First, the `rdtsc` instruction itself is not zero-cost — reading TSC twice with one `bit_mix` sandwiched in between means the `b - a` you measure includes at least the overhead of two TSC reads. That "minimum observed 1.00 cycles/op" is the floor when you do nothing between two TSC reads; the real overhead is higher. Second, on a single call the pipeline is cold, the function's instructions haven't entered the L1 instruction cache yet, and the front end has to fetch and decode on the fly, adding a few more beats. These two contaminants stack up and turn a function whose real latency is about 9 cycles into a measured 37 cycles.

So you see, this isn't "throughput and latency are inherently 4x apart" — it's "the per-call-timing method is inaccurate in this scenario." If you announce from this that "the latency of `bit_mix` is 37 cycles," you've been fooled by your own measurement method.

## So how do you measure it right

It depends on what you want to measure.

**To measure latency** (how long a single operation itself takes), the most reliable approach is not per-call timing but **a dependency-chained loop measurement**: use a dependency chain like `acc = f(acc)` to force serial execution, run many iterations and take the average, and what you get is the critical-path length — the real latency. That's where the 9.30 number earlier came from.

**To measure throughput** (how many calls per second), you flip it around: **actively break the dependency chain**, let the CPU parallelize multiple iterations. Take each call's input from independent sources (for example, read from a pre-generated array), and don't let this call's output become the next call's input:

```cpp
// Throughput measurement: inputs independent, CPU can parallelize across iterations
auto c0 = rdtsc();
for (int i = 0; i < N; ++i) {
    do_not_optimize(bit_mix(inputs[i]));  // inputs[i] do not depend on each other
}
auto c1 = rdtsc();
```

Under this formulation, the CPU freely stuffs different iterations' `bit_mix` into different execution ports to run in parallel, and what gets measured is throughput (calls completed per cycle), which will be noticeably higher than the reciprocal of latency.

**Don't make a typical mistake**: mixing latency measurement and throughput measurement in one benchmark. For example, writing a loop that neither builds a dependency chain nor deliberately breaks one, with inputs that are half-random half-correlated — at that point the number you measure sits somewhere between the two, neither latency nor throughput, an indescribable mixed quantity. This is how a lot of people arrive at meaningless conclusions like "my function takes about X nanoseconds."

## From wall clock to cycles

All along we've been describing performance in cycles rather than nanoseconds, and that's not an accident. Wall-clock time (nanoseconds) mixes in too much that doesn't belong to your code: OS scheduling, interrupts, cache state. Cycles are the heartbeat the CPU actually spends executing instructions, and they're a more faithful tick of "how much work your code actually did."

You read cycles with the TSC (that's the `rdtsc` inline assembly from earlier). A few things to watch for:

- **The TSC is not necessarily perfectly synchronized across cores.** Cross-core migration can throw off the reading, so it's best to pin to a core before measuring (see [the second piece](02-noise-vs-bias.md)).
- **The TSC's frequency is not necessarily equal to the CPU's rated frequency.** Modern systems have an "invariant TSC" that ticks at a fixed rate, unaffected by CPU frequency scaling — this is actually a good thing, meaning you can get stable cycle counts without locking the frequency. But if you want to convert cycles back to nanoseconds, you need to know the TSC's actual frequency; you can't just divide by the CPU's rated clock.
- **The TSC gives total cycles, not "cycles your code used."** If an interrupt or scheduling happens within the measurement window, those cycles get counted all the same. So the TSC is better than wall clock, but not so much better that it fully shields you from noise — you still need to run multiple rounds and take the median.

## Once you have cycles, how to dig further

Suppose you've measured accurately, and some code takes about 12 cycles per call. Then what? With just this number, you still don't know whether it's "fast" or "slow," much less which direction to optimize. Kris stresses a view throughout the talk: **getting the number is only the start — you have to be able to explain why the number is what it is, or the measurement has no value.**

There are two directions to dig, and given this machine's limits I'll only do a conceptual introduction here, no hands-on measurement.

The first direction is computing **IPC** (Instructions Per Cycle). You get it by dividing the number of instructions executed by the number of cycles. High IPC means the CPU is doing work most of the time; low IPC means it's waiting — waiting on memory, on branch resolution, on an execution port. Contemporary x86 has a dispatch width of roughly 4–6, so the theoretical IPC ceiling is 4–6; in practice hitting above 2 is decent, and below 1 usually means it's stuck on memory. This machine doesn't have hardware-counter reading tools (no perf installed), and measuring IPC precisely needs them, so I'll just point at it here.

The second direction is **static pipeline analysis**. LLVM ships a tool called MCA (Machine Code Analyzer): you give it a piece of assembly, and based on the CPU's scheduling model it tells you each instruction's latency, which execution port it uses, and how many cycles a whole loop iteration takes. Its value: it can predict performance without actually running the code. This machine doesn't have `llvm-mca` installed, so I can't demonstrate it hands-on, but the idea is worth knowing — when the cycles you measure line up with what MCA predicts, it means your measurement process is trustworthy; when they don't, either you measured wrong, or MCA's scheduling model is inaccurate for your CPU. Either way, it's valuable information.

::: tip One more important reminder
Don't fall into the "count assembly instructions" trap. Fewer instructions doesn't mean faster — an `imul` and an `add` both count as one instruction, but their cost differs by several times. What really determines speed is the dependency relationship between instructions, the execution ports they occupy, and pipeline bubbles. That needs resource-pressure analysis like MCA's, not counting lines. In the next piece on correlation, we'll see a live example: under `-O2` the compiler optimizes a division into a multiplication, and you'd never catch it just by counting instruction count.
:::

## The boundary of this layer

Once you separate latency from throughput and use the right measurement method, the number you get is finally both meaningful and trustworthy. But there's one last hurdle, the insight Kris most wanted to convey across the whole talk: even if your microbenchmark is impeccable and the numbers are real and trustworthy, there can still be no connection at all between it and the real performance of your whole program. That's a deeper problem than "measured inaccurately" — whether the thing you're measuring and the thing you want to optimize are the same thing at all.

[Next: A Faster Microbenchmark Doesn't Mean a Faster Program →](05-correlation-and-discipline.md)
