---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: "NoDestructor's usage boundaries (the one case to use it, four cases not to), constinit global vs function-local static, the LSan reachability hack, and test invariants"
difficulty: advanced
order: 2
platform: host
prerequisites:
- 'NoDestructor Design Guide (I): motivation, API, and implementation'
reading_time_minutes: 6
related:
- 'NoDestructor Design Guide (I): motivation, API, and implementation'
tags:
- host
- cpp-modern
- advanced
- 内存管理
- 内存安全
title: "NoDestructor Design Guide (II): usage boundaries, LSan, and testing"
translation:
  source: documents/vol9-open-source-project-learn/chrome/04_no_destructor/hands_on/02-no-destructor-usage-and-testing.md
  source_hash: a026c0db95f64cfca81ee3534d8444b2613a581c2b58e3111d83faf08ba73eea
  translated_at: '2026-09-26T03:33:31+00:00'
  engine: anthropic
  token_count: 1600
---
# NoDestructor Design Guide (II): usage boundaries, LSan, and testing

In the previous piece we picked apart the "why" and the implementation of NoDestructor. This one is about using it right. Honestly, this thing is easier to misuse than you would think: the situations where it genuinely applies are very narrow, yet every wrong way of applying it feels quite natural. We will first pin down the one scenario where it belongs, then sweep backwards through the four most common misuses, and finally talk through two engineering details you cannot dodge: who actually carries thread safety, and why LSan gets upset with it.

## The one case where it belongs

```cpp
const T& GetGlobal() {
    static const base::NoDestructor<T> x(args...);   // ✓ function-local static + non-trivially-destructible T
    return *x;
}
```

These three lines look unremarkable, but not one of the three conditions can be dropped: T is non-trivially destructible (otherwise NoDestructor is pure redundancy), the object is written as a function-local static (that point gets its own discussion below), and it is a genuinely global object that the whole program needs. Only when all three are in place does NoDestructor get its turn on stage.

## Four cases where you should not use it

The pattern above is the only one where NoDestructor truly belongs. Yet when we flip through our own older code and look at community usage, the wrong applications pile up. The Chromium source lists them quite clearly in the Caveats at `no_destructor.h:15-46`; grouped by how often people step in them, they fall into four classes:

| Case | Do not use NoDestructor, use instead | Why |
|---|---|---|
| Local variable / member | Plain `T` or `unique_ptr<T>` | NoDestructor is a **real leak** (nothing reclaims the object when it should) |
| Trivially-destructible T | A bare `static T x` | Produces no global destructor, NoDestructor is not needed |
| Trivially-constructible + destructible T | `constinit T x` / `constexpr` | Initialized at compile time, no runtime code |
| Rarely-used data | A create-on-demand function (returning by value) | A NoDestructor cache wastes bss memory |

The first row is the easiest to blur: some people treat NoDestructor as "a fancier unique_ptr" and stuff it into a member, or simply wrap a local variable. That is not skipping a destructor — it is genuinely leaking memory: when the object should be reclaimed, nothing reclaims it. The middle two rows are really two ways of saying the same thing. If T's destructor does nothing (POD, trivially destructible), it never generates a global destructor in the first place, so wrapping another layer of NoDestructor around it is redundant. Going one step further, if T is also constinit/constexpr-constructible, it is initialized at compile time with no runtime code at all — then constinit is the right answer. The last row is on the obscure side, but we have personally stepped in it: cold data "cached just in case", once hung on a NoDestructor, occupies the bss segment for the whole process; you are better off just creating it on demand.

## constinit global vs function-local static

In "the one case where it belongs" we kept stressing the function-local static; here we spell out the exception as well. By default, just write the function-local static — it does two jobs at once: it sidesteps the initialization-order pit dug by global constructors, and it picks up thread safety from C++11 magic statics, and the code is shorter on top. Only when T itself is constinit-constructible may you write a global `constinit const NoDestructor<T> g(...)`, which produces no static initializer at all — truly zero overhead.

There is a hidden pit here that we have personally stepped in. If T cannot be constexpr-constructed, do not assume that writing a global `NoDestructor<T> g(...)` settles everything: it still generates a static initializer, because NoDestructor's own constructor is not constexpr. In that situation there is no other route — you must fall back to the function-local static. Put differently, the constinit-global path is open only to constinit-constructible T, and the bar is higher than you would imagine.

## magic statics: thread safety comes from it, not from NoDestructor

This point easily gets buried under the illusion that "using NoDestructor makes it thread-safe". NoDestructor itself adds not a single lock; its contribution to thread safety is zero. What actually holds the stage is the C++11 initialization guarantee for function-local statics: the first thread to enter the function performs the initialization, and other concurrent threads are held back waiting for it to finish. So the earlier repeated emphasis on "function-local static" is not merely a clever trick for dodging the ctor — it is also the sole source of NoDestructor's thread safety. Use it in some other shape, and this guarantee is gone.

## The LSan leak tradeoff

"Not destructing" sounds great, but the price must be laid out too. First, resources are not released — fortunately the OS reclaims everything at process exit, so the impact for a pure-memory T is small. Second, the destructor's side effects never happen, and that is the lethal one: if T's destructor does things like flushing to disk, sending notifications, or reporting state, skipping it breaks the program's logic. So NoDestructor suits only purely-resource T; anything whose destructor has side effects, do not touch at all.

Then there is a rather nasty LSan false positive. NoDestructor stuffs the object into a `char storage_[]`, and LSan cannot make sense of that byte array — during reachability analysis it fails to recognize the pointers hidden inside, so it rules all heap memory held by NoDestructor a leak. Chromium's fix is a rather crafty hack (`no_destructor.h:132-142`): under `LEAK_SANITIZER` builds it additionally holds a `T* storage_ptr_ = reinterpret_cast<T*>(storage_)`, which amounts to feeding LSan a `T*` root it does recognize and reconnecting the reachability chain (crbug/40562930). This field exists only in LSan builds; regular builds pay nothing. Our teaching edition skips this trick — when running LSan, just press the false positive down with a suppression file.

## Test invariants

When it comes to testing, NoDestructor's behavioral correctness really boils down to five invariants. Let's go through them one by one.

Construction runs exactly once — that is the magic statics promise: call the wrapping function many times, and T's constructor should fire exactly once; you can verify it by counting constructor invocations. Immediately after comes its mirror: the destructor never runs. This one cannot be verified with an ordinary assertion, because the destruction simply does not happen; the common approach is a noisy T that logs from its destructor, then after the program finishes, check that the log never appeared — a death test or an isolated run in a separate process both work.

The third is vetted at compile time: NoDestructor rejects trivially-destructible T, so a usage like `NoDestructor<int>` should be turned away by a static_assert at compile time and simply never compile. The fourth is thread safety: with multiple threads concurrently making the first call to the same wrapping function, the constructor still runs only once — press on it with a counter plus multithreaded load, and no race means a pass. The last is access semantics: `*nd`, `nd->`, and `nd.get()` — all three entry points must behave in line with an ordinary T. This is baseline material, but we have seen someone change the internal storage, skip testing the access semantics, and end up crashing — do not save those few lines of tests.

That completes the entire NoDestructor series. Looking back, the vol9/chrome track has accumulated exactly four puzzle pieces: **OnceCallback** manages callback lifetime and cancellation, **WeakPtr** manages weak references, **flat_map** manages the high-performance container, and **NoDestructor** manages static lifetime. Four industrial-grade C++ designs from four different dimensions — put together, they happen to cover the pieces of Chromium `//base` most worth learning.

## References

- [Chromium `base/no_destructor.h` — Caveats + the LSan hack](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [crbug.com/40562930 — the LSan false positive](https://crbug.com/40562930)
- [LeakSanitizer documentation](https://clang.llvm.org/docs/LeakSanitizer.html)
- [NoDestructor hands-on (III): when to use it, and when not to](../full/04-3-no-destructor-when-to-use.md)
