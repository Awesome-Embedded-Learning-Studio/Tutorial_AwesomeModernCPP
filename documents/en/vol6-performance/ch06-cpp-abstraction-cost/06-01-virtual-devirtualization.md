---
chapter: 6
cpp_standard:
- 17
description: 'Virtual functions are the cornerstone of C++ polymorphism and the abstraction most often "assumed slow". This article measures their real cost (a virtual call through a pointer is 2.5x slower than an inlinable CRTP), but the core message is: modern compilers devirtualize, and in many scenarios your virtual function has already been optimized into a direct call or even inlined — don''t prematurely rewrite virtual functions into CRTP/templates for "maybe faster", measure first'
difficulty: advanced
order: 1
platform: host
prerequisites:
- inline, devirtualization, and the full landscape of compiler optimization
- Pipeline, ILP, and branch prediction
reading_time_minutes: 5
related:
- The zero-cost model of exceptions
- std::function's small buffer optimization
tags:
- host
- cpp-modern
- advanced
- 优化
title: 'Virtual functions and devirtualization: don''t rush to rewrite virtuals as templates'
translation:
  source: documents/vol6-performance/ch06-cpp-abstraction-cost/06-01-virtual-devirtualization.md
  source_hash: 2c3f14b54ad7b46481a07ddb8a11f73ec0c18fdc826568ab96b7b112d9c20ce5
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3300
---
# Virtual functions and devirtualization: don't rush to rewrite virtuals as templates

## "Virtual functions are slow" is one of vol6's home-court propositions

The proposition Carruth pulls out in *There Are No Zero-Cost Abstractions*, "no zero-cost abstractions", names virtual functions most often. Their cost comes from three places: **a vtable lookup (one extra memory access, the vtable may cache miss) + an indirect jump (disrupts the pipeline, may be mispredicted by the branch predictor) + blocking inlining** (the compiler doesn't know who gets called at runtime, so it doesn't dare optimize across the call). Stack those three and a virtual call really is more expensive than a direct call.

But the core message of this article is counter-intuitive: **"virtual functions are slow" is an upper bound, not the norm**. Modern compilers **devirtualize**, and in many scenarios can optimize a virtual call into a direct call or even inline it, dropping the cost to zero. This ch06 chapter is the home court for "the performance cost of C++ abstractions", but for virtual functions our advice is "**measure first, don't rewrite prematurely**". That stance doesn't contradict "virtual functions have a cost": the cost exists, but often doesn't trigger.

## Run it: the real cost of four kinds of calls

Pulling over the data measured in ch04-04 (this machine, average ns per call):

```text
===== Virtual functions and devirtualization =====
  virtual function (pointer, runtime polymorphism):   0.55 ns  ← vtable lookup + indirect jump, blocks inlining
  final class (compiler devirtualization):    0.54 ns
  direct object (non-pointer, often devirtualized): 0.23 ns
  CRTP (static polymorphism, no vtable):    0.22 ns  ← inlinable
  virtual/CRTP = 2.5x
```

Reading this table, separate "upper bound" from "norm":

**Upper bound: the virtual call through a pointer/reference (0.55 ns) is the most expensive**. Here the compiler can't see the runtime type, dutifully does a vtable lookup plus indirect jump, 2.5x slower than CRTP (0.22 ns). That's the hard number behind "virtual functions have a cost".

**Norm: a lot of calls are actually devirtualized**:

- **Direct object (non-pointer/reference) call** (0.23 ns): in `Derived d; d.foo();` the compiler can see the exact type of `d` and devirtualize into a normal call, **just as fast as CRTP**.
- **`final` class/method**: tells the compiler "nothing derives further", which can sometimes trigger devirtualization. But note, **in this example `final` is just as fast as a plain virtual function** (0.54 ≈ 0.55), and that's not "final failed to devirtualize"! `-fopt-info-all` shows that the virtual calls on both the final class (`DerivedF`) and the plain derived class (`Derived`) have been **speculatively devirtualized** by GCC (speculative devirtualization: at runtime it compares the vtable entry against the compile-time-known target address, and on a hit goes down the inlined path). The real reason 0.54 ≈ 0.55 is that "the runtime comparison cost of speculative devirtualization is about the same as one virtual call, nothing saved", not "final didn't take effect". Honest conclusion: **don't assume that tagging `final` automatically makes it fast — whether devirtualization actually saves cost depends on whether the assembly truly turned into a direct `call`** (`-S`: a virtual call is `call [vtable+offset]` an indirect jump; full devirtualization is a direct `call func`).
- **CRTP (static polymorphism)**: pushes polymorphism to compile time (templates), no vtable, inlinable, always fastest. The price is code bloat (each derived class instantiates a copy) plus losing runtime polymorphism.

## When devirtualization actually happens

Devirtualization happens in these scenarios **without you doing anything**:

- A direct object (non-pointer/reference) call, with the type visible at the call site.
- A derivation hierarchy plus `final` that lets the compiler prove a unique implementation.
- LTO on, where type info across translation units becomes visible and more devirtualization opens up.
- A monomorphic hot spot: profile feedback shows some virtual call site calls the same type 99% of the time; some compilers/PGO can devirtualize on that.

So "has my virtual function been devirtualized" **has to be checked in the assembly** (`-S`: a direct call is `call func`, a virtual call is an indirect `call [vtable+offset]`); don't go by feel.

## Practical advice: don't CRTP-ify prematurely

Compress the above into a workflow:

1. **Write it cleanly with virtual functions first** (OOP expressiveness is best, easy to maintain).
2. **Profile** to find hot spots. If a virtual call isn't in a hot spot, leave it alone; it's already fast enough.
3. **If it really is in a hot spot**, check the assembly first to see whether it devirtualized. If it did, you're done.
4. **If it didn't devirtualize and it's the bottleneck**, then consider: `final`, switching to a direct-object call, PGO, and only at the end CRTP/templates.

**The most common anti-pattern is "someone said virtual functions are slow, so the whole class hierarchy gets CRTP-ified on day one"**: code complexity explodes (template error messages, code bloat), while the original virtual call may have been devirtualized by the compiler already, or may not sit in a hot spot at all. This is the classic form of **premature optimization** in the C++ abstraction-cost field. ch04-04 made the same point about inline: your job is "don't get in the compiler's way plus measure the real bottleneck and fix it precisely", not "hand-write the faster-looking version harder".

> Boundary reminder: the **mechanism** of virtual functions (vtable layout, virtual destructors, `override` semantics) belongs to vol4 class design; vol6 only covers "how expensive it is when running on hardware, and how to make it not expensive".

In one sentence: the **upper bound** cost of a virtual function is 0.55 ns (via pointer), about 2.5x an inlinable CRTP (0.22 ns), sourced from vtable lookup plus indirect jump plus blocking inlining; but many virtual calls get devirtualized into direct calls or inlined (direct object, `final`, LTO, PGO, monomorphic hot spots), and in this example `final` through a pointer didn't trigger it (0.54≈0.55), honestly showing it isn't guaranteed; in practice follow "write cleanly with virtuals first → profile → check assembly, confirm not devirtualized plus is the bottleneck → only then consider `final`/CRTP", and don't CRTP-ify prematurely, that's premature optimization.

The next article covers exceptions. Like virtual functions they're often misunderstood as "slow", and the real cost model is "the normal path is zero-cost, the exception path is expensive".

## References

- Piotr Padlewski, *C++ devirtualization in clang* (CppCon 2015 Lightning) — the devirtualization mechanism, reused in vol10
- ch04-04 inline, devirtualization, and the full landscape of compiler optimization (the source of this article's virtual-function data)
- Agner Fog, *Optimizing software in C++* §7 *Virtual functions* (local)
- The measurement code for this article: `code/volumn_codes/vol6-performance/ch04/virtual_devirt.cpp` (shared with ch04-04)
