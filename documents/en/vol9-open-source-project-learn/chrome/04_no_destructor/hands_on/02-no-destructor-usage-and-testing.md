---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: "Where NoDestructor actually belongs (one use / four don'ts), constinit global vs function-local static, the LSan reachability hack, and the test invariants"
difficulty: advanced
order: 2
platform: host
prerequisites:
- 'NoDestructor hands-on (I): motivation, interface, and implementation'
reading_time_minutes: 6
related:
- 'NoDestructor hands-on (I): motivation, interface, and implementation'
tags:
- host
- cpp-modern
- advanced
- 内存管理
- 内存安全
title: "NoDestructor hands-on (II): usage boundaries, LSan, and testing"
---
# NoDestructor hands-on (II): usage boundaries, LSan, and testing

In the last piece we pulled apart the "why" and the implementation of NoDestructor. This one is about using it right. We will be honest: it is easier to misuse than you'd think, because the surface where it's actually correct is narrow, and every wrong way to apply it feels natural. Let's stand up the one pattern that's right, then walk the four most common misuses in reverse, and finish with two engineering details you can't dodge: who actually carries the thread-safety, and why LSan gets annoyed at it.

## The one pattern where it belongs

```cpp
const T& GetGlobal() {
    static const base::NoDestructor<T> x(args...);   // ✓ function-local static + non-trivially-destructible T
    return *x;
}
```

Those three lines look unremarkable, but all three conditions must hold: T is non-trivially destructible (otherwise NoDestructor is just dead weight), it's written as a function-local static (more on that below), and the object is genuinely global, something the whole program needs. Only when all three line up does NoDestructor earn its place.

## Four places not to use it

The pattern above is the only one that's right. Yet when we read our own old code, or scan community usage, the wrong applications pile up. Chromium spells them out clearly in the Caveats section of `no_destructor.h:15-46`. Grouped by how often we've seen each one trip people up:

| Case | Don't use NoDestructor, use | Reason |
|---|---|---|
| Local variable / member | Plain `T` or `unique_ptr<T>` | NoDestructor is a **real leak** (never reclaimed when it should be) |
| Trivially-destructible T | Bare `static T x` | Generates no global destructor, NoDestructor buys nothing |
| Trivially-constructible + destructible T | `constinit T x` / `constexpr` | Initialized at compile time, no runtime code at all |
| Rarely-used data | A create-on-demand function (return by value) | NoDestructor-backed cache wastes bss memory |

The first row is the easiest to fumble. People reach for NoDestructor as if it were "a fancier unique_ptr" and tuck it into a member, or wrap a local. That's not skipping a destructor, that's genuinely leaking memory: when the object should be reclaimed, nothing reclaims it. The middle two rows are really the same point stated twice. If T's destructor does nothing (POD, trivially destructible), it produces no global destructor in the first place, so wrapping it in NoDestructor is redundant. Push it one step further: if T is also constinit/constexpr-constructible, it's already initialized at compile time with zero runtime code, and constinit is the answer. The last row is rarer but we've hit it: "cache it just in case" cold data, hung off a NoDestructor, sits in bss for the whole process. A plain create-on-demand function is better.

## constinit global vs function-local static

We kept stressing "function-local static" in the one-correct-pattern above. Here's the exception, stated plainly. Default to the function-local static. It does two jobs at once: it sidesteps the global-constructor initialization-order trap, and it picks up thread safety from C++11 magic statics, and the code is shorter. Only when T is itself constinit-constructible can you write a global `constinit const NoDestructor<T> g(...)`, which produces no static initializer at all and is genuinely zero-overhead.

There's a trap here we've stepped on. If T is not constexpr-constructible, don't assume a global `NoDestructor<T> g(...)` is safe. It still generates a static initializer, because NoDestructor's own constructor is not constexpr. There's no other route; you fall back to the function-local static. Put another way, the constinit-global path is open only to constinit-constructible T, and that bar is higher than it looks.

## magic statics: thread safety comes from them, not from NoDestructor

This one gets buried under the impression that "NoDestructor makes it thread-safe." NoDestructor adds zero locks. It contributes nothing to thread safety. What carries the load is the C++11 function-local-static initialization guarantee: the first thread to enter the function runs the initialization, concurrent threads block until it finishes. So the repeated emphasis on "function-local static" wasn't a clever trick to dodge the global ctor. It's also the only source of NoDestructor's thread safety. Use it any other way and that guarantee is gone.

## The LSan leak tradeoff

"Not destructing" sounds nice; the cost has to be put on the table too. First, the resource isn't released. The OS reclaims everything at process exit, so for a pure-memory T the impact is small. Second, the destructor's side effects don't fire, and that's the dangerous one: if T's destructor flushes to disk, sends a notification, or reports state, skipping it breaks the program's logic. So NoDestructor fits only pure-resource T. Anything whose destructor has side effects, stay away.

Then there's a fairly ugly LSan false positive. NoDestructor parks the object inside a `char storage_[]`, and LSan can't read that byte array. During reachability analysis it can't see the pointers hidden inside, so it flags all the heap memory NoDestructor holds as leaked. Chromium's fix is a fairly cute hack (`no_destructor.h:132-142`): under `LEAK_SANITIZER` builds it additionally holds a `T* storage_ptr_ = reinterpret_cast<T*>(storage_)`, feeding LSan a `T*` root it does understand and reconnecting the reachability chain (crbug/40562930). That field exists only under LSan builds; regular builds pay nothing. Our teaching version skips this trick and just suppresses the false positive with a suppression file when running under LSan.

## Test invariants

NoDestructor's correctness, expressed as tests, comes down to five invariants. Let's go through them one by one.

Construction runs exactly once. That's the magic-statics promise: call the wrapper function many times, and T's constructor should fire exactly once. A constructor-call counter verifies it. Right next to it is its mirror: the destructor never runs. You can't assert this the usual way, because the destructor doesn't happen. The common approach is a noisy T that logs from its destructor, then check after the program finishes that the log never appeared. A death test or a separate isolated process works.

The third invariant is enforced at compile time. NoDestructor rejects trivially-destructible T, so something like `NoDestructor<int>` should be turned away by a static_assert and fail to compile. The fourth is thread safety: hammer the same wrapper function from multiple threads on first call, and the constructor still runs exactly once. A counter plus concurrent pressure, no race, passes. Last is access semantics. `*nd`, `nd->`, and `nd.get()` must all behave like an ordinary T. This is basic, but we've watched someone tweak the internal storage, skip the access-semantics tests, and roll the change into a crash. Don't save those few lines.

That closes the NoDestructor series. In hindsight, the vol9/chrome track stacks into four pieces: **OnceCallback** owns callback lifetime and cancellation, **WeakPtr** owns weak references, **flat_map** owns the high-performance container, and **NoDestructor** owns static lifetime. Four different axes of industrial C++ design, and together they cover the parts of Chromium `//base` most worth learning.

## References

- [Chromium `base/no_destructor.h` (Caveats + the LSan hack)](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [crbug.com/40562930 (the LSan false positive)](https://crbug.com/40562930)
- [LeakSanitizer documentation](https://clang.llvm.org/docs/LeakSanitizer.html)
- [NoDestructor hands-on (III): when to use it, and when not to](../full/04-3-no-destructor-when-to-use.md)
