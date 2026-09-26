---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: For readers already comfortable with lifetime management, a one-article
  walkthrough of NoDestructor's motivation, API, and implementation — the condensed
  design-guide edition of this series' full/ track (NoDestructor is small, so design
  and implementation are merged into one piece).
difficulty: advanced
order: 1
platform: host
prerequisites:
- Move semantics and perfect forwarding
- 'NoDestructor prerequisite (0): static storage duration, initialization, and destruction'
reading_time_minutes: 7
related:
- 'NoDestructor Design Guide (II): usage boundaries, LSan, and testing'
tags:
- host
- cpp-modern
- advanced
- 内存管理
- RAII
title: "NoDestructor Design Guide (I): motivation, API, and implementation"
translation:
  source: documents/vol9-open-source-project-learn/chrome/04_no_destructor/hands_on/01-no-destructor-design-and-impl.md
  source_hash: 44438a085ef20ca73d88f7a1271380bbe123ca5ae2ae442a4f51ff4c15d3248c
  translated_at: '2026-09-26T03:29:58+00:00'
  engine: anthropic
  token_count: 3200
---
# NoDestructor Design Guide (I): motivation, API, and implementation

> Hands-on track: this piece assumes you're already fluent in static storage duration, placement new, and aligned storage. If not, skim the [full/ prerequisites](../full/pre-00-static-storage-and-init.md) first.

Chromium has a hard rule that struck us as odd at first: global objects are forbidden to have constructors, and forbidden to have destructors. Turn on `-Wglobal-constructors` and `-Wexit-time-destructors`, then dare to place an object with a constructor at global scope, and the compiler slaps you in the face at build time. The reasoning is actually pretty down-to-earth — constructors slow down startup, destructors trigger shutdown races, and the static initialization order fiasco (SIOF) is a murky ledger nobody wants to audit. But global singletons are needed everywhere: default configs, feature flags, random nonces — which of those isn't resident from the moment the process starts? Ban them, and how is the code supposed to be written at all?

We walked every ready-made path, and none clears both gates. The bare global dies first; it walks straight into the line of fire. The Meyers singleton looks clever — `static T& f(){static T x;return x;}` leans on C++11 magic statics to solve both thread safety and laziness of construction, and for a while we thought it was the answer. But it only solves the construction half: `~T()` still has to run, and that whole pile of shutdown-time destruction races remains. The last path is hand-rolled placement new. It works, but every site has to remember to copy in a set of static_asserts and remember to handle LSan reachability — miss one spot and you've planted a time bomb.

NoDestructor is Chromium's patch for this dead end. Pull the idea apart and it's really just two moves: move the object into a function-local static, dodging the global-construction gate while picking up magic statics' thread-safe initialization for free; then simply never call `~T()`, dodging the global-destruction gate along with it. The cost? The object "leaks on purpose" — no destruction, and the operating system sweeps it up when the process exits. It sounds crude, but for a global singleton that's supposed to live until the process's very last tick, this is exactly how it should behave.

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

That's the entire surface — it genuinely surprised us on a first read. A smart-pointer facade of `*`/`->`/`get`, with copy deleted outright: otherwise two NoDestructor instances each running `~char[]` would be harmless enough, but if someone tried to deep-copy the `storage_` inside, it would all fall apart. It also doesn't inherit from T, and that's deliberate: NoDestructor wants to play the role of a container, so semantically it must stay independent and stay out of T's inheritance chain. Everyday usage is a single line, `static const NoDestructor<T> x(args); return *x;` — memorize that one shape and you're set.

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

    // Under LEAK_SANITIZER builds Chromium additionally holds a T* storage_ptr_ as an LSan reachability root
    // (crbug/40562930); the teaching version omits it and uses an LSan suppression file instead.
};

}  // namespace tamcpp::chrome
```

Running the code leaves just a handful of deliberate tradeoffs; we'll point them out one by one so you can match each against what you're reading:

| Decision | Implementation | Reason |
|---|---|---|
| Storage is `alignas(T) char[N]` | `alignas(T) char storage_[sizeof(T)]` | Inline buffer, zero heap allocation, alignment satisfies placement new |
| Construction via placement new | `new (storage_) T(forward<Args>(args)...)` | Constructs in place on storage_, no allocation |
| Destructor `= default` | `~NoDestructor() = default` | Only destructs the char member (trivial), **never calls `~T()`** — this is the root of "no destructor" |
| static_assert gating | Two assertions | Serves only non-trivially-destructible T; the trivial cases are routed to constinit / a plain static |

## How "no destructor" actually works

That one table row, `~NoDestructor() = default`, is the heart of the whole mechanism, so we're pulling it out on its own — because it plays a rather elegant trick on type perspective.

`storage_` is declared with type `char[]`, and that is the only type the compiler sees when generating the destructor. A `char` destructor is trivial; nothing needs to be done. And T? T was constructed later, in place at `storage_`'s start address via placement new. As far as the compiler is concerned, it has no type-level relationship to `storage_` whatsoever, so when destructing `storage_` the compiler never thinks to recall it. Thus `~T()` is never scheduled into NoDestructor's destruction path — T simply lives quietly until the process ends, when the operating system reclaims everything in one sweep. One shift in type perspective buys the entire "no destructor" semantics; we find that move rather beautiful.

## References

- [Chromium `base/no_destructor.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [NoDestructor hands-on (II): the core implementation](../full/04-2-no-destructor-core-impl.md)
- [NoDestructor prerequisite (I): placement new and aligned storage](../full/pre-01-placement-new-and-aligned-storage.md)
