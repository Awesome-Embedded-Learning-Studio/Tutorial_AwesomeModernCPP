---
chapter: 0
cpp_standard:
- 14
- 17
description: Starting from a lookup that is O(log n) on both sides, this article nails down the gulf between efficiency (algorithmic complexity) and performance (real behavior on hardware), lays down the volume's two iron rules and the Amdahl ceiling, and closes with a one-page platform/library selection guide.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- C++ container and algorithm basics (std::vector / std::set / std::lower_bound)
reading_time_minutes: 15
related:
- Why microbenchmarks lie
- 'Memory hierarchy and the latency ladder: why sequential access is 100x faster'
- 'The ASan tool family and memory safety: shadow memory, Heartbleed, and sanitizer selection'
tags:
- host
- cpp-modern
- intermediate
- 优化
- 实战
title: 'Performance Mindset: efficiency is not performance'
translation:
  source: documents/vol6-performance/ch00-performance-mindset/01-efficiency-vs-performance.md
  source_hash: 2648208d38c7044736713111eb0e5a82d426d83011b47b50d684f7ad5ec51b69
  translated_at: '2026-09-26T05:23:22+00:00'
  engine: anthropic
  token_count: 9500
---
# Performance Mindset: efficiency is not performance

## A fact that makes a lot of people uncomfortable

Let's start with a piece of code almost everyone has written: look up a number in a collection. The two most natural options are to put the data into a `std::set`, or into a sorted `std::vector` and binary-search it with `std::lower_bound`. A lookup on either side is $O(\log n)$ — identical complexity — and textbooks usually stop right there and tell you "pick either, they're about the same".

But if you actually go measure, you'll find things are not that simple. The table below is what I ran on my own machine (WSL2/Linux, 2,000,000 random-hit queries, median of 5 runs; the full code is in the "Code example" section of this chapter):

| Elements N | `vector` + binary (ns/query) | `set`.find (ns/query) | `set` / `vector` |
|---:|---:|---:|---:|
| 1,024 | 43 | 39 | 0.9× |
| 4,096 | 51 | 63 | 1.3× |
| 16,384 | 61 | 98 | 1.6× |
| 65,536 | 81 | 275 | 3.4× |
| 262,144 | 105 | 578 | **5.5×** |
| 1,048,576 | 185 | 1006 | **5.4×** |

Once N passes 60,000, `set` is 3 to 5 times slower than `vector`; and at very small N (1024), `set` is actually a touch faster. The two sides have exactly the same complexity, so how can the gap be this big? More awkward still: if your interview answer is "the two are equivalent", then in real code you will inexplicably hand over a service that is several times slower.

The answer is the proposition this entire volume keeps coming back to: **efficiency and performance are not the same thing.**

## Where exactly do efficiency and performance differ

Let's separate the two words first; every later discussion in this volume builds on this distinction:

- **efficiency** is the algorithmic-complexity axis: total work, critical-path length (span), big-O notation. This is a **mathematical property**, independent of any concrete hardware. When you say binary search is $O(\log n)$, that conclusion holds whether it runs on x86, on ARM, or on a paper-tape machine.
- **performance** is how your data **flows on real hardware**: which level of cache it hits, whether it triggers branch-prediction failures, whether it got vectorized, whether there is false sharing. This is an **engineering property**, measurable only on concrete hardware — change to a different CPU model and the conclusion may flip.

The problem is that big-O shoves every "hardware-dependent" effect (cache hit or miss, how accurate branch prediction is, the actual size of the constant factor) into one implicit constant $C$, and then pretends it doesn't matter. The mainstream implementations of `std::set` (libstdc++/libc++/MSVC) are red-black trees: every node is allocated individually and scattered all over the heap. During a lookup, each level you descend is a pointer dereference, and where the next node lives is unpredictable. This is a pattern called **pointer chasing**, which the hardware prefetcher cannot learn — so at large N, nearly every level is a cache miss. The elements of a `std::vector` sit in **contiguous storage**; the points a binary search jumps to at least land inside one compact stretch of memory (a cacheline, the minimum unit the CPU fetches from memory, usually 64 bytes, enough to hold 16 `int`s, and the whole array easily fits in L2/L3). Complexity analysis stuffs this entire gulf into the constant $C$, so one sentence — "they're both $O(\log n)$" — flattens a real 5× gap.

Denis Bakhvalov gives an even more counterintuitive example in Chapter 1 of *Performance Analysis and Tuning on Modern CPUs*: on **small** inputs, InsertionSort ($O(n^2)$) beats QuickSort ($O(n \log n)$) in actual measurements, because Big-O cannot characterize branch-prediction and cache effects — the difference between the two gets hidden inside that "unimportant" constant. The gist of what he writes there: complexity analysis cannot account for the branch-prediction and cache effects of the various algorithms, so it can only wrap them up in an implicit constant $C$ — and that constant can sometimes have a decisive influence on performance.

That is the central thesis of this volume: **don't just look at big-O — look at how the data flows on the hardware.** ch02 later expands the hardware details properly — cache hierarchy, cachelines, the latency ladder; but build the intuition right now: "low complexity = runs fast" is an illusion that will embarrass you in real code. Same $O(\log n)$: a 5× gap. Same $O(n)$: gaps of several tens of times are entirely possible (sequential traversal vs. random access).

## Iron rule 1: correct first, then fast

Chapter 5 of CS:APP nails this rule down in its very first sentence (we quote the original directly, because it cannot be said more precisely):

> The primary objective in writing a program must be to make it work correctly under all possible conditions. A program that runs fast but gives incorrect results serves no useful purpose.

It sounds like a platitude, but placed in the performance-optimization context it has one very specific corollary — one that gets violated over and over: **talking about performance numbers while carrying undefined behavior (UB) is like putting up a building on a site where the foundation was never properly laid.** UB does not sit still under `-O2`: the compiler optimizes aggressively on the basis that "this UB never happens", and the usual result is that the thing your benchmark measures has long since been disfigured — optimized into an empty shell — and you draw a pile of beautiful, completely wrong conclusions from it, then confidently carry them off to "optimize" production code.

That is why this volume puts the sanitizer toolchain (ASan / UBSan / MSan / TSan) in ch00 as the foundation, rather than treating it as "a debugging tool that wandered into the performance volume". Correctness without a sanitizer safety net means performance numbers are untrustworthy, full stop; numbers from concurrent code that has not passed TSan are equally untrustworthy. We will cover that chain in detail later — for now, just remember the conclusion: **correct first, then fast. This one is non-negotiable.**

## Iron rule 2: measure first, then optimize

Your intuition, at the microarchitecture level, is wrong much of the time. Big-direction intuition — "keep the data compact, do one fewer allocation" — is of course right; but the moment you get down to instruction-level details like "should this be branchless", "should I unroll this loop by hand", "are virtual functions actually slow", intuition stops keeping up with the hardware. This is not a knock on anyone; it is simply what the complexity of a modern CPU amounts to. Inside one contemporary CPU you have out-of-order execution, branch prediction, cache hierarchy, prefetchers, SIMD, micro-op fusion... your "feel" cannot keep up with all of that.

A few cases that later chapters of this volume will take apart with real measurements, one by one:

- You assume branchless is faster; it turns out modern branch predictors handle **predictable** branches almost for free, while your branchless rewrite stuffs in a few extra instructions and introduces a data dependency — slower;
- You assume hand-written loop unrolling speeds things up; it turns out the compiler already unrolled it at `-O2`, and your rewrite only makes the code harder to read and the icache worse;
- You assume virtual function calls are slow; it turns out the compiler long ago devirtualized them into direct calls based on the type hierarchy — maybe even inlined them.

None of this is hypothetical; it is the real content of ch04 / ch06 later in this volume. The conclusion fits in one sentence: **profile before you optimize.** The widest box on the flame graph is the place worth touching; change code by gut feeling and you are most likely optimizing the 5% while the real bottleneck lies fast asleep in the other 95%.

This iron rule leads directly into ch01: Benchmark Methodology. That is the **anchor chapter** of this volume — every performance article after it opens by referring back to the measurement discipline it teaches, just as vol5 runs TSan through concurrency correctness. If you only have time to read one article in this volume, read ch01.

## Amdahl: the ceiling on optimization

Before touching any code, there is one more hard rule to know: Amdahl's law. First put forward by Gene Amdahl in 1967, it states in one sentence where the ceiling on speedup lies:

$$S = \frac{1}{(1 - p) + \dfrac{p}{N}}$$

Here $p$ is the fraction of total time taken by "the part that can be accelerated", and $N$ is the speedup you apply to that part. That lonely $(1 - p)$ sitting in the denominator is the serial part: it eats none of your speedup and stays right where it is.

Plug in a few numbers and you will feel how brutal it is: even if you accelerate the part that takes 90% of the time by 1000× ($p=0.9, N=1000$), the overall speedup is only $1 / (0.1 + 0.0009) \approx 9.9\times$. Where is the ceiling? Let $N \to \infty$ (you accelerate that 90% without bound), $p/N \to 0$, $S \to 1/(1 - p) = 1/0.1 = 10\times$ — and that is where "locked under 10×" comes from: there is nothing you can do about the remaining 10% of serial code; no matter how mercilessly you squeeze the parallel part, the serial part does not budge.

The corollary matters enormously: **aim optimization at "the serial part with the big share".** This is the theoretical basis for profile-driven optimization: first measure where the biggest share sits, then change that — do not start just because something "looks slow". The full derivation of Amdahl's law, and its contrast with Gustafson's law (strong scaling with a fixed problem size vs. weak scaling where the problem size grows with the core count), is covered thoroughly in vol5 chapter 0; here we take only the "optimization ceiling" angle and do not reinvent the wheel.

## Don't ignore the biggest lever: platform and libraries

With the two iron rules and one ceiling covered, before diving into micro-optimization, step back and look at the macro picture. Agner Fog devotes an entire chapter of volume 1 of his optimization manual (ch2, *Choosing the optimal platform*) to "choosing the optimal platform", in this order: hardware platform → processor model → operating system → programming language → compiler → function libraries → UI framework. His stance is blunt: **these high-level decisions usually affect performance more than any micro-optimization you will ever fiddle with afterwards.**

We compress it into one page — a few key calls:

- **Kill the most wasteful tool/framework first.** Pick a heavyweight framework that heap-allocates everywhere and stacks virtual call upon virtual call, and no amount of cacheline or alignment tweaking afterwards will win it back. Agner cites Wirth's law, the half-joking adage that software gets slower faster than hardware gets faster. When both happen at once, the user experience treads water or even goes backwards.
- **Data-structure choice > micro-optimization.** Our opening `vector` vs. `set` example is the proof: swap in a cache-friendly container and a 5× gain lands in your pocket — far more substantial than hand-unrolling loops or fiddling with bit operations. Eat the "structural" wins first, then talk about instruction-level optimization.
- **For the embedded track: host and MCU differ by several orders of magnitude in resources.** A heap allocation that means nothing on host can be a fragmentation disaster on an STM32; an optimization validated on host usually holds in principle on an MCU, but the MCU has far less memory available, so overhead the host never even notices can be a hard performance or capacity bottleneck on the MCU. The code examples in this volume are host-first; embedded-leaning topics get called out explicitly and send you to the vol8 embedded domain for the full story.

Agner's full platform-selection checklist runs to a dozen-plus pages; we compress it into one here, because it is not this volume's technical main body — but it is often the most ignored, highest-yield cut of all. Plenty of people who can't make progress on performance optimization haven't failed to master the microarchitecture; they picked the wrong tool/library at the start.

## Code example: verify vector vs set yourself

Talk is cheap, so let's lay out the code behind the opening table. This is a **self-contained** benchmark that depends on no external library — plain standard C++17 compiles it (ch01 later introduces the industrial-strength Google Benchmark methodology properly; here we first use the plainest `std::chrono`, so that the very first article of the volume doesn't pile on concepts).

```cpp
// vector_vs_set.cpp — Both lookups are O(log n): how big a difference can cache effects make?
// Build: g++ -O2 -std=c++17 vector_vs_set.cpp -o vector_vs_set
#include <vector>
#include <set>
#include <algorithm>
#include <random>
#include <chrono>
#include <cstdio>
#include <cstdint>

using Clock = std::chrono::steady_clock;

static double median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

int main() {
    constexpr int queries = 2'000'000;   // 2M queries per N, to dilute single-query noise
    constexpr int trials  = 5;            // 5 runs, take the median; ch01 explains why later
    volatile std::int64_t global_sink = 0; // prevent the whole loop from being dead-code eliminated

    printf("%-10s %18s %18s %10s\n",
           "N", "vector(ns/q)", "set(ns/q)", "set/vector");
    for (int N : {1024, 4096, 16384, 65536, 262144, 1048576}) {
        std::mt19937_64 rng(12345);
        std::vector<int> keys(N);
        for (int i = 0; i < N; ++i) keys[i] = i * 2;          // even numbers, sparse
        std::vector<int> sorted = keys;
        std::sort(sorted.begin(), sorted.end());              // for binary search on the vector
        std::set<int> sset(keys.begin(), keys.end());         // set's red-black tree

        std::vector<int> toFind(queries);                     // all hits, removing the "not found" bias
        for (int i = 0; i < queries; ++i) toFind[i] = keys[rng() % N];

        std::vector<double> tv, ts;
        for (int t = 0; t < trials; ++t) {
            std::int64_t acc = 0;
            auto a = Clock::now();
            for (int q : toFind) {
                auto it = std::lower_bound(sorted.begin(), sorted.end(), q);
                acc += (it != sorted.end() && *it == q);
            }
            auto b = Clock::now();
            tv.push_back(std::chrono::duration<double, std::nano>(b - a).count() / queries);
            global_sink += acc;

            acc = 0;
            auto c = Clock::now();
            for (int q : toFind) {
                auto it = sset.find(q);
                acc += (it != sset.end());
            }
            auto d = Clock::now();
            ts.push_back(std::chrono::duration<double, std::nano>(d - c).count() / queries);
            global_sink += acc;
        }
        double mv = median(tv), ms = median(ts);
        printf("%-10d %18.1f %18.1f %10.1fx\n", N, mv, ms, ms / mv);
    }
    printf("\nglobal_sink=%lld (防死代码消除)\n", (long long)global_sink);
}
```

Let's pick out a few key points on why it is written this way, because every detail here is foreshadowing for the ch01 measurement methodology.

First, **the result must be consumed**. `acc` accumulates hit counts and is finally fed to `volatile global_sink`. Without this step, the compiler notices that "nobody uses what this loop computes" and deletes the whole loop outright (DCE) — you would be measuring an empty program. `volatile` forces a real memory write every time, blocking that optimization path.

Second, **every query hits**. The numbers in `toFind` are all keys that genuinely exist in the set. Without controlling this, "found" and "not found" take paths of different lengths and pollute the result. What we want to compare is pure lookup cost, not hit rate.

Third, **multiple runs, take the median**. Whether a single run gives 50ns or 80ns may just be that the CPU got scheduled away that round, or that Turbo frequency hadn't ramped up yet. Run 5 rounds and take the median to suppress such outliers. This one looks trivial, but it is the starting point of what ch01 expands across a whole chapter — "performance numbers are random variables": what you measure is not a number, it is a distribution.

Fourth, **use `steady_clock`**, not `clock()`. `clock()` measures process CPU time: under multithreading it counts other cores' busyness too, while blocking/sleeping doesn't count at all — it is simply not the "how long did this code take on the wall" that we want; `steady_clock` is a monotonically increasing, high-resolution clock that won't be rewound the way `system_clock` (that one is the true wall clock) is when the system changes the time — it is purpose-built for measuring "how far apart two events are", which is exactly what we need here. ch01 will cover "which clock to use" separately.

Run it and you will see the same trend as the table at the top of this article: at small N, `set` is on my machine actually slightly faster (around 0.9×); once N exceeds the cache, `set` gets dragged into a multi-fold slowdown by cache misses, while `vector` keeps the curve flat through contiguous memory.

That small-N "anomaly" deserves one more word, because it exposes the second thing big-O cannot hide: **branch prediction**. At N=1024, `set`'s entire red-black tree and the elements `vector`'s binary search jumps to are all still inside L1 — the cache blade hasn't even engaged yet. The real difference is in the branches: for random-hit queries, `lower_bound`'s comparison at each step is nearly 50/50, the branch predictor cannot guess it, and one mispredict costs a pipeline flush (a dozen-odd cycles); whereas each level of `set::find`, on top of that one key comparison, also has a few highly predictable operations (null-pointer checks, pointer updates), and when the cache isn't missing, that difference in instruction mix is enough for it to overtake. Bakhvalov covers this counterintuitive "binary search dragged down by branch prediction before the cache even kicks in" phenomenon in his book. Note that this "small-N set slightly faster" reproduces stably on my machine + libstdc++, but **it can flip if you change the compiler, the STL implementation, or the microarchitecture** — so the warning-box line below about "the row where `set` is slightly faster may disappear" refers to changing environments, not random jitter on the same machine. Both are $O(\log n)$: at small N, branch prediction and instruction mix decide who is faster; at large N, the cache decides. And that is exactly efficiency ≠ performance.

> ⚠️ **Do not treat this table as a universal conclusion.** The absolute numbers you get on a different CPU, a different compiler, or a different libc++ implementation will change; the row where `set` is slightly faster may disappear, or may become more pronounced. What we care about is the **trend** (contiguous memory vs. scattered nodes) and the **proposition** (same complexity, a several-fold gap), not any specific multiplier. Copying someone else's performance numbers straight into your own project is just another form of "guessing".

Performance questions always start from "how the data flows on hardware", never from "I feel like". As for how to turn "I feel like" into "I measured it" — that is ch01's job.

## References

- Bryant, R. E., O'Hallaron, D. R., *Computer Systems: A Programmer's Perspective*, Chapter 5, *Optimizing Program Performance* ("correct first, then fast", optimization layers, the Amdahl angle)
- Bakhvalov, D., *Performance Analysis and Tuning on Modern CPUs*, Chapter 1, *Introduction* (the limits of complexity analysis, the InsertionSort vs. QuickSort case)
- Fog, A., *Optimizing Software in C++*, Chapters 1–2 (Why software is often slow / Choosing the optimal platform)
- cppreference: [`std::lower_bound`](https://zh.cppreference.com/w/cpp/algorithm/lower_bound), [`std::set::find`](https://zh.cppreference.com/w/cpp/container/set/find)
- The original source of Amdahl's law: Gene Amdahl, *Validity of the single processor approach to achieving large scale computing capabilities*, AFIPS 1967
