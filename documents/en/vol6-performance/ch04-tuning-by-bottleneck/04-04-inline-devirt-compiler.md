---
chapter: 4
cpp_standard:
- 17
description: 'This article covers the real power of inline (not "saving function-call overhead" but "letting the compiler optimize across function boundaries"), how devirtualization turns virtual functions back into inlineable direct calls, and a landscape view of "what -O2/-O3 compilers actually do". The point: do not fixate on the inline keyword — modern compilers decide by their own cost model, and your job is to "stay out of their way"'
difficulty: advanced
order: 4
platform: host
prerequisites:
- 'Loop and compute optimization: code motion, eliminating memory references, and multiple accumulators'
- 'Pipeline, ILP, and branch prediction: same data, several-fold differences depending on how you execute it'
reading_time_minutes: 7
related:
- 'Virtual functions and devirtualization: don''t rush to rewrite virtuals as templates'
- 'Compiler optimization boundaries: -O levels, optimization blockers, and LTO'
tags:
- host
- cpp-modern
- advanced
- 优化
title: "inline, devirtualization, and the compiler optimization landscape"
translation:
  source: documents/vol6-performance/ch04-tuning-by-bottleneck/04-04-inline-devirt-compiler.md
  source_hash: 40f39088a8de03dc61bbb03f4047761d0c0078bc7552b57ee5f5e8f4f150695d
  translated_at: '2026-09-26T06:22:33+00:00'
  engine: anthropic
  token_count: 2500
---
# inline, devirtualization, and the compiler optimization landscape

## inline's real power: not saving the call, but opening up optimization space

Many people's understanding of `inline` stops at "it saves the push/pop overhead of a function call". That is true, but it only scratches the surface — on a modern CPU an ordinary function call costs just a few nanoseconds, and the call overhead inline saves is usually not the bulk of anything. **inline's real power is that it moves the callee's code into the call site, letting the compiler optimize across function boundaries.**

For example:

```cpp
int square(int x) { return x * x; }
int f(int n) { return square(n) + square(n); }
```

Without inlining, `f` calls `square` twice: two function calls, two computations of `n*n`. **After inlining**, what the compiler sees is `return n*n + n*n`, so:

1. **Common subexpression elimination (CSE)**: `n*n` computed twice → merged into `int t = n*n; return t + t;`.
2. **Strength reduction**: `t + t` → `t << 1` (trading an addition for a shift — pointless here, but the compiler evaluates it).
3. **Constant propagation** (if `n` is known at compile time): the whole of `f(5)` folds into the constant `50`.

These optimizations **are impossible without inlining**, because the compiler cannot see `square`'s implementation (it lives in another translation unit, or the compiler dares not assume anything across the call site). Once inlining happens, the function boundary disappears and the optimization space opens up. That is inline's core value.

## The inline keyword is only a hint

C++'s `inline` keyword is a **hint**, not a command. Modern compilers (GCC/Clang) decide whether to actually inline based on their own **cost model**: a function that is too large does not get inlined (code bloat causing icache misses costs more than it saves), recursion cannot be fully inlined, and virtual functions cannot be inlined (who gets called is only known at run time). Conversely, **a function with no `inline` marker still gets inlined automatically as long as the compiler can see its implementation** (especially under `-O2`/`-O3` + LTO).

So your job is not to sprinkle `inline` everywhere, but:

- **Put hot functions' implementations in headers** (or enable LTO) so the compiler **can see** the implementation — that is the precondition for inlining.
- **Don't block it with `noinline` casually** (it is sometimes used for debugging or code-size control — just remember to remove it).
- When you truly must force inlining, use `[[gnu::always_inline]]` (GCC/Clang) or `__attribute__((always_inline))`, which C++ has never standardized — but use them sparingly: the compiler's cost model is usually more accurate than yours.
- C++20's `[[gnu::flatten]]` can force-inline the entire call chain — for the occasional extremely sensitive hotspot.

**A common mistake**: treating `inline` as a "make functions faster" silver bullet and pasting it everywhere. If the implementation is not in a header (cross-translation-unit), the `inline` keyword does nothing — the compiler still cannot see the implementation. LTO (ch07-02) solves that.

## Devirtualization: turning virtual functions back into inlineable calls

Virtual functions are inline's natural enemy: which function gets called is only known after a run-time vtable lookup, so the compiler dares not inline. But **if the compiler can prove that "the run-time type is fixed"**, it turns the virtual call into a direct call — **devirtualization** — and then it can inline. We measured four calling styles (on my machine, average ns per call):

```text
===== 虚函数与去虚拟化 =====
  虚函数(指针,运行时多态):   0.55 ns  ← 查 vtable + 间接跳转,阻碍内联
  final 类(编译器去虚化):    0.54 ns
  直接对象(非指针,常去虚化): 0.23 ns
  CRTP(静态多态,无虚表):    0.22 ns  ← 可内联
  虚函数/CRTP = 2.5x
```

Two things to read out of it:

**1. A virtual call through a pointer (0.55 ns) is 2.5x slower than the inlinable CRTP/direct-object variants (0.22-0.23 ns).** Looking at the assembly, the virtual call in this example was in fact **speculatively devirtualized** by GCC (at run time it compares the vptr against a compile-time-known target address, and on a match takes an inlined fast path), and the function body `x*3+1` has been inlined. So the extra overhead in the 0.55 ns tier is not "vtable lookup + indirect jump", but the per-iteration **type guard** inside the loop (load the vptr, compare against the known address, conditional branch); the direct-object/CRTP tier does not even have the guard, which is why it is faster. **CRTP (static polymorphism, implemented with templates)** pushes polymorphism to compile time — no vtable, inlinable, the fastest of all.

**2. `final` did not make the loop faster in this example (0.54 ≈ 0.55).** I marked `struct DerivedF final`, expecting the compiler to "know there are no further-derived classes" and thus dare to devirtualize — but 0.54 ≈ 0.55 is **not** "final failed to devirtualize"! On my machine, `-fopt-info-all` shows that the virtual calls on both the final class (`DerivedF`) and the plain derived class (`Derived`) were **speculatively devirtualized** by GCC (the same mechanism as point 1 above). The real reason for 0.54 ≈ 0.55 is that "the per-iteration type-guard cost of speculative devirtualization is on par with one virtual call, so nothing was saved". This is an **honest and important result**: **don't assume that marking `final` automatically makes things fast — whether devirtualization actually saves overhead depends on whether the assembly truly turned into a direct `call`** (`-S`: a virtual call is an indirect `call [vtable+offset]` jump; full devirtualization is a direct `call func`).

Corollary: **don't preemptively rewrite virtual functions into CRTP/templates for a "maybe faster"**. Measure first — the compiler may already have devirtualized (especially when you call through a direct object); only when a virtual function genuinely measures as the bottleneck should you consider CRTP or `final`. ch06-01 expands this into the full "cost of virtual functions" discussion.

## The compiler optimization landscape: what -O2/-O3 actually do

Now put inline and devirt into the bigger picture. One table for what each `-O` level of GCC/Clang roughly does (check the official docs for the precise list):

| Level | Roughly what it does | When to use |
|---|---|---|
| `-O0` | No optimization, variables observable | Debugging (**performance numbers are meaningless**) |
| `-O1` | Basic optimizations (constant folding, simple inlining) | — |
| `-O2` | Most optimizations: CSE, LICM, register allocation, scheduling, **auto-inlining (same TU)**, basic vectorization | **The default release sweet spot** |
| `-O3` | More aggressive: **loop vectorization, auto-unrolling, more aggressive inlining** | Numerical/SIMD code benefits; occasionally backfires (icache) |
| `-Os`/`-Oz` | Optimizes for **size** | Embedded, flash-constrained |

Key takeaways:

- **`-O2` is the default release sweet spot.** It already covers most of the loop optimizations from 04-02, same-translation-unit inlining, and basic scheduling.
- **What `-O3` adds over `-O2` is mainly "more aggressive vectorization + unrolling + inlining".** It helps numerical/SIMD-friendly code; on branch-heavy, irregular code it **can actually be slower** (code bloat → icache misses).
- **Cross-translation-unit inlining and optimization are beyond both `-O2` and `-O3`** — that takes LTO (ch07-02). This is why enabling LTO is recommended for large projects' release builds.

## Stay out of the compiler's way: optimization blockers

inline and every optimization in this section presuppose that "the compiler can see the code, and dares to optimize". A few situations **block the compiler** (optimization blockers, covered in detail in ch07-01):

- **Cross-translation-unit**: cannot see the implementation → dares not inline. Fix: put implementations in headers / enable LTO.
- **Pointer aliasing**: the compiler does not know whether two pointers hit the same address, so it dares not aggressively reorder memory reads and writes. Fix: declare no aliasing with `__restrict` (introduced in C99; a C++ extension, but supported by both GCC and Clang).
- **`volatile`**: forces a real memory read/write every time, which amounts to disabling optimization. Use it only for MMIO/signals/lock-free flags — **never for performance**.

All three come down to "you wrote something else that got in the compiler's way". The core spirit of this article: **the main work around inlining and compiler optimization is not "actively doing things", but "stay out of the way + let the compiler see the implementation"**. Active hand-written intervention (forced inlining, CRTP) comes only after a bottleneck has been measured.

Compress this article into a few sentences: inline's real value is opening up optimization across function boundaries (CSE, constant propagation, strength reduction), not saving call overhead; the `inline` keyword is only a hint — letting the compiler see the implementation (headers / LTO) is the precondition for inlining; devirtualization can turn virtual functions back into inlineable calls, but it is not "mark `final` and the overhead vanishes" — in this example both the final class and the plain derived class were speculatively devirtualized by GCC (provable with `-fopt-info-all`), and 0.54 ≈ 0.55 because the per-iteration guard cost of devirtualization was not saved; calling through a direct object is more easily fully devirtualized, and CRTP is static polymorphism, the fastest (virtual/CRTP = 2.5x); `-O2` is the release sweet spot, and `-O3` adds aggressive vectorization/unrolling — friendly to numerical code but occasionally backfiring; optimization blockers (cross-TU, aliasing, volatile) are you blocking the compiler's way, and the next article, ch04-05, will run into the "does the compiler dare" question once more, in the SIMD context.

## References

- Piotr Padlewski, *C++ devirtualization in clang* (CppCon 2015 Lightning) — the devirtualization mechanism (reused in vol10)
- GCC/Clang documentation on `-O` levels, `-finline-*`, `__restrict`, and the `always_inline` attribute
- ch06-01 Virtual functions and devirtualization (this volume; the full discussion of the cost of virtual functions)
- This article's measured code: `code/volumn_codes/vol6-performance/ch04/virtual_devirt.cpp`
