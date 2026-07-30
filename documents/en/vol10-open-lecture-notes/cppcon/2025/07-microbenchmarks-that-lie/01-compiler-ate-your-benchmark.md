---
title: "The Compiler Lied to You: The #1 Microbenchmark Lie"
description: "CppCon 2025 notes — taking apart the first liar in microbenchmarking: the compiler. A GCC 16.1.1 run shows -O2 collapsing a billion-iteration sum loop into zero, with sum still correct thanks to constant folding"
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
  - "Noise You Can Tame, Bias Is the Nightmare"
  - "The Branch Predictor Is Helping You Cheat"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/07-microbenchmarks-that-lie/01-compiler-ate-your-benchmark.md
  source_hash: 11bde6f0c4f193da49fb17e3e020d8ccaab6217ec2bf80963e26efc0b062a334
  translated_at: '2026-07-29T00:33:58.679864+00:00'
  engine: anthropic
  token_count: 3119
---

# The Compiler Lied to You: The #1 Microbenchmark Lie

::: tip Where these notes come from
This series branches off from Kris Jusiak's CppCon 2025 talk *Why 99% of C++ Microbenchmarks Lie – and How to Write the 1% that Matter!* Kris is the author of [Boost].UT and has been deep in compile-time computation and testing frameworks for years. The talk video is on [YouTube](https://www.youtube.com/watch?v=s_cWIeo9r4I). I'm taking the methodology from the talk apart, and each layer of liar gets a hands-on measurement on my own machine — this isn't a retelling of the slides.
:::

I'll say it up front: if you've ever written a C++ microbenchmark, gotten a nice clean nanosecond number, and then optimized your code against it — you've almost certainly been fooled by the compiler, more than once. This piece only takes apart the first liar, the easiest one to fall into: **you think you're measuring a loop, the compiler deletes the loop entirely, and you stare at a zero for half the day.**

## First, a plain-vanilla benchmark

To keep our attention on the measurement itself, let's pick the most boring function imaginable: sum everything from `0` to `n-1`.

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
    const long N = 1'000'000'000L;  // 1 billion iterations
    auto t0 = std::chrono::steady_clock::now();
    long s = heavy_sum(N);
    auto t1 = std::chrono::steady_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / N;
    std::printf("sum=%ld  per-iter=%.4f ns\n", s, ns);
}
```

The logic is simple enough not to need explanation. We run it once at each of three optimization levels; my machine setup is on the series front page: GCC 16.1.1, `-std=c++20`.

```bash
$ g++ -std=c++20 -O0 opt_away.cpp -o opt_away_O0 && ./opt_away_O0
sum=499999999500000000  per-iter=0.4567 ns
$ g++ -std=c++20 -O2 opt_away.cpp -o opt_away_O2 && ./opt_away_O2
sum=499999999500000000  per-iter=0.0000 ns
$ g++ -std=c++20 -O3 opt_away.cpp -o opt_away_O3 && ./opt_away_O3
sum=499999999500000000  per-iter=0.0000 ns
```

Let's stop and look at those two rows of numbers.

At `-O0`, `per-iter=0.4567 ns` — that's the right order of magnitude for honestly running a billion additions: roughly one clock cycle and change per iteration. But at `-O2` and `-O3`, `per-iter=0.0000 ns`. A billion additions, and the measured time is zero (or more precisely, truncated to zero by integer division).

The weirder part: **the value of `sum` is correct**. All three optimization levels print the same `499999999500000000`, which is exactly `0+1+2+...+999999999`.

If you take that `-O2` `0.0000 ns`, compare it against another function, and declare "my sum function costs zero nanoseconds per iteration, we've hit the physical limit" — congratulations, the compiler just fooled you.

## What the compiler actually did

Let's break this into two moves.

The first move is **constant folding**. The input `N` to `heavy_sum` is a `const long`, and the compiler can see its value is `1000000000`. A loop that sums from `0` to `N-1` has a closed form `N*(N-1)/2` — middle-school math. The compiler evaluates that formula at compile time and bakes `499999999500000000` into the binary as an immediate. So `sum` is correct — it isn't computed at runtime at all, it's computed once at compile time.

The second move is **dead code elimination**. Since `sum` is now known at compile time, the loop has no reason to exist: it produces no new information, and it doesn't read or write any memory the compiler can't see. The compiler deletes the whole thing.

Stack the two moves together and here's the result: **your billion-iteration loop executed zero times.** The zero nanoseconds you measured is a real zero nanoseconds — because nothing actually ran.

We can confirm this in assembly. `objdump` the symbol tables of the two binaries:

```bash
$ objdump -t opt_away_O0 | grep heavy_sum
0000000000000000 l    F .text  00000000000000xx  heavy_sum(long)
$ objdump -t opt_away_O2 | grep heavy_sum
# (empty)
```

At `-O0`, `heavy_sum` is a standalone function symbol; at `-O2` the symbol **is gone entirely** — it got inlined into `main`, the loop body was folded away, and even the function itself disappeared. Now count the conditional jump instructions in each binary (`jne`/`je`/`jmp` and friends — the things a loop needs):

```bash
$ objdump -d -M intel opt_away_O0 | grep -cE 'j(ne|e|mp|b|a|ge|le)\b'
23
$ objdump -d -M intel opt_away_O2 | grep -cE 'j(ne|e|mp|b|a|ge|le)\b'
15
```

`-O2` has 8 fewer jumps than `-O0` — the loop's jumps are gone. This isn't some new trick in GCC 16.1.1; any modern compiler does this at `-O2`, and the behavior has been stable for over a decade.

## Why this is so easy to fall into

You might think: how often is `N` actually a compile-time constant in real code? I used to think that too, until I stepped on enough of these to realize the scenarios that trigger this class of optimization are far more common than you'd guess.

The most common one: **the loop's result is never actually consumed**. Modify `heavy_sum` like this, and assume the caller never uses the return value:

```cpp
long s = heavy_sum(N);
// s never appears again
```

This time `N` can be read in at runtime, but the compiler sees `s` is unused and still kills the whole loop — it may not be able to substitute the closed form, but deletion is a given. Your benchmark reports zero nanoseconds; you think the function is fast, but the function is simply gone.

Another common one: **the result is used by `printf` exactly once**. If the compiler is aggressive enough, it can move the entire computation to compile time and just slot the constant in at the print site. What you measure as "runtime" is really the time of `printf`.

And there's a subtler case: even if you store the result into a variable, as long as the compiler can prove that variable is only read later and never observed externally (say, it doesn't escape the function, it isn't written to global memory), it still has room to eliminate the computation. This is exactly why every serious benchmark framework asks you to do one thing — **"pin" the result somewhere the compiler can't touch.**

## Three ways to pin it down

These three techniques vary in strength and cost; let's go through them one by one.

### Technique 1: `volatile` forces a memory write

The oldest and most intuitive approach is to store the accumulated result into a `volatile` variable. `volatile` tells the compiler: every read and write of this variable must honestly happen — no eliding, no folding, no hoisting to compile time.

```cpp
volatile long sink = 0;
for (long i = 0; i < N; ++i) sink += i;
```

With `volatile`, the compiler doesn't dare delete the loop, because every `sink += i` is a real memory write (that's what `volatile` semantics demand). The loop survives.

But `volatile` has a cost: it forces a memory write every iteration, while `acc` could otherwise stay in a register the whole time. What you measure is "a loop with a memory write per iteration," not "the original loop." In a simple integer sum that cost may not show, but if the loop body is light to begin with, the memory access `volatile` introduces will visibly inflate the measurement — you jump from one extreme (zero nanoseconds) to the other (too high).

Also note: **`volatile` is not an atomic operation, and it doesn't establish a memory barrier**. It can't be used for synchronization in multithreaded scenarios, something the standard has stressed repeatedly since C++20. Its only role here is to block the compiler's optimization.

### Technique 2: `benchmark::DoNotOptimize`

Google Benchmark ships a tool built specifically for this problem, called `DoNotOptimize`. The implementation is clever: keep the compiler from eliminating your data, while adding as little overhead as possible.

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

Under the hood, `DoNotOptimize(a)` is an inline assembly snippet with a special constraint that "feeds" the address of `a` to the compiler, making it believe `a` might be observed externally — so it doesn't dare delete it. Unlike `volatile`, it doesn't force a memory write every time, and the overhead is much smaller.

Google Benchmark isn't installed on my machine, but we can replicate the principle. The snippet below uses inline assembly to mimic the core effect of `DoNotOptimize` — again, "trick the compiler into thinking this value is used externally":

```cpp
// Simplified DoNotOptimize: use asm to make the compiler think acc may be modified asynchronously
static inline void do_not_optimize(long& x) {
    asm volatile("" : "+r"(x) : :);
}

long acc = 0;
for (long i = 0; i < N; ++i) acc += i;
do_not_optimize(acc);
```

The `"+r"(x)` constraint tells the compiler: this assembly snippet (even though it's empty) reads and writes `x`, and `x` must live in a register. So the compiler doesn't dare eliminate the computations tied to `acc` — it "sees" `acc` being consumed by a black box it can't analyze. This is the standard idiom for both GCC and Clang, with minimal overhead, usually just a cycle or two of perturbation.

### Technique 3: `ClobberMemory` forces a flush

Sometimes pinning a single value isn't enough. If the loop writes to a chunk of memory, the compiler may merge, reorder, or even eliminate that whole block of writes, and pinning the final value won't stop it. In that case you flush the entire memory barrier:

```cpp
// In Google Benchmark:
benchmark::ClobberMemory();
// Equivalent hand-written version:
asm volatile("" : : : "memory");
```

The `"memory"` clobber tells the compiler: this assembly might read or write any memory. So the compiler has to commit every pending memory write before this point and can't optimize across it. It's heavier than `DoNotOptimize`, but it blocks more aggressive memory-related optimizations.

::: warning Don't be too quick to trust the numbers
Build a habit: after writing a benchmark, glance at the assembly first with `g++ -S` or Compiler Explorer to confirm the loop you intend to measure is still there and hasn't been folded into an immediate. It takes a few seconds and stops most "measured nothing" incidents. If the function symbol you're measuring has vanished from the assembly, or the loop body has been replaced by a couple of `mov` instructions loading a constant, the benchmark numbers are meaningless — fix the code first.
:::

## The boundary of this layer

Once you strip away the compiler, the first liar, you walk into a nastier layer. Even if the loop survives, the result is pinned, and the assembly checks out, the nanosecond number you get can still be lying — because the machine running it is constantly jittering: the OS can preempt you at any moment, CPU frequency drifts up and down, and what's sitting in cache is entirely out of your control. Together these are called noise, and they make your numbers bounce around.

More annoying is a category called bias. It isn't random bouncing; it systematically pulls your numbers in one direction, no matter how many times you run.

The two have completely different natures, and the ways to deal with them are completely different too. We take them apart one by one in the next piece.

[Next: Noise You Can Tame, Bias Is the Nightmare →](02-noise-vs-bias.md)
