---
chapter: 6
cpp_standard:
- 17
description: Virtual functions are the cornerstone of C++ polymorphism and the abstraction
  most often assumed to be slow. This article pins down their real cost with measurements
  (a virtual call through a pointer is 2.5x slower than an inlinable CRTP), but the
  core message is that modern compilers devirtualize — in many scenarios your virtual
  functions have long since been optimized into direct calls or even inlined. Don't
  preemptively rewrite virtual functions into CRTP/templates for a maybe-faster; measure
  first
difficulty: advanced
order: 1
platform: host
prerequisites:
- inline, devirtualization, and the compiler optimization landscape
- 'Pipeline, ILP, and branch prediction: same data, several-fold differences depending
  on how you execute it'
reading_time_minutes: 5
related:
- 'The zero-cost model of exceptions: free on the normal path, expensive on the exception
  path'
- 'std::function''s small buffer optimization: the cost of type erasure'
tags:
- host
- cpp-modern
- advanced
- 优化
title: 'Virtual functions and devirtualization: don''t rush to rewrite virtuals as
  templates'
translation:
  source: documents/vol6-performance/ch06-cpp-abstraction-cost/06-01-virtual-devirtualization.md
  source_hash: 2c3f14b54ad7b46481a07ddb8a11f73ec0c18fdc826568ab96b7b112d9c20ce5
  translated_at: '2026-09-26T08:36:15+00:00'
  engine: anthropic
  token_count: 1150
---
# Virtual functions and devirtualization: don't rush to rewrite virtuals as templates

## "Virtual functions are slow" is one of vol6's headline propositions

Among the claims Carruth pulls out in *There Are No Zero-Cost Abstractions* — "there are no zero-cost abstractions" — virtual functions are the one most often named. Their cost comes from three places: **the vtable lookup (one extra memory access, and the vtable itself may cache-miss) + the indirect jump (obstructs the pipeline, and the branch predictor may guess it wrong) + blocked inlining** (the compiler cannot see which function gets called at run time, so it won't risk optimizing across the call). Stack those three together and a virtual call really does cost more than a direct call.

But this article's core message is counterintuitive: **"virtual functions are slow" is the upper bound, not the norm**. Modern compilers **devirtualize** — in many scenarios they can turn a virtual call into a direct call or even inline it, taking the overhead to zero. ch06 is the home chapter of "the performance cost of C++ abstractions", yet for virtual functions our advice is "**measure first, don't rewrite preemptively**". That stance does not contradict "virtual functions have a cost": the cost is real, but it often simply never happens.

## Run it yourself: the real cost of four calling styles

Let's pull in the data already measured in ch04-04 (on this machine, average ns per call):

```text
===== 虚函数与去虚拟化 =====
  虚函数(指针,运行时多态):   0.55 ns  ← 查 vtable + 间接跳转,阻碍内联
  final 类(编译器去虚化):    0.54 ns
  直接对象(非指针,常去虚化): 0.23 ns
  CRTP(静态多态,无虚表):    0.22 ns  ← 可内联
  虚函数/CRTP = 2.5x
```

When reading this table, keep "upper bound" and "norm" apart:

**The upper bound: a virtual call through a pointer/reference (0.55 ns) is the most expensive.** Here the compiler cannot see the run-time type, so it dutifully does the vtable lookup + indirect jump — 2.5x slower than CRTP (0.22 ns). That is the hard number behind "virtual functions have a cost".

**The norm: many of these calls are in fact devirtualized:**

- **Calling on a direct object (not a pointer/reference)** (0.23 ns): in code like `Derived d; d.foo();` the compiler can see the exact type of `d` and devirtualizes it into a plain call — **exactly as fast as CRTP**.
- **`final` classes/methods**: telling the compiler "nothing derives further" can sometimes trigger devirtualization. But watch out: **in my example `final` was exactly as fast as the plain virtual function** (0.54 ≈ 0.55) — and that is *not* "final didn't devirtualize"! `-fopt-info-all` shows that the virtual calls on both the `final` class (`DerivedF`) and the plain derived class (`Derived`) were **speculatively devirtualized** by GCC (speculative devirtualization: at run time, compare the vtable entry against a target address known at compile time; on a hit, take the inlined path). The real reason 0.54 ≈ 0.55 is that "the run-time comparison done by speculative devirtualization costs about as much as one virtual call, so nothing was saved" — not "final had no effect". Honest conclusion: **don't assume that marking `final` automatically makes it fast; whether devirtualization actually saves anything is decided by whether the assembly truly turned into a direct `call`** (`-S`: a virtual call is the indirect jump `call [vtable+offset]`; full devirtualization is a direct `call func`).
- **CRTP (static polymorphism)**: pushes polymorphism to compile time (templates) — no vtable, inlinable, always the fastest. The price is code bloat (one instantiation per derived class) plus losing run-time polymorphism.

## When you genuinely need devirtualization

The compiler **devirtualizes on its own** in the following scenarios (nothing for you to do):

- Calls on a direct object (not a pointer/reference), with the type visible at the call site.
- A derivation hierarchy plus `final`, letting the compiler prove there is a single implementation.
- With LTO enabled, type information becomes visible across translation units, creating more devirtualization opportunities.
- Monomorphic hot spots: when profile feedback shows one virtual call site hits the same type 99% of the time, some compilers/PGO setups can devirtualize on that basis.

So for "has my virtual function been devirtualized", **look at the assembly** (`-S`: a direct call is `call func`; a virtual call is the indirect jump `call [vtable+offset]`) — don't guess.

## Practical advice: don't CRTP-ify ahead of time

Compress all of the above into one workflow:

1. **Write it clearly with virtual functions first** (OOP expressiveness at its best, easy to maintain).
2. **Profile** to find the hot spots. If a virtual call is not in a hot spot, leave it alone — it is already fast enough.
3. **If it really is in a hot spot**, check the assembly first to see whether it got devirtualized. If it did, you are done.
4. **If it wasn't devirtualized and it is the bottleneck**, then consider: `final`, switching to direct-object calls, PGO — and only at the very end, CRTP/templates.

**The most common antipattern is "someone said virtual functions are slow, so the whole class hierarchy gets CRTP-ified on day one"**: code complexity explodes (template error messages, code bloat), while the original virtual calls may have been devirtualized by the compiler ages ago, or may never sit in a hot spot at all. This is **premature optimization** in its textbook form, in the domain of C++ abstraction costs. ch04-04 said the same thing about inline: your job is "stay out of the compiler's way + measure out the real bottleneck, then fix it precisely", not "hand-write ever-faster-looking code".

> Boundary note: the **mechanics** of virtual functions (vtable layout, virtual destructors, `override` semantics) belong to class design in vol4; vol6 only asks "is it expensive when it runs on the hardware, and how to keep it from being expensive".

One sentence to wrap up: the **upper-bound** cost of a virtual function is 0.55 ns (via pointer), about 2.5x the inlinable CRTP (0.22 ns), sourced from the vtable lookup + indirect jump + blocked inlining; but many virtual calls do get devirtualized into direct calls/inlining (direct objects, `final`, LTO, PGO, monomorphic hot spots) — in this example `final` through a pointer did not trigger it (0.54 ≈ 0.55), which honestly says it is not guaranteed. In practice, follow "write clearly with virtual functions → profile → confirm in the assembly that it was not devirtualized + it is the bottleneck → only then consider `final`/CRTP"; don't CRTP-ify ahead of time — that is premature optimization.

The next article is about exceptions. Like virtual functions, they are often misread as "slow", while the real cost model is "zero cost on the normal path, expensive on the exception path".

## References

- Piotr Padlewski, *C++ devirtualization in clang* (CppCon 2015 Lightning) — the devirtualization machinery, reused in vol10
- ch04-04 "inline, devirtualization, and the compiler optimization landscape" (where this article's virtual-function data comes from)
- Agner Fog, *Optimizing software in C++*, §7 *Virtual functions* (local copy)
- This article's measurement code: `code/volumn_codes/vol6-performance/ch04/virtual_devirt.cpp` (shared with ch04-04)
