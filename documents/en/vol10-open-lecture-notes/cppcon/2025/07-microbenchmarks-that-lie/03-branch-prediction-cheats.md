---
title: "The Branch Predictor Is Helping You Cheat"
description: "CppCon 2025 notes — taking apart the third layer of microbenchmark liars: the branch predictor. Measured on this machine, the same predicate function runs at 0.36 ns/elem on fixed input and 2.01 ns/elem on shuffled input — a 5.6x gap"
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
  - 'Noise Can Be Suppressed: Bias Is the Real Nightmare'
  - 'Latency, Throughput, Cycles: What Are You Actually Measuring'
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/07-microbenchmarks-that-lie/03-branch-prediction-cheats.md
  source_hash: 7ced5827bde04e237172eba73daef3a25afe1a83575934df9bd3a1ab31d93d2b
  translated_at: '2026-09-26T16:31:00+00:00'
  engine: anthropic
  token_count: 1800
---

# The Branch Predictor Is Helping You Cheat

In the [first two parts](01-compiler-ate-your-benchmark.md) we took apart two layers of liars: the compiler and runtime noise. This part is about a sneakier one. It lives inside the CPU, the vast majority of developers have no idea it exists, and it can flatter your benchmark numbers until you barely recognize them yourself. It's the branch predictor.

## Same Code, Different Input, 5.6x Slower

Let's write a small function with a conditional branch, structured exactly like the FizzBuzz predicate — a chain of `if`s with modulo:

```cpp
static int classify(int n) {
    if (n % 15 == 0) return 4;   // FizzBuzz
    if (n % 5  == 0) return 3;   // Buzz
    if (n % 3  == 0) return 2;   // Fizz
    return 1;                    // plain number
}
```

Then prepare three input sets, each two million integers, differing only in how they are arranged:

- **All identical**: two million `15`s — every single one hits the first branch.
- **Sequential `1..N`**: in order, cycling every 15 values, a highly regular branch pattern.
- **Shuffled**: thoroughly randomized with `std::shuffle`, so the branch direction is unpredictable.

For each input set we call `classify` two million times with nothing else in between, running 5 rounds per set and taking the median. The code shares its skeleton with the noise experiment in the previous part, so here we only look at the results:

```text
All identical 15  (branch perfectly predicted):  717.0 us  (0.36 ns/elem)
Sequential 1..N  (periodic pattern):             1573.0 us  (0.79 ns/elem)
Shuffled         (predictor defeated):           4021.0 us  (2.01 ns/elem)

Shuffled / Identical = 5.61x — same code, same amount of work, and this is what it costs when branch prediction collapses
```

Let's sit with these three lines for a moment. Not one line of `classify` changed, the compile flags are unchanged (`-O2`), and the amount of work is exactly the same (two million modulo-and-compare operations either way). The only thing that changed is the ordering of the input. And yet the fastest and the slowest differ by **5.61x**.

If, when writing your benchmark, you casually reach for a fixed value or a sequential array as the input (which is what most people do, because it takes the least effort), the number you measure is that pretty 0.36 ns/elem. You think "my function is plenty fast." Then it ships to production, faces the messy input of the real world, and its actual behavior lands in the 2.01 ns/elem tier — more than five times slower, and you have no idea why.

## What the Branch Predictor Is Doing

To understand this, you first have to let go of a deep-rooted illusion: the CPU does not obediently execute instructions one at a time.

Modern CPUs have deep pipelines — a dozen or even twenty-plus stages. When a conditional branch instruction (say the `jne`/`je` that those `if`s compile to) enters the pipeline, which way it goes won't be settled for several more cycles. But the CPU can't afford to wait: if it just sat there, the dozen-plus stages behind it would run dry, and that cost is called a pipeline stall. So the CPU guesses: based on this branch's past history, it predicts which way it will go this time, and **ahead of time** fetches, decodes, and even begins executing the instructions on the predicted side. When the real condition finally computes, if the guess was right, everyone goes home happy — it's as if it pocketed those dozen-plus pipeline stages of time for free. If the guess was wrong, the CPU has to throw away all the work it did early (this is called a pipeline flush) and re-fetch instructions from the correct path.

The cost of guessing right versus wrong is night and day. A single pipeline flush on a contemporary x86 wastes roughly 15-20 cycles. That's why the "all identical 15" case is so fast — every one of the two million iterations takes the same branch, the predictor learns it after one look, the hit rate approaches 100%, and there is essentially no flushing. The "shuffled" case, in contrast, makes every branch direction unpredictable, so the predictor can only guess blind and its hit rate degrades toward 50% (a coin flip between two options) — half the branches force a pipeline flush, and that's where the slowness comes from.

Modern CPU branch predictors are genuinely clever: inside, they use global history registers, local history tables, even perceptron-based algorithms to learn branch patterns, and they can remember the history of thousands upon thousands of branches. But their cleverness has one precondition: **your branches must have a pattern worth learning**. Once the input is truly random, the predictor is lost.

## How the CPU Guesses When It Has No History

A detail worth mentioning in passing — it occasionally shows up in interviews. When a branch appears for the first time and the predictor has no history on it at all, the CPU falls back on a static default rule:

- **Backward branches** (like the `jne` at the bottom of a loop jumping back to the loop head) are predicted **taken** by default, because loops continue the vast majority of the time.
- **Forward branches** (like an `if` skipping over a block of code when the condition fails) are predicted **not taken** by default, because an `if`'s then-branch usually runs more often than its else-branch.

This rule explains a piece of age-old optimization advice: putting the more common logic in the `if`'s then-branch and the rare logic in its else-branch nudges the static prediction hit rate a little higher. Of course, once the predictor has accumulated enough history, dynamic prediction overrides this static rule — its effect matters mainly when the program has just started and the caches haven't warmed up yet.

## How to Deal With This Liar

The core idea in one sentence: **make your benchmark input the same distribution as the real input in your production environment**.

If your real workload is uniformly random, then use shuffled random input in the benchmark; if 90% of your real requests take the true branch and 10% take the false branch, then construct your input sequence at exactly that ratio. Whatever you do, never measure a branch-sensitive function with a fixed constant or a `1..N` sequential array — that number is your code's ceiling under ideal input, not its behavior under real load.

Google Benchmark paired with `<random>` is the standard approach; this machine doesn't have GB installed, but hand-rolling it isn't complicated:

```cpp
#include <random>
#include <vector>
#include <algorithm>

std::mt19937 rng(42);
std::vector<int> inputs(2'000'000);
for (size_t i = 0; i < inputs.size(); ++i) inputs[i] = static_cast<int>(i + 1);
std::shuffle(inputs.begin(), inputs.end(), rng);  // shuffle, so the branches become unpredictable

// during measurement, iterate over this input set instead of passing a fixed value
size_t idx = 0;
for (auto _ : state) {
    do_not_optimize(classify(inputs[idx]));
    idx = (idx + 1) % inputs.size();
}
```

One detail here deserves emphasis: the `inputs` array must be large enough that the predictor can't memorize the whole pattern. If the array holds only a few dozen elements, even shuffled, the predictor will have it learned after a few laps. Two million elements are enough to keep it guessing blind.

If branch direction matters especially heavily to your function's performance, you can go one step further and construct the input according to the real hit ratio. Say lookups in your business hit 5% of the time — then your input sequence should make 95% of lookups miss and 5% hit, rather than being uniformly distributed. A benchmark always measures "performance under your scenario," never "performance under a uniform distribution."

## The Caches Cheat Along With It

The branch predictor also has a frequent accomplice: the cache hierarchy. The illusion it creates is cut from the same cloth — your benchmark numbers look pretty because the cache state happens to be favorable.

The most typical form: **the first access is slow, everything after is fast**. The first time your data is read in from memory it triggers a cache miss and costs tens to over a hundred cycles; but once it's loaded it stays in L1/L2, and subsequent accesses cost only a few cycles. If your benchmark runs many rounds, only the first round pays the miss price and everything afterward is a hit — the average you compute folds in one overpriced first round and then gets flattened out by hundreds of underpriced hit rounds. That average represents neither "cold start" nor "warm steady state" — it's a chimera of the two.

A sneakier form: **code that never gets called can still affect you**. It sounds absurd, but the mechanism is direct. The compiler lays functions into the binary in some order; add one more function (even one that's never called) and every function after it gets pushed to a higher address. Hot code that used to pack into a single cache line may now be split across two, or even straddle a page boundary. The instruction-cache hit rate changes, and your benchmark numbers change with it. Someone has hit this exact trap: delete a completely unused utility function, and an unrelated hot function suddenly gets 8% faster.

We won't run hands-on measurements for this cache-related bias in this series, because it is the main battlefield of the next talk, *Cache-Friendly C++*. For now you only need to remember one conclusion: **whenever something "inexplicably gets a few percent faster or slower," clear the branch-prediction and cache suspects first** — odds are the root cause is one of the two.

## The Boundary of This Layer

The illusions conjured jointly by the branch predictor and the caches are, in essence, the [bias we covered in the previous part](02-noise-vs-bias.md) — they are systematic, not random, and no amount of re-running will press them down; the only defense is "make the input match the real distribution." Once you've stripped these away, your benchmark numbers are finally fairly trustworthy. But one last cognitive trap still waits: the "fast or slow" you think you're measuring — which dimension does that even refer to? Latency and throughput are handled by the CPU in completely different ways, and conflating them is another disaster zone where "you measured, but it's as good as not measuring."

[Next part: Latency, Throughput, Cycles: What Are You Actually Measuring →](04-latency-throughput-cycles.md)
