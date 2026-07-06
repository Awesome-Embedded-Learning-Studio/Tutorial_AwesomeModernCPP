---
chapter: 0
cpp_standard:
- 14
- 17
description: Starting from a lookup that is both O(log n), this article nails down the gulf between efficiency (algorithmic complexity) and performance (real behavior on hardware), lays down the volume's two iron rules and the Amdahl ceiling, and includes a one-page platform/library selection checklist.
difficulty: intermediate
order: 1
platform: host
reading_time_minutes: 15
prerequisites:
- 'C++ containers and algorithms basics (std::vector / std::set / std::lower_bound)'
related:
- Why microbenchmarks lie
- Memory hierarchy and the latency ladder
- The ASan tool family and memory safety
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
  translated_at: '2026-07-05T12:41:00+00:00'
  engine: manual
  token_count: 3500
---
# Performance Mindset: efficiency is not performance

## A fact that makes a lot of people uncomfortable

Let's look at a piece of code almost everyone has written: looking up a number in a collection. The two most natural choices are: stuff the data into a `std::set`, or into a sorted `std::vector` and binary-search it with `std::lower_bound`. Both lookups are $O(\log n)$, the complexity is identical, and textbooks usually stop right there and tell you "pick whichever, they're about the same".

But if you actually measure, you'll find things aren't that simple. The table below is what I get on my own machine (WSL2/Linux, 2,000,000 random-hit queries, median of 5 runs; full code is in the "Code example" section later in this chapter):

| N | `vector` + binary (ns/q) | `set`.find (ns/q) | `set` / `vector` |
|---:|---:|---:|---:|
| 1,024 | 43 | 39 | 0.9× |
| 4,096 | 51 | 63 | 1.3× |
| 16,384 | 61 | 98 | 1.6× |
| 65,536 | 81 | 275 | 3.4× |
| 262,144 | 105 | 578 | **5.5×** |
| 1,048,576 | 185 | 1006 | **5.4×** |

Once N passes 60k, `set` is 3 to 5× slower than `vector`; and when N is small (1024), `set` is actually slightly faster. The complexity is exactly the same on both sides, so how come the gap is this big? Worse, if your interview answer is "they're equivalent", in real code you'll quietly ship a service that's several times slower.

The answer is the proposition this whole volume keeps returning to: **efficiency and performance are not the same thing.**

## efficiency vs performance, where exactly is the difference

Let's separate the two words first; everything that follows in this volume builds on this distinction:

- **efficiency** is the algorithmic-complexity axis: total work, critical-path length (span), big-O notation. This is a **mathematical property**, independent of any specific hardware. You say binary search is $O(\log n)$, and that holds whether it runs on x86, ARM, or a paper-tape machine.
- **performance** is about how your data **actually flows on real hardware**: which cache level it hits, whether it triggers branch mispredictions, whether it gets vectorized, whether there's false sharing. This is an **engineering property**, only measurable on concrete hardware, and a different CPU model can flip the conclusion.

The problem is that big-O shoves all the "hardware-related" effects (cache hit or miss, branch-prediction accuracy, the actual size of constant factors) into an implicit constant $C$, and then pretends it doesn't matter. The mainstream `std::set` implementations (libstdc++/libc++/MSVC) are red-black trees; every node is allocated separately, scattered across the heap. During lookup, every level down is a pointer dereference, and the next node it jumps to is at an unpredictable address. This is a pattern called **pointer chasing**, which the hardware prefetcher can't learn, so at large N almost every level is a cache miss. `std::vector`'s elements are stored **contiguously**; the points a binary search jumps to at least land in a compact stretch of memory (a cacheline, the minimum unit the CPU fetches from memory, usually 64 bytes, holds 16 `int`s, and the whole array easily fits in L2/L3). Complexity analysis stuffs this entire gulf into the constant $C$, so a single "it's all $O(\log n)$" flattens a real 5× gap.

Denis Bakhvalov gives an even more counterintuitive example in Chapter 1 of *Performance Analysis and Tuning on Modern CPUs*: on **small** inputs, InsertionSort ($O(n^2)$) actually beats QuickSort ($O(n \log n)$), because big-O can't capture branch-prediction and cache effects, and their difference gets hidden inside that "insignificant" constant. His point, paraphrased: complexity analysis can't account for the branch-prediction and cache effects of various algorithms, so it has to pack them into an implicit constant $C$, and that constant can sometimes be decisive for performance.

That's the overarching thesis of this volume: **don't just look at big-O; watch how data flows on hardware.** Later, ch02 will properly expand on cache hierarchy, cachelines, and latency ladders; but you need to build one intuition right now: "low complexity = runs fast" is an illusion that will embarrass you in real code. Same $O(\log n)$, a 5× difference; same $O(n)$, easily tens of times apart (sequential traversal vs random access).

## Iron rule 1: correct first, then fast

Chapter 5 of *Computer Systems: A Programmer's Perspective* nails this in its very first sentence (quoted directly, because it can't be said more precisely):

> The primary objective in writing a program must be to make it work correctly under all possible conditions. A program that runs fast but gives incorrect results serves no useful purpose.

Sounds like a truism, but in the context of performance optimization it has a very specific, repeatedly-violated corollary: **talking about performance numbers in the presence of undefined behavior (UB) is like building on a foundation that hasn't been poured yet.** UB doesn't sit still under `-O2`; the compiler reasons from "this UB never happens" and optimizes aggressively, and the usual result is that what your benchmark measures is no longer a real function call but an empty shell optimized beyond recognition, and you draw a pile of beautiful, entirely-wrong conclusions from it, then confidently take them off to "optimize" production code.

That's why this volume puts the sanitizer toolchain (ASan / UBSan / MSan / TSan) in ch00 as the foundation, rather than treating it as "a debugging tool that wandered into the performance volume." Without sanitizer-backed correctness, performance numbers are untrustworthy across the board; the same goes for any concurrency numbers that haven't been through TSan. We'll cover that chain in detail later; for now just remember the conclusion: **correct first, then fast; this is non-negotiable.**

## Iron rule 2: measure first, then optimize

Your intuition, at the microarchitecture level, is wrong a lot of the time. Big-direction intuitions like "compact the data, do fewer allocations" are of course right; but once you're asking "branchless or not", "unroll this loop or not", "are virtual calls actually slow"—instruction-level questions like that—intuition can't keep up with the hardware anymore. This isn't a dig at anyone; it's just what the complexity of a modern CPU amounts to. A contemporary CPU has out-of-order execution, branch prediction, cache hierarchy, prefetchers, SIMD, micro-op fusion… your "feeling" can't keep up with all that.

A few cases that later chapters will tear apart with real measurements:

- You think branchless is faster; turns out the modern branch predictor handles **predictable** branches almost for free, and your branchless rewrite just adds a few instructions and introduces a data dependency, making it slower.
- You think hand-unrolling the loop speeds things up; the compiler already unrolled it at `-O2`, and your re-do just makes the code harder to read and worse for icache.
- You think virtual calls are slow; the compiler already devirtualized them into direct calls based on the type hierarchy, and even inlined them.

None of this is hypothetical; it's the real content of ch04 / ch06. The conclusion fits in one sentence: **profile before you optimize.** The widest box on the flame graph is where you should be working; going by gut, odds are you're optimizing the 5% while the real bottleneck is sleeping in the other 95%.

This rule directly motivates ch01, Benchmark Methodology. That's the **anchor chapter** of this volume: every later performance article opens by referencing the measurement discipline it teaches, just like vol5 runs TSan through all of concurrency correctness. If you only have time to read one article in this volume, read ch01.

## Amdahl: the optimization ceiling

Before touching any code, there's one more hard rule to know: Amdahl's law. Gene Amdahl put it forward in 1967; in one sentence it tells you where the speedup ceiling is:

$$S = \frac{1}{(1 - p) + \dfrac{p}{N}}$$

where $p$ is the fraction of total time taken by "the part you can accelerate", and $N$ is the speedup you apply to that part. That lonely $(1 - p)$ in the denominator is the serial part: it doesn't eat your speedup, it just sits there unchanged.

Plug in some numbers and you'll feel how brutal it is: even if you accelerate the 90%-part by 1000× ($p=0.9, N=1000$), the total speedup is only $1 / (0.1 + 0.0009) \approx 9.9\times$. Where's the ceiling? Let $N \to \infty$ (you accelerate that 90% without limit), $p/N \to 0$, $S \to 1/(1 - p) = 1/0.1 = 10\times$, which is where "locked under 10×" comes from: that remaining 10% of serial code, you can't do anything about; no matter how hard you squeeze the parallel part, the serial part doesn't budge.

The corollary matters a lot: **optimize where the serial part is large.** That's the theoretical basis for profile-driven optimization—first measure where the biggest share is, then change that, don't just act on "this looks slow." The full derivation of Amdahl's law and its contrast with Gustafson's law (strong scaling with fixed problem size vs weak scaling where the problem grows with core count) is covered thoroughly in vol5 chapter 0; here we only take the "optimization ceiling" angle and don't reinvent it.

## Don't ignore the biggest lever: platform and libraries

Having covered two iron rules and one ceiling, before diving into micro-optimization, step back and look at the macro picture. Agner Fog devotes a whole chapter (ch2, *Choosing the optimal platform*) in volume 1 of his optimization manual to "choosing the optimal platform", in this order: hardware platform → processor model → operating system → programming language → compiler → libraries → UI framework. His stance is blunt: **these high-level decisions usually affect performance more than any micro-optimization you tweak afterwards.**

Compressed into one page, a few key calls:

- **Kill the most wasteful tool/framework first.** Pick a heavy framework that heap-allocates everywhere and layers virtual calls on virtual calls, and no amount of cacheline-tweaking or alignment-fiddling afterwards will recover it. Agner cites Wirth's law, the half-joking adage that software gets slower faster than hardware gets faster. When both happen at once, the user experience is treading water or even going backwards.
- **Data-structure choice > micro-optimization.** Our opening `vector` vs `set` example is the proof: swap in a cache-friendly container, pocket a 5× gain, far more practical than hand-unrolling loops or bit-twiddling. Eat the "structural" wins first, then talk about instruction-level optimization.
- **Embedded angle: host vs MCU differ by orders of magnitude in resources.** A heap allocation that doesn't matter on host can be a fragmentation disaster on STM32; an optimization validated on host usually holds in principle on MCU, but MCU has far less memory available, so overhead the host doesn't even notice can be a hard performance or capacity bottleneck on MCU. The code examples in this volume are host-focused; embedded-leaning topics get called out separately and point you to the vol8 embedded domain for the full story.

Agner's full platform-selection checklist runs over a dozen pages; we compress it to one because it isn't this volume's technical subject, but it's often the most ignored, highest-yield cut. A lot of people get stuck on performance optimization not because they haven't mastered the microarchitecture, but because they picked the wrong tool/library at the start.

## Code example: verify vector vs set yourself

Talk is cheap; let's lay out the code for the table at the start. This is a **self-contained** benchmark that depends on no external library; plain C++17 compiles it (ch01 will properly introduce the industrial-strength Google Benchmark methodology later; here we use the plainest `std::chrono` first, to avoid piling on concepts in the very first article of the volume).

```cpp
// vector_vs_set.cpp — Both are O(log n) lookups; how big a difference can cache effects make?
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
    constexpr int queries = 2'000'000;   // 2M queries per N, to dilute single-shot noise
    constexpr int trials  = 5;            // 5 trials, take the median; ch01 explains why
    volatile std::int64_t global_sink = 0; // prevent the whole loop from being dead-code eliminated

    printf("%-10s %18s %18s %10s\n",
           "N", "vector(ns/q)", "set(ns/q)", "set/vector");
    for (int N : {1024, 4096, 16384, 65536, 262144, 1048576}) {
        std::mt19937_64 rng(12345);
        std::vector<int> keys(N);
        for (int i = 0; i < N; ++i) keys[i] = i * 2;          // even, sparse
        std::vector<int> sorted = keys;
        std::sort(sorted.begin(), sorted.end());              // for vector binary search
        std::set<int> sset(keys.begin(), keys.end());         // set red-black tree

        std::vector<int> toFind(queries);                     // all hits, removes "not found" bias
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
    printf("\nglobal_sink=%lld (anti-dead-code-elimination)\n", (long long)global_sink);
}
```

Let's walk through a few key points on why it's written this way, because every detail here is foreshadowing for the ch01 measurement methodology.

First, **the result must be consumed.** `acc` accumulates hit counts and is fed into `volatile global_sink` at the end. Without this step, the compiler notices "nobody uses what this loop computes" and deletes the whole loop (DCE); you'd be measuring an empty program. `volatile` forces a real memory write every time, blocking that optimization path.

Second, **all queries hit.** The numbers in `toFind` are all keys actually present in the set. Without controlling this, "found" and "not found" take paths of different lengths and pollute the result. What we want to compare is pure lookup cost, not hit rate.

Third, **multiple trials, take the median.** A single run that gives 50ns or 80ns might just be that the CPU got scheduled away this round, or Turbo frequency didn't ramp up. Run 5 rounds and take the median to suppress outliers. This looks trivial, but it's the starting point of what ch01 will expand into "performance numbers are random variables": what you measure isn't a number, it's a distribution.

Fourth, **use `steady_clock`, not `clock()`.** `clock()` measures process CPU time, which under multithreading also counts other cores' busyness, and doesn't count blocking/sleep—completely not the "how long did this code take on the wall" that we want; `steady_clock` is a monotonic, high-resolution clock that won't get rewound like `system_clock` (that's the actual wall clock) when the system time changes; it's purpose-built for measuring "how far apart are two events", which is exactly what we need here. ch01 will cover "which clock to use" separately.

When you run it, you'll see the same trend as the table at the top of this article: at small N, `set` is actually slightly faster on my machine (around 0.9×); once N exceeds the cache, `set` gets dragged into a multi-fold slowdown by cache misses, while `vector` keeps the curve flat through contiguous memory.

That small-N "anomaly" deserves another word, because it exposes the second thing big-O can't hide: **branch prediction.** At N=1024, `set`'s entire red-black tree and the elements `vector`'s binary search will jump to are all still in L1; the cache blade hasn't even engaged yet. The real difference is in branches: `lower_bound`'s comparison at each step is almost 50/50 for random-hit queries, and the branch predictor can't guess it; one mispredict costs a pipeline flush (a dozen-plus cycles). `set::find` at each level, on top of that one key comparison, also has a few highly-predictable operations (null-pointer checks, pointer updates); when the cache isn't missing, that instruction-mix difference is enough to let it overtake. Bakhvalov specifically covers this "binary search dragged down by branch prediction before the cache even kicks in" counterintuitive phenomenon in his book. Note that this "small-N set slightly faster" is stably reproducible on my machine + libstdc++, but **it can flip if you change the compiler, the STL implementation, or the microarchitecture**, so the warning-box line below about "the row where `set` is slightly faster may disappear" refers to changing the environment, not random jitter on the same machine. Both are $O(\log n)$; at small N, branch prediction and instruction mix decide who's faster; at large N, the cache decides; and that is exactly efficiency ≠ performance.

> ⚠️ **Don't treat this table as a universal conclusion.** Absolute numbers will change when you run on a different CPU, compiler, or libc++ implementation; the row where `set` is slightly faster may disappear, or become more pronounced. What we care about is the **trend** (contiguous memory vs scattered nodes) and the **proposition** (same complexity, several-fold difference), not any specific multiplier. Copying someone else's performance numbers straight into your own project is just another form of "guessing".

Performance problems always start from "how data flows on hardware", never from "I feel like". As for how to turn "I feel like" into "I measured it", that's ch01's job.

## References

- Bryant, R. E., O'Hallaron, D. R. *Computer Systems: A Programmer's Perspective*, Chapter 5 *Optimizing Program Performance* (correct-first-then-fast, optimization layers, the Amdahl angle).
- Bakhvalov, D. *Performance Analysis and Tuning on Modern CPUs*, Chapter 1 *Introduction* (limits of complexity analysis, the InsertionSort vs QuickSort case).
- Fog, A. *Optimizing Software in C++*, Chapters 1–2 (Why software is often slow / Choosing the optimal platform).
- cppreference: [`std::lower_bound`](https://en.cppreference.com/w/cpp/algorithm/lower_bound), [`std::set::find`](https://en.cppreference.com/w/cpp/container/set/find).
- Original Amdahl's-law source: Gene Amdahl, *Validity of the single processor approach to achieving large scale computing capabilities*, AFIPS 1967.
