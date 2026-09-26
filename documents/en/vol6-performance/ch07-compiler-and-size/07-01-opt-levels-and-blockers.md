---
chapter: 7
cpp_standard:
- 17
description: "This article makes two things clear: what the compiler actually does at
  each of -O0/-O1/-O2/-O3/-Os/-Oz (measured: -O0 to -O2 is a 4x speedup, and -O3 is
  sometimes slower than -O2), and the three classes of 'blockers' the compiler cannot
  optimize through - cross-translation-unit calls (LTO territory), pointer aliasing
  (unlocked with __restrict), and volatile (forcibly disables optimization). The core
  message: much of your job is simply 'stay out of the compiler's way'"
difficulty: advanced
order: 1
platform: host
prerequisites:
- inline, devirtualization, and the compiler optimization landscape
- 'Frontend optimization: code layout, PGO, and BOLT'
reading_time_minutes: 6
related:
- 'LTO, ThinLTO, and the engineering rollout of PGO'
- 'Binary size optimization: -Os, --gc-sections, and template bloat control'
tags:
- host
- cpp-modern
- advanced
- 优化
- 工具链
title: "-O levels and optimization blockers: what the compiler can and can't do"
translation:
  source: documents/vol6-performance/ch07-compiler-and-size/07-01-opt-levels-and-blockers.md
  source_hash: a17a25eef54896e1c0579d6ddd496ac78ca689e3441ab20649db3e961b5f6837
  translated_at: '2026-09-26T07:06:21+00:00'
  engine: anthropic
  token_count: 3700
---
# -O levels and optimization blockers: what the compiler can and can't do

## The compiler is your first performance teammate

One of the most important things when writing performance C++ is **understanding what the compiler will do for you, and what it can't**. It auto-inlines, auto-vectorizes, and eliminates dead code on its own — we saw all of this in ch04-02/04/05. But it also has three classes of hard limits, "optimization blockers", where it needs your cooperation. This article covers both sides.

## -O levels: what each level does

GCC/Clang's `-O` levels control how hard the optimizer works. One table (check the official docs for the precise pass lists):

| Level | Roughly what it does | When to use it |
|---|---|---|
| `-O0` | No optimization; variables observable, assembly readable | Debugging (**performance numbers are meaningless**) |
| `-O1` | Basic optimizations (constant folding, simple inlining) | Occasional debugging |
| `-O2` | Most optimizations: CSE, LICM, scheduling, auto-inlining (same TU), basic vectorization | **The default release sweet spot** |
| `-O3` | More aggressive: **loop vectorization, auto-unrolling, more aggressive inlining** | Pays off for numeric/SIMD code; **occasionally backfires** |
| `-Os`/`-Oz` | Optimizes for **size** (still fast, just smaller) | Embedded with constrained flash (ch07-04) |

`-O2` is the default choice for release builds: it already covers most of the loop optimizations from ch04, same-translation-unit inlining, and basic scheduling. What `-O3` adds over `-O2` is mainly "more aggressive vectorization + unrolling + inlining".

### Measured: -O0→-O2 is 4x faster, and -O3 is occasionally slower than -O2

We benchmark the same function (a loop with a `volatile` scale) at different -O levels:

```text
===== -O levels (same loop function) =====
  -O0: 18.6 ms   ← no optimization, 4x slower than -O2
  -O2:  4.9 ms   ← release sweet spot
  -O3:  7.4 ms   ← slower than -O2! (see below: not vectorization backfiring)
```

Two things here, one sentence each.

**First, `-O0` performance numbers are meaningless.** 18.6 ms vs 4.9 ms — a 4x gap. Never benchmark with `-O0`: what you measured is "unoptimized code", not "how fast your code actually is". Use `-O0` for debugging; performance numbers always start at `-O2`. ch01 hammered on this point repeatedly.

**Second, `-O3` isn't always faster than `-O2`.** Here `-O3` (7.4 ms) is slower than `-O2` (4.9 ms). But before jumping to conclusions, let's look at the assembly. Comparing `g++ -O2 -S` vs `g++ -O3 -S` for this `scale_add_alias` function, **neither one was vectorized**: the `volatile` read can't be cached into a register, and `-fopt-info-vec-missed` explicitly reports `not vectorized: volatile type`. Note this has nothing to do with alias analysis — `volatile` plays no part in aliasing decisions. The two levels differ only slightly in register allocation and instruction scheduling. So 7.4 vs 4.9 is **not "aggressive vectorization backfiring" (there was no vectorization at all); it's more likely measurement noise stacked on top of scheduling differences**: a single measurement plus WSL2 noise. Repeat the run and this gap shifts; we've seen it flip the other way too.

This is an honest and important result. **"`-O3` beats `-O2`" is a misconception**: `-O3` helps on numeric/SIMD-friendly code (real vectorization collects the win), but on irregular, volatile-heavy, branch-dense code it either fails to vectorize (as in this case) or occasionally takes a scheduling hit. So default your release builds to `-O2`, and turn on `-O3` or `-ftree-vectorize` only in the spots where you've **confirmed `-O3` actually gains** (numeric hot spots).

## optimization blockers: three classes the compiler can't cross

That covers what the compiler can do; now for what it can't. Three classes of blockers — and every one of them you can trigger without realizing it.

### 1. Cross-translation-unit (LTO's territory, ch07-02)

When the compiler compiles one `.cpp`, it can't see the implementations inside any other `.cpp`. So when `int helper(int)` is declared in a header but implemented in another `.cpp`, **the compiler doesn't dare inline it** (it doesn't know the implementation). That's the cross-TU blocker. The fix is **LTO (link-time optimization)**: cross-file inlining at link time. In ch07-02 we measure LTO making a cross-TU call **3.9x faster**.

### 2. Pointer aliasing

C/C++ allows two pointers to point at the same address (aliasing). The compiler **assumes by default that two pointers may alias**, so it doesn't dare aggressively reorder memory reads and writes — it's afraid that "writing `a[i]` just clobbered `b[i]`". This conservative assumption is what stalls many loops that could otherwise be vectorized or reordered.

Measured (`scale_add`: a loop computing `a[i] += b[i] * *scale`, with `volatile` scale):

```text
  -O3 aliasing version (default): 7.4 ms   ← compiler won't assume a≠b
  -O3 __restrict version:         5.8 ms   ← you promise a/b/scale don't alias, compiler dares to optimize
```

> ⚠️ Note: the attribution in this teaching demo is actually **not clean**. In the accompanying code, the alias version's signature takes `volatile int* scale` while the restrict version's takes `int* __restrict scale` (scale is not volatile) — meaning the restrict version **also drops the `volatile` on `scale`**. So 7.4→5.8 isn't entirely the work of alias analysis; part of it is that once `volatile` is gone, `scale` can be cached/hoisted. A clean aliasing comparison would keep `scale`'s type identical across both versions and vary only whether `a`/`b` carry `__restrict`. That's a simplification of the teaching demo — in real-world comparisons, mind the "change only one variable" discipline from ch00-01.

`__restrict` (introduced in C99; an extension in C++, but supported by both GCC and Clang) is your promise to the compiler that "this pointer doesn't alias anyone else":

```cpp
void f(int* __restrict a, int* __restrict b, int* __restrict scale, int n);
```

Once you've promised, the compiler dares to vectorize and reorder. The cost: if you lied (the pointers actually do alias), **it's UB**. So use `__restrict` when "you're certain there's no aliasing" holds — the classic scenario is several independent arrays in numeric computing. Don't spray it around, but in numeric hot spots it's a reliable win.

> Note: `__restrict` also works on references (`const int& __restrict`), but the semantics are a bit subtle — read up before using it. C++ has no `restrict` keyword (that's C's); use `__restrict`.

### 3. volatile

`volatile` forces the compiler to **genuinely read and write memory on every single access** — no caching into registers, no optimization of any kind. It exists for **MMIO (memory-mapped I/O), signal handling, and lock-free flags shared between threads** — scenarios where every access must truly reach memory (no caching allowed). But `volatile` **is the antonym of optimization**: in the test above, `volatile scale` forced a real load on every loop iteration, directly dragging the loop down.

In practice, **don't use `volatile` for "thread synchronization" or "performance"**. It guarantees neither atomicity nor memory ordering (that's `std::atomic`'s job, covered in vol5) — it only guarantees "no optimization". In most performance code, `volatile` is misuse; it should be replaced with `std::atomic` or dropped altogether.

## References

- The GCC manual, *Options That Control Optimization* (the list of passes each of `-O0`/`-O1`/`-O2`/`-O3`/`-Os`/`-Oz` enables)
- Agner Fog, *Optimizing software in C++*, §8 *Different C++ compilers* (local copy available)
- CSAPP chapter 5, *Optimizing Program Performance* (the conceptual definition of optimization blockers — the aliasing/memory-referencing story)
- This article's benchmark code: `code/volumn_codes/vol6-performance/ch07/opt_levels_blockers.cpp`

To wrap up in one sentence: the compiler is your performance teammate — **stay out of its way**. Let it see implementations (LTO), let it trust your no-alias promises (`__restrict`), and don't use `volatile` to ban its optimizations; `-O2` is the release sweet spot, `-O3` is reserved for numeric hot spots, and a performance number from `-O0` is never to be trusted.
