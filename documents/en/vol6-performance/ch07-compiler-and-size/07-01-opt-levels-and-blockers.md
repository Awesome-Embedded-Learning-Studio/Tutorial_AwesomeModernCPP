---
chapter: 7
cpp_standard:
- 17
description: 'This article nails two things: what each -O level (-O0/-O1/-O2/-O3/-Os/-Oz) actually
  does (measured -O0→-O2 4× faster, and -O3 is sometimes slower than -O2), and the three classes
  of "optimization blockers" the compiler can''t cross (cross-translation-unit, which is LTO territory;
  pointer aliasing, unlocked with __restrict; and volatile, which force-disables optimization). The
  takeaway is that your job is largely "don''t get in the compiler''s way"'
difficulty: advanced
order: 1
platform: host
prerequisites:
- Inline, devirtualization, and the full compiler-optimization landscape
- Front-end optimization: code layout, PGO, and BOLT
reading_time_minutes: 6
related:
- LTO, ThinLTO, and the engineering rollout of PGO
- Size optimization: -Os, --gc-sections, and template bloat control
tags:
- host
- cpp-modern
- advanced
- 优化
- 工具链
title: '-O levels and optimization blockers: what the compiler can and can''t do'
translation:
  source: documents/vol6-performance/ch07-compiler-and-size/07-01-opt-levels-and-blockers.md
  source_hash: a17a25eef54896e1c0579d6ddd496ac78ca689e3441ab20649db3e961b5f6837
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 2900
---
# -O levels and optimization blockers: what the compiler can and can't do

## The compiler is your first performance teammate

When you write C++ for performance, one of the most important things is **figuring out what the compiler will do for you, and what it can't**. It will inline automatically, vectorize automatically, eliminate dead code automatically; ch04-02/04/05 already showed all of that. But it also has three classes of hard "can't optimize" limits (blockers) that need your cooperation. This article covers both sides.

## -O levels: what each level does

GCC/Clang's `-O` levels control optimization effort. One table (for the precise list, check the official docs):

| Level | Roughly what it does | When to use |
|---|---|---|
| `-O0` | No optimization, variables observable, assembly readable | Debugging (**performance numbers are meaningless**)|
| `-O1` | Basic optimization (constant folding, simple inlining)| Occasional debugging |
| `-O2` | Most optimizations: CSE, LICM, scheduling, auto-inlining (same TU), basic vectorization | **Release default sweet spot** |
| `-O3` | More aggressive: **loop vectorization, auto-unrolling, more aggressive inlining** | Numerical/SIMD wins; **occasionally hurts** |
| `-Os`/`-Oz` | Optimize for **size** (fast but smaller)| Embedded flash-constrained (ch07-04)|

`-O2` is the default release choice. It already covers most of the loop optimizations from ch04, same-translation-unit inlining, and basic scheduling. What `-O3` adds on top of `-O2` is mainly "more aggressive vectorization + unrolling + inlining".

### Measurement: -O0→-O2 4× faster, -O3 occasionally slower than -O2

We measure the same function (a loop with a `volatile` scale) at different -O levels:

```text
===== -O levels (same loop function) =====
  -O0: 18.6 ms   ← no optimization, 4× slower than -O2
  -O2:  4.9 ms   ← release sweet spot
  -O3:  7.4 ms   ← slower than -O2! (see below: not vectorization backfiring)
```

Two things, one at a time.

**First, `-O0` performance numbers are meaningless.** 18.6 ms vs 4.9 ms is a 4× gap. Never run performance tests under `-O0`; what you're measuring is "unoptimized code", not "the real performance of your code". Debug with `-O0`, but performance numbers start at `-O2`. ch01 hammered this point.

**Second, `-O3` isn't always faster than `-O2`.** Here `-O3` (7.4 ms) is slower than `-O2` (4.9 ms). But before jumping to conclusions, let's look at the assembly. `g++ -O2 -S` vs `g++ -O3 -S` for this `scale_add_alias` function: **neither vectorizes**. A `volatile` read can't be cached into a register, and `-fopt-info-vec-missed` explicitly reports `not vectorized: volatile type`. Note this has nothing to do with alias analysis; `volatile` doesn't participate in alias reasoning. The only difference between the two levels is register allocation and instruction scheduling. So 7.4 vs 4.9 **isn't "aggressive vectorization backfiring" (there was no vectorization to begin with), it's more likely measurement noise plus scheduling differences**: a single-shot measurement on WSL2, repeated runs flip this gap, and I've seen the reverse too.

That's an honest and important result. **"`-O3` is better than `-O2`" is a misunderstanding.** `-O3` pays off on numerical/SIMD-friendly code (real vectorization wins); on irregular, volatile-heavy, or branch-dense code it either doesn't vectorize (this example) or occasionally loses to scheduling. So release defaults to `-O2`, and you only enable `-O3` or `-ftree-vectorize` on **specific spots where you've confirmed `-O3` helps** (numerical hotspots).

## optimization blockers: three classes the compiler can't cross

Now the "what it can't do" part. Three blockers, each easy to trigger by accident.

### 1. Cross-translation-unit (LTO territory, ch07-02)

When the compiler compiles a `.cpp`, it can't see implementations in other `.cpp` files. So when `int helper(int)` is declared in a header but implemented in another `.cpp`, **the compiler doesn't dare inline it** (it doesn't know the implementation). That's the cross-TU blocker. The fix is **LTO (link-time optimization)**, which inlines across files at link time; ch07-02 measures LTO making a cross-TU call **3.9× faster**.

### 2. Pointer aliasing

C/C++ allows two pointers to refer to the same address (aliasing). The compiler **defaults to assuming two pointers might alias**, so it doesn't dare reorder memory reads and writes aggressively, because it's afraid "writing to `a[i]` might affect `b[i]`". This conservative assumption blocks a lot of loops that could otherwise be vectorized or reordered.

Measurement (`scale_add`: in the loop `a[i] += b[i] * *scale`, with `volatile` scale):

```text
  -O3 aliasing version (default):   7.4 ms   ← compiler won't assume a≠b
  -O3 __restrict version:           5.8 ms   ← you promise a/b/scale don't alias, compiler dares to optimize
```

> ⚠️ Note: the attribution of this teaching demo is actually **not clean**. In the accompanying code, the alias version's signature is `volatile int* scale` and the restrict version's signature is `int* __restrict scale` (scale is non-volatile), meaning the restrict version **also drops `volatile` from `scale`**. So the 7.4→5.8 gain isn't entirely alias analysis at work; part of it is that after removing `volatile`, `scale` can be cached/hoisted. A clean aliasing comparison would keep `scale`'s type identical in both versions and only toggle `__restrict` on `a`/`b`. This is a simplification of the teaching demo; in real-world comparisons, watch the "change only one variable" discipline, which echoes ch00-01.

`__restrict` (introduced in C99, a C++ extension but supported by both GCC and Clang) is your promise to the compiler that "this pointer doesn't alias with anything else":

```cpp
void f(int* __restrict a, int* __restrict b, int* __restrict scale, int n);
```

With that promise, the compiler dares to vectorize/reorder. The price: if you lie (the pointers actually do alias), **it's UB**. So use `__restrict` when you're sure there's no aliasing; a typical scenario is several independent arrays in numerical computation. Don't overuse it, but in numerical hotspots it's a steady win.

> Note: `__restrict` also works on references (`const int& __restrict`), but the semantics are slightly subtle; look it up before using. C++ has no `restrict` keyword (that's C's); use `__restrict`.

### 3. volatile

`volatile` forces the compiler to **truly read or write memory every time**, with no caching into registers and no optimizations applied. It was originally meant for **MMIO (memory-mapped IO), signal handling, and lock-free flags between threads**, scenarios where every access must genuinely hit memory (no caching). But `volatile` **is the antonym of optimization**: in the test above, `volatile scale` forced every loop iteration to do a real load, directly slowing the loop down.

In practice, **don't use `volatile` for "thread synchronization" or "performance"**. It guarantees neither atomicity nor memory ordering (that's `std::atomic`'s job, see vol5); it only guarantees "no optimization". In most performance code, `volatile` is misuse and should be replaced by `std::atomic` or dropped outright.

## References

- GCC manual, *Options That Control Optimization* (the list of passes enabled by `-O0`/`-O1`/`-O2`/`-O3`/`-Os`/`-Oz`)
- Agner Fog, *Optimizing Software in C++*, §8 *Different C++ compilers*. Local copy
- CSAPP Chapter 5 *Optimizing Program Performance* (the concept of optimization blockers, aliasing/memory-reference)
- Code for this article's measurements: `code/volumn_codes/vol6-performance/ch07/opt_levels_blockers.cpp`

The one-line summary: the compiler is your performance teammate, **don't get in its way**. Let it see implementations (LTO), trust your no-alias promise (`__restrict`), and don't use `volatile` to disable its optimizations; `-O2` is the release sweet spot, `-O3` belongs on numerical hotspots, and performance numbers from `-O0` are never to be trusted.
