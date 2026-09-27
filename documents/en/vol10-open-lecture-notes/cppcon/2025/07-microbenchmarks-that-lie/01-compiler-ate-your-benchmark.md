---
title: "The Compiler Lied to You: The #1 Microbenchmark Lie"
description: "CppCon 2025 notes — taking apart the first liar in microbenchmarking: the compiler. A GCC 16.1.1 run shows -O2 optimizing a billion-iteration summing loop down to zero executions, while sum still comes out right — courtesy of constant folding"
chapter: 7
order: 1
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
  - "Noise Can Be Suppressed: Bias Is the Real Nightmare"
  - "The Branch Predictor Is Helping You Cheat"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/07-microbenchmarks-that-lie/01-compiler-ate-your-benchmark.md
  source_hash: 11bde6f0c4f193da49fb17e3e020d8ccaab6217ec2bf80963e26efc0b062a334
  translated_at: '2026-09-26T16:29:48+00:00'
  engine: anthropic
  token_count: 5700
---

# The Compiler Lied to You: The #1 Microbenchmark Lie

::: tip Where these notes come from
This series is a free-form riff on Kris Jusiak's CppCon 2025 talk *Why 99% of C++ Microbenchmarks Lie – and How to Write the 1% that Matter!* Kris is the author of [Boost].UT and has spent years deep in compile-time computation and testing frameworks. The talk video is on [YouTube](https://www.youtube.com/watch?v=s_cWIeo9r4I). We're taking the talk's methodology apart layer by layer, and every layer of liar gets its own hands-on measurement on our machine — this is not a retelling of the slides.
:::

Let's lay it on the line up front: if you have ever written a C++ microbenchmark, gotten a pretty nanosecond number out of it, and then tuned your code against that number — odds are the compiler has fooled you, and more than once. This piece takes apart only the first liar, the one easiest to fall for: **you think you're measuring a loop, the compiler deletes the loop outright, and you spend half the day staring at a zero.**

## First, the Plainest Benchmark Imaginable

To keep our attention on the measurement itself, we picked the most boring function imaginable: add up everything from `0` to `n-1`.

```cpp
// opt_away.cpp
#include <chrono>
#include <cstdio>

static long heavy_sum(long n) {
    long acc = 0;
    for (long i = 0; i < n; ++i) acc += i;
    return acc;
}

int main() {
    const long N = 1'000'000'000L;  // 1 billion
    auto t0 = std::chrono::steady_clock::now();
    long s = heavy_sum(N);
    auto t1 = std::chrono::steady_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / N;
    std::printf("sum=%ld  per-iter=%.4f ns\n", s, ns);
}
```

The logic is simple enough that it needs no explanation. We ran it once at each of three optimization levels; the machine setup is described on the series front page: GCC 16.1.1, `-std=c++20`.

```bash
$ g++ -std=c++20 -O0 opt_away.cpp -o opt_away_O0 && ./opt_away_O0
sum=499999999500000000  per-iter=0.4567 ns
$ g++ -std=c++20 -O2 opt_away.cpp -o opt_away_O2 && ./opt_away_O2
sum=499999999500000000  per-iter=0.0000 ns
$ g++ -std=c++20 -O3 opt_away.cpp -o opt_away_O3 && ./opt_away_O3
sum=499999999500000000  per-iter=0.0000 ns
```

Let's stop and actually look at those numbers.

At `-O0`, `per-iter=0.4567 ns` — the right order of magnitude for honestly executing a billion additions, roughly one clock cycle and change of work per iteration. But at `-O2` and `-O3`, `per-iter=0.0000 ns`. A billion additions, and the measured time comes out as zero (or more precisely, gets truncated to zero by the integer division).

Weirder still: **the value of `sum` is correct**. All three optimization levels print the same `499999999500000000`, which is exactly the result of `0+1+2+...+999999999`.

If you take `-O2`'s `0.0000 ns`, compare it against another function, and announce "my summing function costs zero nanoseconds per iteration — we've hit the physical limit" — congratulations, the compiler fooled you.

## What the Compiler Actually Did

Let's take this apart as two moves.

The first move is **constant folding**. `heavy_sum`'s argument `N` is a `const long`, and the compiler can see that its value is `1000000000`. A loop summing from `0` to `N-1` produces `N*(N-1)/2` — middle-school math. The compiler evaluates that formula at compile time and bakes `499999999500000000` into the binary as an immediate. That's why `sum` is correct — it was never run at all, it was computed.

The second move is **dead code elimination**. Now that `sum` is known at compile time, the loop has no reason to exist: it produces no new information, and it reads or writes no memory the compiler can't see. The compiler deletes the whole thing.

Stack the two moves together and the net effect is: **your billion-iteration loop executed zero times.** The zero nanoseconds you measured is a truthful zero nanoseconds — because nothing actually ran.

We can verify this in the assembly. `objdump` the symbol tables of both binaries:

```bash
$ objdump -t opt_away_O0 | grep heavy_sum
0000000000000000 l    F .text  00000000000000xx  heavy_sum(long)
$ objdump -t opt_away_O2 | grep heavy_sum
# (empty)
```

At `-O0`, `heavy_sum` is a standalone function symbol; at `-O2` the symbol **is gone entirely** — it got inlined into `main`, the loop body was constant-folded away, and even the function itself disappeared. Now count the conditional jump instructions in both binaries (`jne`/`je`/`jmp` and the like — the things a loop can't live without):

```bash
$ objdump -d -M intel opt_away_O0 | grep -cE 'j(ne|e|mp|b|a|ge|le)\b'
23
$ objdump -d -M intel opt_away_O2 | grep -cE 'j(ne|e|mp|b|a|ge|le)\b'
15
```

`-O2` has 8 fewer jumps than `-O0`; the loop's jumps are gone. This is not some new trick from GCC 16.1.1 — any modern compiler does this at `-O2`, and the behavior has been stable for over a decade.

## Why This Trap Is So Easy to Fall Into

You might think: how often is `N` actually a compile-time constant in real code? We used to think that too, until enough stings taught us that the scenarios triggering this class of optimization are far more common than you'd imagine.

The most common one: **the loop's result is never really consumed**. Change `heavy_sum` to this, and assume the caller never uses the return value at all:

```cpp
long s = heavy_sum(N);
// s never appears again
```

This time `N` could just as well be read in at runtime; the compiler sees that nobody uses `s` and still kills the whole loop — it may not be able to substitute a formula, but delete it definitely will. Your benchmark reports zero nanoseconds; you think the function is fast, when in fact the function is gone.

Another common one: **the result is consumed exactly once by `printf`**. If the compiler is aggressive enough, it can move the entire computation to compile time and only splice the constant in at the print site. The "runtime" you're measuring is really the time of `printf`.

And a sneakier one: even if you store the result in a variable, as long as the compiler can prove that the variable is only read afterward and never externally observed (say, it doesn't escape the function, isn't written to global memory), it still has room to eliminate the computation. This is exactly why every serious benchmark framework asks you to do one thing — **"pin" the result somewhere the compiler can't touch.**

## Three Ways to Pin the Result Down

These three techniques differ in strength and in cost; let's take them one by one.

### Technique 1: `volatile` Forces a Memory Write

The oldest and most straightforward approach: store the accumulated result in a `volatile` variable. `volatile` tells the compiler that every read and write of this variable must honestly happen — no eliding, no folding, no moving it to compile time.

```cpp
volatile long sink = 0;
for (long i = 0; i < N; ++i) sink += i;
```

With `volatile` in place, the compiler doesn't dare delete the loop, because every `sink += i` is a real memory write (that's what `volatile` semantics demand). The loop survives.

But `volatile` has a cost: it forces a memory write on every iteration, whereas `acc` could otherwise stay in a register the whole time. What you're measuring is "a loop that carries a memory write," not "the original loop." In a plain integer sum that cost may not be obvious, but if the loop body is inherently light, the memory access `volatile` introduces will push the measurement up noticeably — you leap from one extreme (zero nanoseconds) to the other (too high).

One more thing to note: **`volatile` is not an atomic operation and does not establish a memory barrier**. It cannot be used for synchronization in multithreaded scenarios — something the standard has stressed repeatedly since C++20. Its only job here is to block the compiler's optimization.

### Technique 2: `benchmark::DoNotOptimize`

Google Benchmark ships a tool built precisely for this problem, called `DoNotOptimize`. The implementation is clever: it keeps the compiler from eliminating your data while adding as little overhead as possible.

```cpp
#include <benchmark/benchmark.h>

static void BM_Sum(benchmark::State& state) {
    long N = state.range(0);
    for (auto _ : state) {
        long acc = 0;
        for (long i = 0; i < N; ++i) acc += i;
        benchmark::DoNotOptimize(acc);  // pin acc so the compiler can't eliminate it
    }
}
BENCHMARK(BM_Sum)->Arg(1000000000);
BENCHMARK_MAIN();
```

Under the hood, `DoNotOptimize(a)` is an inline-assembly snippet with a special constraint that "feeds" the address of `a` to the compiler, making it believe `a`'s value might be observed externally — so it doesn't dare delete it. Unlike `volatile`, it doesn't force a memory write every time, so the overhead is far smaller.

Google Benchmark isn't installed on our machine, but the principle is easy to replicate. The snippet below uses a bit of inline assembly to mimic the core effect of `DoNotOptimize` — again, "trick the compiler into believing this value is used externally":

```cpp
// Simplified DoNotOptimize: use asm to make the compiler think acc may be modified asynchronously
static inline void do_not_optimize(long& x) {
    asm volatile("" : "+r"(x) : :);
}

long acc = 0;
for (long i = 0; i < N; ++i) acc += i;
do_not_optimize(acc);
```

The `"+r"(x)` constraint tells the compiler: this assembly snippet (empty as it is) reads and writes `x`, and `x` must live in a register. So the compiler no longer dares eliminate the computations around `acc` — it "sees" `acc` being consumed by a black box it cannot analyze. This is the standard idiom on both GCC and Clang, with tiny overhead — usually just a cycle or two of perturbation.

### Technique 3: `ClobberMemory` Forces a Flush

Sometimes pinning a single value isn't enough. If the loop writes to a chunk of memory, the compiler may merge, reorder, or even eliminate that entire block of writes, and pinning the final value won't stop it. That's when you flush the whole memory barrier:

```cpp
// In Google Benchmark:
benchmark::ClobberMemory();
// Equivalent hand-written version:
asm volatile("" : : : "memory");
```

The `"memory"` clobber tells the compiler: this assembly might read or write any memory. So the compiler has to make every pending memory write real before this point and cannot optimize across it. It's heavier than `DoNotOptimize`, but it blocks the more aggressive memory-related optimizations.

::: warning Don't Rush to Trust the Numbers
Build a habit: after writing a benchmark, take one look at the assembly first — `g++ -S` or Compiler Explorer — and confirm that the loop you mean to measure is still there and hasn't been folded into an immediate. It costs a few seconds and wards off most of the "measured absolutely nothing" accidents. If the function symbol you're measuring has already vanished from the assembly, or the loop body has been replaced by a couple of `mov` instructions loading a constant, the benchmark's numbers mean nothing — fix the code first.
:::

## The Boundary of This Layer

With the compiler — this layer's liar — unmasked, you step into a nastier layer. Even if the loop survives, the result is pinned, and the assembly checks out, the nanosecond number you get can still be lying — because the machine running it never stops jittering: the operating system can switch you out at any moment, CPU frequency drifts up and down, and what's sitting in the caches is entirely out of your control. Together these add up to noise, and they make your numbers bounce around.

Even more obnoxious is a category called bias. It isn't random bouncing — it systematically drags your numbers in one direction, no matter how many times you run.

The two have completely different natures, and the ways to fight them are completely different too. We take them apart one by one in the next piece.

[Next: Noise Can Be Suppressed: Bias Is the Real Nightmare →](02-noise-vs-bias.md)
