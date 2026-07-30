---
title: "A Faster Microbenchmark Is Not a Faster Program"
description: "CppCon 2025 notes — the closing chapter. We tear down the last barrier: correlation. A local measurement of the FizzBuzz lookup-table version shows it 37% faster than the if-chain in a microbenchmark, but the assembly reveals the modulo was already strength-reduced into multiplication long ago — the microbenchmark's win may not survive the trip to production"
chapter: 7
order: 5
conference: cppcon
conference_year: 2025
talk_title: 'Why 99% of C++ Microbenchmarks Lie – and How to Write the 1% that Matter!'
speaker: Kris Jusiak
cpp_standard: [20]
difficulty: intermediate
platform: host
reading_time_minutes: 14
tags:
  - cpp-modern
  - host
  - intermediate
  - 优化
related:
  - "Latency, Throughput, Cycles: What Are You Actually Measuring"
  - "The Compiler Lied to You: The Number-One Microbenchmark Deception"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/07-microbenchmarks-that-lie/05-correlation-and-discipline.md
  source_hash: f8726d4e79e645a2fb08f593c489c3022d475e878c5c586c6dda5afeca2f3bde
  translated_at: '2026-07-29T00:33:58.679864+00:00'
  engine: anthropic
  token_count: 3377
---

# A Faster Microbenchmark Is Not a Faster Program

[The previous four pieces](01-compiler-ate-your-benchmark.md) took the layers apart one by one: the compiler will delete your loop, noise will jitter, bias will pull systematically, the branch predictor and the cache will team up to flatter the numbers, and latency and throughput will get tangled together. Handle all of them and your microbenchmark finally looks beyond reproach — real numbers, sound method, controlled error. Then what? Then you take that pretty number, ship it, and production performance does not budge — it might even get slower.

This is not a haunting. It is the one concept Kris wants most to plant in your head: correlation. Is the speed your microbenchmark measures the same thing as the speed of the program you actually want to optimize?

## First, a microbenchmark that looks like an "optimization win"

We will use FizzBuzz as the example, because it is simple enough to keep all the attention on the measurement itself. Two versions. The first is a naive `if`-chain doing three modulo operations:

```cpp
const char* fizzbuzz_naive(int n) {
    if (n % 15 == 0) return "FizzBuzz";
    if (n % 5  == 0) return "Buzz";
    if (n % 3  == 0) return "Fizz";
    return "";
}
```

The second is the "theoretically superior" lookup-table approach — one modulo, then a table lookup:

```cpp
static const char* lookup[16] = {
    "","","","Fizz","","Buzz","Fizz","","","Fizz",
    "Buzz","","Fizz","","","FizzBuzz"
};
const char* fizzbuzz_lookup(int n) {
    return lookup[n % 15];
}
```

Intuitively the lookup should be faster: it does one modulo, while the naive version does up to three. We run each for 2 million rounds, each round covering `1..15`, on this machine (GCC 16.1.1, `-O2`):

```text
naive  if-chain:  20355.0 us,  20586.0 us
lookup table:     13001.0 us,  12595.0 us
```

The lookup-table version is about 37% faster. If this microbenchmark were all you saw, you would cheerfully merge the lookup table into the code and call the optimization a success. But this step proves nothing — it only proves that "in the specific, tiny-data, highly-regular environment of the microbenchmark, the lookup table wins".

## Look at the assembly and the win smells less sweet

Let us hold the celebration and look down at what the compiler actually produced. Compile the `naive` version to assembly:

```bash
$ g++ -std=c++20 -O2 -S corr.cpp -o corr.s
$ grep -E "imul|div|idiv|sar" corr.s | head -8
  imull  $-286331153, %edi, %eax
  imull  $-858993459, %edi, %eax
  imull  $-1431655765, %edi, %edi
  ...
```

All `imull` (integer multiply), not a single `div` or `idiv`. The compiler took `n % 15`, `n % 5`, `n % 3` and **strength-reduced** every one of them into "multiply by a magic number then shift". Those scary constants (`-286331153`, `-858993459`) are the multiplicative replacements the compiler computed for "divide by 15/5/3".

What this means: the naive version's "up to three modulo" penalty is, under `-O2`, not really modulo at all — it is three multiply-add sequences. What the lookup table saves is just the cost of "computing the extra two magic multipliers". In the microbenchmark the data set is tiny and that 16-entry `lookup` table sits in L1 the whole time, so the lookup is nearly free and it wins.

But in a real program? That table can get evicted from L1 by unrelated code around it. The real input distribution may concentrate the results of `n % 15` so heavily that the naive version's first `if` hits and returns, and the later modulo never executes. Or the function gets inlined into a large loop and register-allocation pressure exposes the cost of the lookup's memory access. In any of those scenarios, the 37% advantage from the microbenchmark can evaporate or even reverse.

That is the core of the correlation problem: **your microbenchmark's environment and the real environment you want to optimize are often not the same thing.**

## The F1 wind-tunnel analogy

Kris reaches for an analogy that fits perfectly: wind-tunnel testing for an F1 car.

The team blows air over a model in the wind tunnel, gets beautiful aerodynamic numbers, and reworks the design to match. But if the airflow in the tunnel is not the airflow the car actually meets on the track, the tunnel numbers are worthless no matter how pretty — they have no correlation with track performance.

The microbenchmark is the programmer's wind tunnel. You lift a piece of code out on its own, measure it in a carefully controlled little environment, and get a precise number. Nothing about that process is wrong — it is fast, cheap to iterate on, well suited to testing ideas. But it carries a fatal precondition, one that is routinely ignored: **a microbenchmark's result is only meaningful after you have verified that it correlates with real-world performance.**

I have fallen into this trap myself. Once I optimized a string-processing function that ran 40% faster in the microbenchmark, because its hot path happened to fit perfectly into L1. After it went into the service, that function was preceded by a long stretch of JSON-parsing code that wrecked the cache — the "optimization" introduced more memory accesses and ended up slower. At the time I could not fathom why. Looking back now, the correlation had never been established.

## How to build correlation

Building correlation is not one action; it is a chain of verification running from micro to macro. My current workflow looks like this.

Step one, the microbenchmark. It is fast, cheap, good for testing ideas quickly. But treat its result only as a **candidate signal**, not a conclusion. What you get out of this step is "this change might bring an X% improvement under ideal conditions".

Step two, a medium-scale test. Run the optimization inside a complete module that calls it, and see whether the optimization still holds in a more realistic context. This step exposes a lot of problems the microbenchmark cannot hide: register pressure, cache contention, interaction with other code.

Step three, production-load testing. Put it under real traffic, real data distribution, real concurrency, and see whether the end-to-end metrics actually improve. Only when the direction agrees across all three layers can you say "this optimization works" — only then have you built correlation.

If a layer does not line up — say the microbenchmark got faster but end-to-end did not move — do not be discouraged; that is the best learning opportunity there is. Go dig into why it did not line up, and you will understand the behavior of the CPU and the system one level deeper. When things do not line up, there is usually a noise source or bias source hiding that you did not know about.

::: warning A question you must be able to answer
After the measurement is done, you must be able to answer this question: **why this number?** If you cannot explain what is behind "it got 20% faster" — fewer instructions, higher cache hit rate, more accurate branch prediction, relieved execution-port contention — then the measurement is just a coincidental number, and you cannot judge whether it still holds in another scenario. Understanding the reason matters far more than getting the number.
:::

## Make the optimization stick — do not let it quietly disappear

Suppose you have built correlation and the optimization really works. There is one last thing that can flush the effort down the drain: **can this optimization keep existing in production?**

The optimization you proved effective today may silently vanish tomorrow when someone changes an unrelated line, bumps the compiler version, or switches a build configuration. And you will not feel a thing in the microbenchmark — because the call path that triggered the optimization may already have been replaced by inlining. Kris's answer is to upgrade "observation" into "testing": turn the output of the tools you used to eyeball things into machine-checked assertions.

The most practical flavor is the **disassembly contract test**. When you expect the compiler to perform a particular optimization on a particular piece of code, write that expectation as a test. For instance, if you want `n % 15` to be strength-reduced into multiplication with no divide instruction, check the function's machine code for `idiv`:

```cpp
// Dump the function's machine code and check for a divide instruction (x86 idiv is the 0xF7 family)
bool has_division_instruction(const void* func, std::size_t bytes) {
    const auto* p = static_cast<const std::uint8_t*>(func);
    for (std::size_t i = 0; i < bytes; ++i) {
        // Simplified: precise detection must account for prefixes and ModRM; this only sketches the idea
        if (p[i] == 0xF7) return true;   // rough hit
    }
    return false;
}
```

This kind of test has an obvious drawback: it depends on specific bytecodes and may break across compiler versions or flags, so it is better suited to regression checks in projects with a locked toolchain. Its value is that it turns "what code the compiler should generate" from a human-eyeballed, occasional behavior into an assertion CI runs every time. The moment the optimization regresses, the test goes red.

The same goes for correctness verification, and it matters even more: a benchmark that runs fast but produces wrong results is meaningless. Google Benchmark provides the `state.PauseTiming()` and `state.ResumeTiming()` pair so you can insert an untimed correctness check inside the timing loop:

```cpp
for (auto _ : state) {
    std::vector<int> data = base;
    my_sort(data);
    benchmark::DoNotOptimize(data.data());
    state.PauseTiming();          // verification does not count toward timing
    verify_sorted(data);          // assert that it is sorted
    state.ResumeTiming();
}
```

Now no matter how slow the verification logic runs, it does not pollute the performance data — but the moment the sort produces wrong output, the assert tells you. I have used this pattern in several projects to catch "optimized three times faster, output is garbage" incidents.

## The methodology Kris leaves behind

We have now walked the whole chain: from "why nanoseconds matter", through unmasking the liars layer by layer, to building correlation, to making the optimization stick. Stepping back, a reliable microbenchmark discipline comes down to roughly these.

**First, always measure — but not with the "run once, divide by count" non-method.** That approach is more dangerous than not measuring at all, because it hands you a false certainty. You get a number precise to the nanosecond and have no idea how many unknown factors have distorted it.

**Second, layer your understanding of interference.** The [compiler](01-compiler-ate-your-benchmark.md) is one layer, runtime [noise and bias](02-noise-vs-bias.md) another, the [branch predictor and cache](03-branch-prediction-cheats.md) another, and the [measurement dimension itself](04-latency-throughput-cycles.md) another. Each layer has its own countermeasure; mix them up and the work is wasted.

**Third, control the variables.** Pin cores, lock frequency, warm up, fix compile options and link order, keep inputs close to the real distribution. None of these actions is complicated, but they are easy to forget, and once forgotten the results stop reproducing.

**Fourth, do not cut corners on statistics.** Look at the distribution, not just the mean; use the median for skewed distributions; carry error bars on comparisons.

**Fifth, and the most fundamental: after measuring, answer "why this number".** When you can tell the causal chain end to end — tracing a cycles delta back to the cache, the cache back to the data layout, the data layout back to that one code change — that is the moment you actually understand what your program is doing, rather than happening to guess a number right.

All of this sounds tedious, and it is not cheap. You may ask whether it is worth it. If you work in high-frequency trading, kernels, database engines, game engines — domains where a nanosecond is money or a nanosecond is user experience — there is hardly a second path. But even if you only write ordinary business code, understanding "is my measurement trustworthy" is universal — it saves you from countless episodes of "optimized nothing", and when you genuinely need to squeeze performance, you know which way to push.

This is the biggest thing I took from Kris's talk: not a specific technique, but a discipline — **stay suspicious of every number until you can explain why it is that number.** Let us hold one another to that.
