---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: "For readers already comfortable with lifetime management, a fast walkthrough of NoDestructor's motivation, API, and implementation. The condensed design-guide version of the full/ series (NoDestructor is small, so design and implementation sit in one piece)."
difficulty: advanced
order: 1
platform: host
prerequisites:
- Move semantics and perfect forwarding
- 'NoDestructor prerequisite (0): static storage duration, initialization, and destruction'
reading_time_minutes: 7
related:
- 'NoDestructor design guide (II): usage boundaries and testing'
tags:
- host
- cpp-modern
- advanced
- 内存管理
- RAII
title: "NoDestructor Design Guide (I): Motivation, API, and Implementation"
---
# NoDestructor Design Guide (I): Motivation, API, and Implementation

> Hands-on track. Assumes you're already comfortable with static storage duration, placement new, and aligned storage; if not, skim the [full/ prerequisites](../full/pre-00-static-storage-and-init.md) first.

Chromium has a hard rule we initially found odd: global objects are not allowed to have constructors, and they're not allowed to have destructors. Turn on `-Wglobal-constructors` and `-Wexit-time-destructors`, then dare to put a constructed object at global scope, and the compiler throws it back in your face at build time. The reasoning is actually pretty sound. Constructors slow startup, destructors trigger shutdown races, and the static initialization order fiasco (SIOF) is an accounting mess nobody wants to touch. But global singletons are everywhere: default configs, feature flags, random nonces, all resident from the moment the process starts. Ban them, and how do you even write the code?

We tried every ready-made path, and none clears both gates. A bare global is the first to die; it walks straight into the warning. The Meyers singleton looks clever. `static T& f(){static T x;return x;}` leans on C++11 magic statics for thread-safe lazy construction, and for a while we thought it was the answer. But it only solves the construction half. `~T()` still has to run, so that whole pile of shutdown destructors is still there. The last path is hand-rolled placement new, which works, but every call site has to remember to copy a static_assert block and handle LSan reachability. Miss one spot and it's a time bomb.

NoDestructor is Chromium's patch for this dead end. Pull the idea apart and it's two moves. Move the object into a function-local static, sidestepping the global-construction gate and picking up magic statics' thread-safe initialization for free. Then simply don't call `~T()`, sidestepping the global-destruction gate too. The cost: the object "leaks on purpose." It doesn't destruct; the OS reclaims it when the process exits. It sounds crude, but for a global singleton that's supposed to live until the very last tick, that's exactly how it should behave.

## The API

```cpp
template <typename T>
class NoDestructor {
public:
    template <typename... Args>
    explicit NoDestructor(Args&&... args);   // perfect-forwarding construct
    explicit NoDestructor(const T& x);        // copy construct (handy for initializer_list)
    explicit NoDestructor(T&& x);             // move construct
    NoDestructor(const NoDestructor&) = delete;
    ~NoDestructor() = default;                // ← key: doesn't call ~T()
    const T& operator*() const;  T& operator*();
    const T* operator->() const; T* operator->();
    const T* get() const;  T* get();
};
```

The surface is this small, and it surprised us on the first read. There's a smart-pointer facade with `*`/`->`/`get`, and copy is deleted outright. Otherwise two NoDestructor instances each running `~char[]` is fine, but the moment someone tries to deep-copy the `storage_` inside, everything breaks. It also doesn't inherit T, and that's deliberate: NoDestructor wants to be a container, semantically independent, not entangled in T's inheritance chain. The everyday usage is one line, `static const NoDestructor<T> x(args); return *x;`. Keep that shape in mind and you're set.

## Implementation (complete, ~50 lines)

```cpp
// Platform: host | C++ Standard: C++20
#pragma once
#include <new>
#include <type_traits>
#include <utility>

namespace tamcpp::chrome {

template <typename T>
class NoDestructor {
public:
    template <typename... Args>
    explicit NoDestructor(Args&&... args) {
        new (storage_) T(std::forward<Args>(args)...);   // placement new
    }
    explicit NoDestructor(const T& x) { new (storage_) T(x); }
    explicit NoDestructor(T&& x)      { new (storage_) T(std::move(x)); }

    NoDestructor(const NoDestructor&) = delete;
    NoDestructor& operator=(const NoDestructor&) = delete;
    ~NoDestructor() = default;   // only destructs the char member, doesn't call ~T()

    const T& operator*()  const { return *get(); }
    T&       operator*()        { return *get(); }
    const T* operator->() const { return get(); }
    T*       operator->()       { return get(); }
    const T* get() const { return reinterpret_cast<const T*>(storage_); }
    T*       get()       { return reinterpret_cast<T*>(storage_); }

private:
    static_assert(!(std::is_trivially_constructible_v<T> &&
                    std::is_trivially_destructible_v<T>),
                  "T trivially ctble+dtble: use constinit T directly");
    static_assert(!std::is_trivially_destructible_v<T>,
                  "T trivially destructible: use plain function-local static T");

    alignas(T) char storage_[sizeof(T)];

    // Under LEAK_SANITIZER builds Chromium additionally holds a T* storage_ptr_
    // as an LSan reachability root (crbug/40562930); the teaching version omits
    // it and uses an LSan suppression file instead.
};

}  // namespace tamcpp::chrome
```

When the code runs, there are a handful of deliberate tradeoffs. We'll point them out one by one so you can match them against what you read:

| Decision | Implementation | Reason |
|---|---|---|
| Storage as `alignas(T) char[N]` | `alignas(T) char storage_[sizeof(T)]` | Inline buffer, zero heap allocation, alignment satisfies placement new |
| Construct via placement new | `new (storage_) T(forward<Args>(args)...)` | Construct in place on storage_, no allocation |
| Destructor `= default` | `~NoDestructor() = default` | Only destructs the char member (trivial), **doesn't call `~T()`**; this is the root of "no destructor" |
| static_assert gating | Two assertions | Only serves non-trivially-destructible T; the trivial case is routed to constinit / a plain static |

## How "no destructor" actually works

That one line in the table, `~NoDestructor() = default`, is the heart of the whole mechanism, so we'll pull it out on its own. It plays a neat trick on type visibility.

`storage_` is declared as `char[]`, the only type the compiler sees when generating the destructor. A `char` destructor is trivial; it does nothing. What about T? T gets constructed later by placement new, in place at `storage_`'s start address. As far as the compiler is concerned, T has no type-level relationship to `storage_`, so when destructing `storage_` it never thinks to recall T. `~T()` is therefore never wired into NoDestructor's destruction path. T just sits there quietly until the process ends, when the OS reclaims everything in one sweep. One shift of type perspective buys the entire "no destructor" semantics, and we think that's a beautiful move.

## References

- [Chromium `base/no_destructor.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [NoDestructor in practice (II): core implementation](../full/04-2-no-destructor-core-impl.md)
- [NoDestructor prerequisite (1): placement new and aligned storage](../full/pre-01-placement-new-and-aligned-storage.md)
