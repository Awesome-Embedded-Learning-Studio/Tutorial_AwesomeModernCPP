---
title: "A Faster Microbenchmark Is Not a Faster Program"
description: "CppCon 2025 notes — the closing installment. We tear down the final hurdle: correlation. A local test shows the FizzBuzz lookup-table approach beating the if-chain by 37% in a microbenchmark, yet the assembly reveals the modulo was long since strength-reduced into multiplication — a microbenchmark victory that may not survive the trip to production"
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
  - "The Compiler Lied to You: The #1 Microbenchmark Lie"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/07-microbenchmarks-that-lie/05-correlation-and-discipline.md
  source_hash: f8726d4e79e645a2fb08f593c489c3022d475e878c5c586c6dda5afeca2f3bde
  translated_at: '2026-09-26T16:39:52+00:00'
  engine: anthropic
  token_count: 6200
---

# A Faster Microbenchmark Is Not a Faster Program

[Over the first four articles](01-compiler-ate-your-benchmark.md) we peeled the onion layer by layer: the compiler deletes your loops, noise jitters, bias skews things systematically, the branch predictor and the cache team up to flatter your numbers, and latency and throughput get stirred into one mush. Handle all of them, and your microbenchmark is finally impeccable — real numbers, sound methodology, controlled error. And then? Then you take that beautiful number, optimize your code around it, ship it... and production performance doesn't budge an inch. It might even get slower.

Nothing spooky is going on. This is the one concept Kris most wanted to cram into your head over the whole talk: correlation. Is the fast-or-slow your microbenchmark measures the same fast-or-slow as the program you actually want to optimize?

## First, a Microbenchmark That Looks Like an Optimization Success

Let's use FizzBuzz as our example, because it's simple enough that all the attention lands on the measurement itself. We'll write two versions. The first is a naive `if` chain that does three modulo operations:

```cpp
const char* fizzbuzz_naive(int n) {
    if (n % 15 == 0) return "FizzBuzz";
    if (n % 5  == 0) return "Buzz";
    if (n % 3  == 0) return "Fizz";
    return "";
}
```

The second is the "theoretically superior" lookup-table version — compute one modulo, let a table do the rest:

```cpp
static const char* lookup[16] = {
    "","","","Fizz","","Buzz","Fizz","","","Fizz",
    "Buzz","","Fizz","","","FizzBuzz"
};
const char* fizzbuzz_lookup(int n) {
    return lookup[n % 15];
}
```

Intuitively the lookup should be faster: it does a single modulo, while the naive version does up to three. We run each for 2 million rounds, each round covering `1..15`. On this machine (GCC 16.1.1, `-O2`):

```text
naive  if链:  20355.0 us,  20586.0 us
lookup 查表:  13001.0 us,  12595.0 us
```

The lookup is about 37% faster. If this microbenchmark were all you could see, you'd gleefully merge the lookup version and declare the optimization a success. But this step proves nothing — only that "in the microbenchmark's peculiar environment, with its tiny data footprint and highly regular inputs, the lookup won."

## One Look at the Assembly, and the Victory Sours

Before we rush to celebrate, let's look down at what the compiler actually generated. Compile the `naive` version to assembly:

```bash
$ g++ -std=c++20 -O2 -S corr.cpp -o corr.s
$ grep -E "imul|div|idiv|sar" corr.s | head -8
  imull  $-286331153, %edi, %eax
  imull  $-858993459, %edi, %eax
  imull  $-1431655765, %edi, %edi
  ...
```

All `imull` (integer multiply), without a single `div` or `idiv` in sight. The compiler took every one of those modulos — `n % 15`, `n % 5`, `n % 3` — and **strength-reduced** them into "multiply by a magic number, then shift." Those intimidating-looking constants (`-286331153`, `-858993459`) are exactly the multiplicative replacements the compiler worked out for "divide by 15/5/3."

What this means: the naive version's "up to three modulos" handicap isn't a modulo handicap at all under `-O2` — it's three multiply-add sequences. All the lookup version saves is the cost of "computing two extra magic-number multiplications." In the microbenchmark the data is tiny, that 16-entry `lookup` table sits in L1 the whole time, and a table lookup is nearly free — so it wins.

But in a real program? That table can get evicted from L1 by the unrelated code around it; the real input distribution may concentrate the results of `n % 15` so heavily that the naive version returns on the first `if` and never executes the remaining modulos at all; or the function gets inlined into a big loop, and register-allocation pressure exposes the lookup's memory-access cost. In those scenarios, the 37% edge from the microbenchmark can evaporate — or even reverse.

This is the heart of the correlation problem: **the environment your microbenchmark runs in and the real environment you want to optimize are often not the same thing.**

## The F1 Wind Tunnel Analogy

Kris made a particularly apt analogy in the talk: wind-tunnel testing of F1 race cars.

The team blows air over a car model in the wind tunnel, gets beautiful aerodynamic numbers, and redesigns the car accordingly. But if the airflow conditions in the tunnel aren't the same as the airflow the car actually faces on the track, the tunnel data is worthless no matter how good it looks — it has no correlation with track performance.

A microbenchmark is the programmer's wind tunnel. You lift a piece of code out on its own, measure it in a carefully controlled little environment, and get precise numbers. Nothing wrong with the process itself — it's fast, iteration is cheap, and it's great for validating ideas. But it carries a fatal precondition, one that keeps getting ignored: **microbenchmark results are only meaningful after you've verified that they correlate with real-world performance.**

I've fallen into this pit myself. Once, I was optimizing a string-processing function: 40% faster in the microbenchmark, because the function's hot path happened to fit into L1 cache perfectly. After it was merged into the service, that function was preceded by a long stretch of JSON-parsing code that wrecked the cache, and the "optimization" actually introduced more memory accesses — the net result was slower. At the time I couldn't make sense of it; looking back now, it was simply correlation that had never been established.

## How to Establish Correlation

Establishing correlation is not one action — it's a verification chain running from the microscopic to the macroscopic. My current workflow looks like this:

Step one, the microbenchmark. It's fast and cheap, ideal for trying ideas quickly. But its results can only serve as a **candidate signal**, never a conclusion. What you get at this step is "this change might bring an X% improvement under ideal conditions."

Step two, medium-scale testing. Put the optimization inside a complete module that calls it, and see whether it's still there in a context closer to reality. This step exposes many problems a microbenchmark can hide: register pressure, cache contention, interaction with other code.

Step three, production-load testing. Put it under real traffic, real data distributions, real concurrency, and see whether the end-to-end metrics actually improve. Only when the results point the same way at all three levels can you say "this optimization works" — which is to say, correlation has been established.

If some step doesn't line up — the microbenchmark got faster but end-to-end didn't move, say — don't be discouraged; this is the best learning opportunity there is. Dig into why it doesn't line up, and your understanding of CPU and system behavior will deepen by a layer. A mismatch usually means a noise source or bias source you didn't know about yet is hiding in there.

::: warning A Question You Must Be Able to Answer
Once the measurement is done, you must be able to answer this question: **why this number?** If you can't articulate what's behind "it got 20% faster" — fewer instructions, a higher cache hit rate, more accurate branch prediction, or relieved execution-port conflicts — then the measurement is just a number you happened to hit, and you have no way to judge whether it still holds in other scenarios. Understanding the cause matters far more than obtaining the number itself.
:::

## Keep the Optimization Alive, and Don't Let It Silently Vanish

Suppose you've established correlation, and the optimization genuinely works. One last thing can still flush your effort down the drain: **can this optimization continue to exist in production?**

An optimization you measured as effective today can silently disappear tomorrow when someone changes an unrelated line of code, bumps the compiler version, or switches to a different build configuration. And your microbenchmark won't feel a thing — because the call path that triggered the optimization may have been inlined away. Kris's solution is to upgrade "observation" into "testing": turn the tool output you used to inspect manually into assertions that a machine checks automatically.

The most practical flavor is the **disassembly contract test**. If you expect the compiler to perform a particular optimization on particular code, write that expectation down as a test. For example, if you want `n % 15` strength-reduced into multiplication with no division instruction appearing, check whether the function's machine code contains `idiv`:

```cpp
// Dump the function's machine code and check for division instructions (x86 idiv is the 0xF7 family)
bool has_division_instruction(const void* func, std::size_t bytes) {
    const auto* p = static_cast<const std::uint8_t*>(func);
    for (std::size_t i = 0; i < bytes; ++i) {
        // Simplified: an exact check would need to account for prefixes and ModRM; this only sketches the idea
        if (p[i] == 0xF7) return true;   // rough hit
    }
    return false;
}
```

This kind of test has an obvious drawback: it depends on the exact byte encoding, so a different compiler version or different flags can break it — which makes it better suited to regression detection in projects with a locked-down toolchain version. But its value is that it turns "what code the compiler should generate" from an ad-hoc behavior inspected by the human eye into an assertion that CI runs every single time. The moment the optimization regresses, the test goes red.

Correctness verification works the same way, and matters even more: a benchmark that runs fast but computes wrong results is meaningless. Google Benchmark provides the `state.PauseTiming()` and `state.ResumeTiming()` pair of interfaces, letting you insert an untimed correctness check inside the timing loop:

```cpp
for (auto _ : state) {
    std::vector<int> data = base;
    my_sort(data);
    benchmark::DoNotOptimize(data.data());
    state.PauseTiming();          // verification is not counted toward the timing
    verify_sorted(data);          // assert that it is sorted
    state.ResumeTiming();
}
```

However slow the verification logic runs, it never pollutes the performance data — yet the moment the sort goes wrong, the assert lets you know immediately. I've used this pattern in several projects to catch incidents of the "three times faster after optimizing, and the data is completely garbled" variety.

## The Methodology Kris Leaves Behind

At this point we've walked the whole chain: from "why nanoseconds matter," through unmasking the liars layer by layer, to establishing correlation, to keeping optimizations alive. Looking back and tying it together, a dependable microbenchmark discipline boils down to roughly these rules:

**First, always measure — but never with the "run it once and divide by the count" approach that's barely better than nothing.** That practice is more dangerous than not measuring at all, because it hands you a false certainty. You get a number precise down to the nanosecond, with no idea how many factors you don't know about have distorted it.

**Second, understand the interference sources layer by layer.** The [compiler](01-compiler-ate-your-benchmark.md) is one layer, runtime [noise and bias](02-noise-vs-bias.md) another, [the branch predictor and caches](03-branch-prediction-cheats.md) another, and [the measurement dimension itself](04-latency-throughput-cycles.md) yet another. Each layer has its own countermeasures; mix them up and the effort is wasted.

**Third, control your variables.** Pin cores, lock frequencies, warm up, fix compiler flags and link order, make inputs match the real distribution. None of these actions is complicated, but they're easy to forget — and once forgotten, the results stop being reproducible.

**Fourth, don't cut corners on statistics.** Look at distributions, not just the mean; use the median for skewed distributions; attach error bars to comparisons.

**Fifth, and most fundamental: after measuring, answer "why this number."** The moment you can narrate the causal chain end to end — tracing a cycles difference back to the cache, the cache back to the data layout, the data layout back to that one code change — that's when you truly understand what your program is doing, rather than having luckily guessed a number.

All of this sounds long-winded, and it isn't cheap. You might ask: is it worth it? If you work in high-frequency trading, kernels, database engines, game engines — domains where a nanosecond is money, or a nanosecond is user experience — there's hardly another road. But even if you just write ordinary business code, understanding "whether my measurements can be trusted" is universally useful — it spares you countless embarrassments of the "optimized everything and got nothing" kind, and when you genuinely need to squeeze performance, you'll know which direction to push.

That is the biggest takeaway Kris's talk left me with: not any specific technique, but a sense of discipline — **stay skeptical of every number until you can explain why it is the number it is.** Let's hold ourselves to that, together.
