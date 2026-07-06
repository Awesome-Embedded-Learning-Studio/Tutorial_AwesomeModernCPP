---
chapter: 4
cpp_standard:
- 17
description: 'This article covers the real power of inline (not "saving the call overhead" but "letting the compiler optimize across function boundaries"), how devirtualization turns a virtual call back into an inlineable direct call, and a landscape view of "what -O2/-O3 actually do." The point: don''t fixate on the inline keyword — modern compilers decide based on their own cost model, and your job is "don''t block the path"'
difficulty: advanced
order: 4
platform: host
prerequisites:
- Loop and compute optimization — code motion, unrolling, and multiple accumulators
- Pipeline, ILP, and branch prediction
reading_time_minutes: 7
related:
- Virtual functions and devirtualization — don't rush to turn virtuals into templates
- Compiler optimization boundaries — -O levels, optimization blockers, and LTO
tags:
- host
- cpp-modern
- advanced
- 优化
title: "inline, devirtualization, and the compiler optimization landscape"
translation:
  source: documents/vol6-performance/ch04-tuning-by-bottleneck/04-04-inline-devirt-compiler.md
  source_hash: 40f39088a8de03dc61bbb03f4047761d0c0078bc7552b57ee5f5e8f4f150695d
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3000
---
# inline, devirtualization, and the compiler optimization landscape

## inline's real power: not saving calls, but opening up optimization space

A lot of people's understanding of `inline` stops at "saves the function-call push/pop overhead." That's true but only on the surface — on a modern CPU a normal function call is a few nanoseconds, and the call overhead inline saves usually isn't the bulk. **inline's real power is that it "moves the callee's code into" the call site, letting the compiler optimize across function boundaries.**

For example:

```cpp
int square(int x) { return x * x; }
int f(int n) { return square(n) + square(n); }
```

Without inlining, `f` calls `square` twice — two function calls, two `n*n`. **After inlining**, the compiler sees `return n*n + n*n`, and so:

1. **Common subexpression elimination (CSE)**: `n*n` is computed twice → merged into `int t = n*n; return t + t;`.
2. **Strength reduction**: `t + t` → `t << 1` (add → shift; pointless here, but the compiler evaluates it).
3. **Constant propagation** (if `n` is known at compile time): the whole `f(5)` folds to the constant `50`.

These optimizations **can't happen without inlining**, because the compiler can't see `square`'s implementation (it's in another translation unit, or the compiler doesn't dare assume across the call site). Once inlining happens, the function boundary dissolves and the optimization space opens up. That's the core value of inline.

## The inline keyword is only a "hint"

C++'s `inline` keyword is a **hint**, not a command. Modern compilers (GCC/Clang) decide whether to actually inline based on their own **cost model**: too-big functions aren't inlined (code bloat, icache misses cost more than they gain), recursion can't be fully inlined, virtual functions can't be inlined (you don't know who's called until runtime). Conversely, **functions not marked `inline` still get auto-inlined as long as the compiler can see the implementation** (especially under `-O2`/`-O3` + LTO).

So your job isn't "sprinkle `inline` everywhere." It's:

- **Put hot-function implementations in the header** (or turn on LTO), so the compiler **can see** the implementation — that's the precondition for inlining.
- **Don't block with `noinline` casually** (sometimes used for debugging or to control code size, but remember to remove it).
- When you really must force inlining, use `[[gnu::always_inline]]` (GCC/Clang) or the non-standardized `__attribute__((always_inline))`, but sparingly — the compiler's cost model is usually more accurate than yours.
- C++20's `[[gnu::flatten]]` forces inlining of a whole call chain; occasionally useful on extremely sensitive hotspots.

**A common mistake**: treating `inline` as the "make function fast" silver bullet and sticking it everywhere. If the implementation isn't in a header (cross-TU), the `inline` keyword does nothing — the compiler still can't see the implementation. LTO (ch07-02) fixes that.

## Devirtualization: turn a virtual back into an inlineable call

Virtual functions are inline's natural enemy, because who's called isn't known until runtime vtable lookup, and the compiler doesn't dare inline. **But if the compiler can prove "the runtime type is fixed,"** it **devirtualizes** the virtual call into a direct call, which can then be inlined. We measure four call styles (local, average ns per call):

```text
===== Virtual functions and devirtualization =====
  virtual (pointer, runtime polymorphism):  0.55 ns  ← vtable lookup + indirect jump, blocks inlining
  final class (compiler devirt):            0.54 ns
  direct object (non-pointer, often devirtualized): 0.23 ns
  CRTP (static polymorphism, no vtable):    0.22 ns  ← inlineable
  virtual/CRTP = 2.5x
```

Two things to read out:

**1. A virtual call through a pointer (0.55 ns) is 2.5x slower than an inlineable CRTP/direct object (0.22-0.23 ns).** Reading the assembly, in this example the virtual call was actually **speculatively devirtualized** by GCC (at runtime it compares the vptr against a compile-time-known target address, and on a hit takes an inlined fast path), and the function body `x*3+1` has already been inlined — so the extra overhead in the 0.55 ns tier isn't "vtable lookup + indirect jump," it's the per-iteration **type guard** in the loop (load vptr, compare against known address, conditional branch); the direct-object/CRTP tier has no such guard, so it's faster. **CRTP (static polymorphism, implemented with templates)** pushes polymorphism to compile time, has no vtable, can be inlined, and is the fastest.

**2. `final` didn't speed up the loop in this example (0.54 ≈ 0.55).** I marked `struct DerivedF final`, expecting the compiler to "know there's no further derivation" and dare to devirtualize — but 0.54 ≈ 0.55 **is not "final didn't devirtualize"!** Local `-fopt-info-all` shows that the virtual calls on the final class (`DerivedF`) and the ordinary derivation (`Derived`) were both **speculatively devirtualized** by GCC (same mechanism as point 1). The real cause of 0.54 ≈ 0.55 is "the per-iteration type-guard cost of speculative devirtualization is comparable to one virtual call, so nothing was saved." This is an **honest, important result**: **don't assume that marking `final` automatically speeds things up — whether devirtualization actually saves overhead depends on whether the assembly really turned into a direct `call`** (`-S`: a virtual call is `call [vtable+offset]` indirect jump, a full devirtualization is a direct `call func`).

Corollary: **don't preemptively turn virtuals into CRTP/templates for "might be faster."** Measure first — the compiler may already have devirtualized (especially when you call through a direct object); only when you measure that virtual functions are the bottleneck should you reach for CRTP or `final`. ch06-01 expands this into a full discussion of "the cost of virtual functions."

## The compiler optimization landscape: what -O2/-O3 actually do

Place inline and devirt into the bigger picture. One table showing roughly what each `-O` level does in GCC/Clang (precise list in the official docs):

| Level | Roughly what it does | When to use |
|---|---|---|
| `-O0` | No optimization, variables observable | Debugging (**performance numbers are meaningless**) |
| `-O1` | Basic optimizations (constant folding, simple inlining) | — |
| `-O2` | Most optimizations: CSE, LICM, register allocation, scheduling, **auto-inlining (same TU)**, basic vectorization | **release default sweet spot** |
| `-O3` | More aggressive: **loop vectorization, auto-unrolling, more aggressive inlining** | Numerical/SIMD-friendly code; occasionally backfires (icache) |
| `-Os`/`-Oz` | Optimize for **size** | Embedded with flash limits |

Key recognitions:

- **`-O2` is the release default sweet spot.** It already does most of ch04-02's loop optimizations, same-TU inlining, and basic scheduling.
- **`-O3` over `-O2` is mostly "more aggressive vectorization + unrolling + inlining."** Useful for numerical-compute/SIMD-friendly code; **may actually slow down** branch-heavy, irregular code (code bloat → icache misses).
- **Cross-TU inlining and optimization, `-O2`/`-O3` can't do** — that needs LTO (ch07-02). That's why large projects recommend LTO for release builds.

## Don't block the compiler: optimization blockers

inline and the optimizations in this section all assume "the compiler can see it and dares to optimize." A few situations **block the compiler** (optimization blockers, ch07-01 in detail):

- **Cross-TU**: can't see the implementation → won't inline. Fix: put the implementation in a header / turn on LTO.
- **Pointer aliasing**: the compiler doesn't know whether two pointers point at the same address and doesn't dare aggressively reorder memory reads/writes. Fix: declare no alias with `__restrict` (C99 origin; a C++ extension but supported by GCC/Clang).
- **`volatile`**: forces a real memory read/write every time, effectively disabling optimization. Use only for MMIO/signals/lock-free flags, **never for performance**.

All three are "you wrote something that blocked the compiler's path." The spirit of this article is: **the main work of inlining and compiler optimization isn't "actively do something," it's "don't block the path + let it see the implementation."** Active hand-writing (forced inline, CRTP) comes only after you've measured a bottleneck.

Compressing this article into a few lines: inline's real value is opening up optimization space across function boundaries (CSE, constant propagation, strength reduction), not saving call overhead; the `inline` keyword is only a hint, and letting the compiler see the implementation (header / LTO) is the precondition for inlining; devirtualization can turn a virtual function back into an inlineable call, but it's not "slap on `final` and you save overhead" — in this example both the final class and the ordinary derivation were speculatively devirtualized by GCC (provable with `-fopt-info-all`), and 0.54 ≈ 0.55 is the per-iteration guard cost that didn't get saved; direct-object calls are easier to fully devirtualize; CRTP is static polymorphism and the fastest (virtual/CRTP = 2.5x); `-O2` is the release sweet spot, and `-O3` adds aggressive vectorization/unrolling that's numerical-friendly but occasionally backfires; optimization blockers (cross-TU, aliasing, volatile) are you blocking the compiler's path — the next article, ch04-05, runs into the "does the compiler dare" question again in the SIMD context.

## References

- Piotr Padlewski, *C++ devirtualization in clang* (CppCon 2015 Lightning) — devirtualization mechanisms (reused in vol10).
- GCC/Clang docs on `-O` levels, `-finline-*`, `__restrict`, the `always_inline` attribute.
- ch06-01 Virtual functions and devirtualization (this volume; the full discussion of virtual-function cost).
- Measured code for this article: `code/volumn_codes/vol6-performance/ch04/virtual_devirt.cpp`.
