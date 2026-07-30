---
title: "The Branch Predictor Is Helping You Cheat"
description: "CppCon 2025 notes — taking apart the third layer of liars in microbenchmarks: the branch predictor. Measured on this machine, the same predicate function does 0.36 ns/elem on fixed input and 2.01 ns/elem on shuffled input — a 5.6x gap"
chapter: 7
order: 3
conference: cppcon
conference_year: 2025
talk_title: 'Why 99% of C++ Microbenchmarks Lie – and How to Write the 1% that Matter!'
speaker: Kris Jusiak
cpp_standard: [20]
difficulty: intermediate
platform: host
reading_time_minutes: 11
tags:
  - cpp-modern
  - host
  - intermediate
  - 优化
related:
  - "Noise You Can Suppress, Bias Is the Nightmare"
  - "Latency, Throughput, Cycles: What Are You Actually Measuring"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/07-microbenchmarks-that-lie/03-branch-prediction-cheats.md
  source_hash: 7ced5827bde04e237172eba73daef3a25afe1a83575934df9bd3a1ab31d93d2b
  translated_at: '2026-07-29T00:33:58.679864+00:00'
  engine: anthropic
  token_count: 2733
---

# The Branch Predictor Is Helping You Cheat

In the [previous two pieces](01-compiler-ate-your-benchmark.md) we took apart the compiler and runtime noise — two layers of liars. This piece is about a sneakier one. It lives inside the CPU, most developers never realize it's there, and it can flatter your benchmark numbers until you barely recognize them yourself. It's the branch predictor.

## Same Code, Different Input, 5.6x Slower

Let's write a small function with a conditional branch, structured exactly like the predicate in FizzBuzz — a chain of `if`s with modulo:

```cpp
static int classify(int n) {
    if (n % 15 == 0) return 4;   // FizzBuzz
    if (n % 5  == 0) return 3;   // Buzz
    if (n % 3  == 0) return 2;   // Fizz
    return 1;                    // plain number
}
```

Then prepare three input sets, each two million integers, differing only in how they're arranged:

- **All identical**: two million `15`s, every one hits the first branch.
- **Sequential `1..N`**: in order, cycling every 15 — a highly regular branch pattern.
- **Shuffled**: thoroughly randomized with `std::shuffle`, so the branch direction is unpredictable.

For each input set we call `classify` two million times, do nothing in between, and run 5 rounds taking the median. The code shares the same skeleton as the noise experiment in the previous piece, so here we only look at the results:

```text
All identical 15  (branch perfectly predicted):  717.0 us  (0.36 ns/elem)
Sequential 1..N  (periodic pattern):             1573.0 us  (0.79 ns/elem)
Shuffled          (predictor defeated):          4021.0 us  (2.01 ns/elem)

Shuffled / Identical = 5.61x — same code, same work, the branch predictor collapses and it slows down this much
```

Let's sit with these three lines for a moment. The `classify` function changed by not a single line, the compile flags are unchanged (`-O2`), and the amount of work is identical (two million modulo-and-compare operations each). The only thing that changed is the ordering of the input. Yet the fastest and slowest differ by **5.61x**.

If, while writing your benchmark, you casually picked a fixed value or a sequential array as input (which is what most people do, because it's the path of least resistance), the number you'd measure is that pretty 0.36 ns/elem. You'd think "my function is really fast." Then it ships to production, meets the messy input of the real world, and its actual behavior lands at the 2.01 ns/elem tier — more than five times slower, and you have no idea why.

## What the Branch Predictor Actually Does

To understand this, you first have to drop a deep-rooted illusion: the CPU does not obediently execute instructions one at a time.

Modern CPUs have deep pipelines — a dozen or even two dozen stages. When a conditional branch instruction (the `jne`/`je` corresponding to those `if`s above) enters the pipeline, which way it goes can't be settled for several cycles. But the CPU can't afford to wait — if it stalled there, the next dozen-plus pipeline stages would run dry, and that cost is called a pipeline stall. So the CPU guesses: based on this branch's past history, it predicts which way it will go this time, and **ahead of time** fetches, decodes, even begins executing the instructions on the predicted side. When the real condition is finally computed, if it guessed right, all is well — it effectively pocketed those dozen-plus stages of pipeline time for free. If it guessed wrong, the CPU has to throw away all that speculative work (this is a pipeline flush) and re-fetch from the correct path.

The cost of guessing right versus wrong is night and day. A single pipeline flush on a contemporary x86 wastes roughly 15-20 cycles. That's why the "all identical 15" case is so fast — every one of those two million iterations takes the same branch, the predictor learns it after one look, the hit rate approaches 100%, and there's almost no flushing. The "shuffled" case, on the other hand, makes every branch direction unpredictable, so the predictor can only guess blindly, and its hit rate degrades toward 50% (a coin flip). Half the branches force a pipeline flush — that's where the slowness comes from.

Modern CPU branch predictors are genuinely clever: they use global history registers, local history tables, even perceptron algorithms to learn branch patterns, and they can remember the history of thousands of branches. But their cleverness has one precondition: **your branches must have a pattern to learn**. Once the input is truly random, the predictor is helpless.

## When There's No History, How Does the CPU Guess?

A detail worth mentioning, since it occasionally shows up in interviews. When a branch appears for the first time and the predictor has no history on it at all, the CPU falls back to a static default rule:

- **Backward branches** (like the `jne` at the bottom of a loop jumping back to the loop head) are predicted **taken**, because loops almost always continue.
- **Forward branches** (like an `if` skipping a block of code when the condition fails) are predicted **not taken**, because the then-branch of an `if` is usually more common than the else-branch.

This rule explains a piece of age-old optimization advice: writing the more common logic in the then-branch and the rare logic in the else-branch pushes the static prediction hit rate a little higher. Of course, once the predictor has accumulated enough history, dynamic prediction overrides this static rule — its effect matters mainly at program startup, when the caches haven't warmed up yet.

## How to Defeat This Liar

The core idea in one sentence: **make the input to your benchmark the same distribution as the real input in your production environment**.

If your real workload is uniformly random, then use shuffled random input in the benchmark; if 90% of your real requests take the true branch and 10% take the false branch, construct your input sequence at that ratio. Whatever you do, don't measure a branch-sensitive function with a fixed constant or a `1..N` sequential array — that number is the ceiling of your code under ideal input, not its behavior under real load.

Google Benchmark paired with `<random>` is the standard approach; this machine doesn't have GB installed, but hand-rolling it isn't complicated:

```cpp
#include <random>
#include <vector>
#include <algorithm>

std::mt19937 rng(42);
std::vector<int> inputs(2'000'000);
for (size_t i = 0; i < inputs.size(); ++i) inputs[i] = static_cast<int>(i + 1);
std::shuffle(inputs.begin(), inputs.end(), rng);  // shuffle, so branches become unpredictable

// During measurement, iterate over this input set rather than passing a fixed value
size_t idx = 0;
for (auto _ : state) {
    do_not_optimize(classify(inputs[idx]));
    idx = (idx + 1) % inputs.size();
}
```

One detail worth emphasizing: the `inputs` array has to be large enough that the predictor can't memorize the entire pattern. If the array only has a few dozen elements, even shuffled, the predictor will learn it after a few laps. Two million elements is enough to keep it guessing.

If branch direction affects your function's performance especially heavily, you can go further and construct the input to match the real hit ratio. Say your business has a 5% lookup hit rate — then your input sequence should make 95% of lookups miss and 5% hit, rather than being uniformly distributed. A benchmark always measures "performance under your scenario," never "performance under a uniform distribution."

## The Caches Cheat Along With It

The branch predictor has a frequent accomplice: the cache hierarchy. It creates the same kind of illusion — your benchmark numbers look pretty because the cache state happens to be favorable.

The most typical form: **the first access is slow, everything after is fast**. The first time your data is read from memory it triggers a cache miss and costs tens to hundreds of cycles; once it's loaded it sits in L1/L2, and subsequent accesses take only a few cycles. If your benchmark runs many rounds, only the first round pays the miss cost, and everything after is a hit. The average you compute folds in one overpriced first round and then gets dragged down by hundreds of underpriced hit rounds — and that average represents neither "cold start" nor "warm steady state." It's a neither-fish-nor-fowl number.

A sneakier form: **code that's never called can still affect you**. It sounds absurd, but the mechanism is direct. When the compiler lays functions out in the binary, it does so in some order; add one more function (even one that's never called) and every function after it gets pushed to a higher address. Hot code that used to pack into one cache line may now be split across two, or even cross a page boundary. The instruction-cache hit rate changes, and so do your benchmark numbers. People have hit this trap: delete a completely unused utility function, and an unrelated hot function suddenly gets 8% faster.

We won't dig into this kind of cache-related bias with hands-on measurement in this series — it's the main battlefield of the next talk, *Cache-Friendly C++*. For now you only need one takeaway: **any time something "mysteriously speeds up or slows down by a few percent," clear the branch-predictor and cache suspects first** — odds are the root cause is one of the two.

## The Boundary of This Layer

The illusions conjured jointly by the branch predictor and the caches are, in essence, the [bias from the previous piece](02-noise-vs-bias.md) — they're systematic, not random, and no amount of re-running will suppress them. The only defense is "make the input match the real distribution." Once you strip these away, your benchmark numbers are finally fairly trustworthy. But one last cognitive trap is still waiting: the "fast versus slow" you think you're measuring — which dimension does it even refer to? Latency and throughput are handled by the CPU in completely different ways, and conflating them is another disaster zone where "you measured, but it's as good as not measuring."

[Next: Latency, Throughput, Cycles: What Are You Actually Measuring →](04-latency-throughput-cycles.md)
