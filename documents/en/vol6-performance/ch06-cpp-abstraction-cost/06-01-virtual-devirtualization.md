---
chapter: 6
cpp_standard:
- 17
description: Virtual functions are the cornerstone of C++ polymorphism, and also the
  abstraction most often 'assumed slow'. This article uses real measurements to see
  their true cost clearly (a virtual call through a pointer is 2.5x slower than an
  inlinable CRTP), but the core message is that modern compilers devirtualize — in
  many scenarios your virtual functions have long since been optimized into direct
  calls or even inlined. Don't prematurely rewrite virtual functions into CRTP/templates
  for a 'maybe faster'; measure first
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
  translated_at: '2026-09-26T06:54:33+00:00'
  engine: anthropic
  token_count: 3300
---
# Virtual functions and devirtualization: don't rush to rewrite virtuals as templates

## "Virtual functions are slow" is one of vol6's home-court propositions

Virtual functions are the most frequently named suspect under the proposition Carruth pulls out in *There Are No Zero-Cost Abstractions* ("no zero-cost abstractions"). Their cost comes from three places: **the vtable lookup (one extra memory access, and the vtable may be a cache miss) + the indirect jump (disrupts the pipeline, can be mispredicted by the branch predictor) + blocked inlining** (the compiler can't tell who gets called at run time, so it doesn't dare optimize across the call). Stack the three together and a virtual call really does cost more than a direct call.

But the core message of this article is counter-intuitive: **"virtual functions are slow" is an upper bound, not the norm**. Modern compilers **devirtualize**, and in many scenarios they can turn a virtual call into a direct call or even inline it, taking the cost to zero. ch06 is the home court of "the performance cost of C++ abstractions", but for virtual functions our advice is "**measure first, don't rewrite prematurely**". That stance doesn't contradict "virtual functions have a cost": the cost is real, but it often never happens.

## Run it yourself: the real cost of four kinds of calls

Pulling over the data measured in ch04-04 (this machine, average ns per call):

```text
===== 虚函数与去虚拟化 =====
  虚函数(指针,运行时多态):   0.55 ns  ← 查 vtable + 间接跳转,阻碍内联
  final 类(编译器去虚化):    0.54 ns
  直接对象(非指针,常去虚化): 0.23 ns
  CRTP(静态多态,无虚表):    0.22 ns  ← 可内联
  虚函数/CRTP = 2.5x
```

Reading this table, separate "upper bound" from "norm":

**Upper bound: the virtual call through a pointer/reference (0.55 ns) is the most expensive**. Here the compiler can't see the run-time type, so it dutifully does the vtable lookup plus the indirect jump, 2.5x slower than CRTP (0.22 ns). That's the hard number behind "virtual functions have a cost".

**Norm: a lot of calls are in fact devirtualized**:

- **Direct object (non-pointer/reference) call** (0.23 ns): in code like `Derived d; d.foo();` the compiler can see the exact type of `d` and devirtualizes straight into an ordinary call, **just as fast as CRTP**.
- **`final` class/method**: tells the compiler "nothing derives further", which can sometimes trigger devirtualization. But note, **in this example `final` is just as fast as the plain virtual function** (0.54 ≈ 0.55), and that is not "final failed to devirtualize"! `-fopt-info-all` shows that GCC has **speculatively devirtualized** the virtual calls on both the final class (`DerivedF`) and the ordinary derived class (`Derived`) (speculative devirtualization: at run time it compares the vtable entry against the compile-time-known target address, and on a hit takes the inlined path). The real reason 0.54 ≈ 0.55 is "the run-time comparison cost of speculative devirtualization is comparable to one virtual call, so nothing is saved", not "final didn't take effect". Honest conclusion: **don't assume that tagging `final` automatically makes it fast — whether devirtualization actually saves cost depends on whether the assembly truly turned into a direct `call`** (`-S`: a virtual call is an indirect `call [vtable+offset]`; full devirtualization is a direct `call func`).
- **CRTP (static polymorphism)**: pushes polymorphism to compile time (templates) — no vtable, inlinable, always the fastest. The price is code bloat (one instantiation per derived class) plus the loss of run-time polymorphism.

## When devirtualization actually happens

In these scenarios the compiler **does devirtualization on its own** (nothing for you to do):

- Direct object (non-pointer/reference) calls, with the type visible at the call site.
- A derivation hierarchy plus `final` that lets the compiler prove there is a unique implementation.
- With LTO on, type information across translation units becomes visible, opening up more devirtualization opportunities.
- Monomorphic hot spots: profile feedback shows some virtual call site hits the same type 99% of the time, and some compilers/PGO can devirtualize on that basis.

So "has my virtual function been devirtualized" is something **you check in the assembly** (`-S`: a direct call is `call func`; a virtual call is an indirect `call [vtable+offset]`) — don't go by feel.

## Practical advice: don't CRTP-ify prematurely

Compressing all of the above into one workflow:

1. **Write it clearly with virtual functions first** (OOP is the most expressive and the easiest to maintain).
2. **Profile** to find hot spots. If a virtual call isn't in a hot spot, leave it alone — it's already fast enough.
3. **If it really is in a hot spot**, first check the assembly to see whether it devirtualized. If it did, you're done.
4. **If it didn't devirtualize and it is the bottleneck**, then consider `final`, switching to a direct-object call, PGO — and only at the end, CRTP/templates.

**The most common anti-pattern is "someone said virtual functions are slow, so the whole class hierarchy gets CRTP-ified on day one"**: code complexity explodes (template error messages, code bloat), while the original virtual call may have been devirtualized by the compiler long ago, or may not sit in a hot spot at all. This is the classic face of **premature optimization** in the C++ abstraction-cost territory. ch04-04 made the same point when covering inline: your job is "stay out of the compiler's way + measure out the real bottleneck, then fix it precisely", not "hand-write ever-faster-looking code as hard as you can".

> Boundary reminder: the **mechanics** of virtual functions (vtable layout, virtual destructors, `override` semantics) belong to vol4 class design; vol6 only covers "is it expensive once it runs on hardware, and how to make it not expensive".

To close in one sentence: the **upper-bound** cost of a virtual function is 0.55 ns (via a pointer), roughly 2.5x the inlinable CRTP (0.22 ns), coming from the vtable lookup + indirect jump + blocked inlining; but many virtual calls get devirtualized into direct calls or inlined (direct object, `final`, LTO, PGO, monomorphic hot spots) — in this example `final` through a pointer didn't trigger it (0.54≈0.55), an honest acknowledgment that it isn't guaranteed; in practice follow "write clearly with virtual functions first → profile → check the assembly, confirm it wasn't devirtualized + it is the bottleneck → only then consider `final`/CRTP", and don't CRTP-ify prematurely — that's premature optimization.

The next article covers exceptions. Like virtual functions, they're often misunderstood as "slow", while the real cost model is "zero cost on the normal path, expensive on the exception path".

## References

- Piotr Padlewski, *C++ devirtualization in clang* (CppCon 2015 Lightning) — the devirtualization machinery, reused in vol10
- ch04-04, inline, devirtualization, and the compiler optimization landscape (the source of this article's virtual-function data)
- Agner Fog, *Optimizing software in C++* §7 *Virtual functions* (local)
- This article's measurement code: `code/volumn_codes/vol6-performance/ch04/virtual_devirt.cpp` (shared with ch04-04)
