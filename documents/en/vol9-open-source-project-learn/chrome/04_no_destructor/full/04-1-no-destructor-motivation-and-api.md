---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: Start from the pain of a global config table (Chromium bans global ctors/dtors), pin down the hole NoDestructor is meant to fill, and settle the full target API and its interface decisions
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'NoDestructor prerequisite (0): static storage duration, initialization, and destruction'
- 'NoDestructor prerequisite (I): placement new and aligned storage'
reading_time_minutes: 10
related:
- 'NoDestructor hands-on (II): the core implementation'
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- RAII
title: "NoDestructor hands-on (I): motivation and API design"
translation:
  source: documents/vol9-open-source-project-learn/chrome/04_no_destructor/full/04-1-no-destructor-motivation-and-api.md
  source_hash: 11e5e37134ad91751dbda14e801df94ef16b2bfbe457d37c3af0fa99311247be
  translated_at: '2026-09-26T03:07:51+00:00'
  engine: anthropic
  token_count: 5100
---
# NoDestructor hands-on (I): motivation and API design

In [prerequisite (0)](./pre-00-static-storage-and-init.md) we went through the whole case against global ctors/dtors in Chromium — SIOF, shutdown races, startup latency. The problem is that `//base` is full of spots that "need a global singleton": a default config table, a feature-flag map, a random nonce generated on demand. The rule is on the books, the work still has to get done, and that's where `base::NoDestructor<T>` takes the stage. This piece works through the motivation and the interface; the implementation lands in the next one.

## Start from a global config table

Say we have a "default config table" that everything in the program reads, with fixed contents:

```cpp
const std::map<std::string, Config>& DefaultConfig() {
    // how do we implement this global?
}
```

The most instinctive move is to throw out a global variable:

```cpp
const std::map<std::string, Config> g_default = LoadDefault();   // ❌ banned in Chromium
```

It reads nicely, but it forces a global constructor to run before `main` (calling `LoadDefault` plus constructing the `std::map`), and at program exit a global destructor still has to run to tear the map down. Chromium keeps `-Wglobal-constructors` / `-Wexit-time-destructors` switched on, and either one rejects this outright. So what else can we do?

---

## Three ready-made paths, and why none of them suffices

**A bare function-local static** — the recipe recommended in Scott Meyers' book, known in the community as the Meyers singleton:

```cpp
const std::map<...>& DefaultConfig() {
    static const std::map<std::string, Config> g = LoadDefault();   // magic statics: thread-safe construction
    return g;
}
```

It really does dodge the global constructor — `g` only gets constructed on the first call to `DefaultConfig()`, and magic statics take care of the thread safety along the way (see [pre-00](./pre-00-static-storage-and-init.md)). What we missed the first time around was a loose end: `g` still destructs at exit. The `std::map` destructor still gets registered as a global destructor. In other words, it passes the construction gate and fails the destruction gate.

What glares even more is the shutdown race. Say `g` carries a reference to some other global object (a logger pointer, for instance), or the other way around — another global, in the middle of its own destruction, calls back into `DefaultConfig()`. By that point `g` may already have been destroyed, and what you're holding is a dead reference; the program walks straight into undefined behavior. Chromium's shutdown paths are twisty to begin with, and we have seen this race in production more than once. It genuinely hurts.

**Hand-rolled placement new with the destructor skipped** — write `alignas(T) char buf[...]` yourself, placement-new the object onto it, and simply never call the destructor. This path runs, but after throwing together a version for convenience and looking back at it, the pits were denser than expected: LSan compatibility (see [04-4]), `static_assert` gating, alignment, lifetime — all of it lands on us. Write it twice and you're already reinventing the wheel.

So there the three paths sit: the raw global is banned, the Meyers singleton carries a destruction race, and the hand-rolled placement new is repetitive and error-prone. NoDestructor is Chromium's official tool that pulls "the destructor nail" out of the second path and packages up the third path's boilerplate.

---

## Chromium's answer: NoDestructor

The design idea behind NoDestructor, as we'd put it, condenses into two sentences.

First: no destruction. Once the object is constructed, `~T()` is never called again — no destructors means no destruction order, and the shutdown race is gone at the root. The cost is an "intentional leak": the OS reclaims the memory in one sweep when the process exits. In a long-running process like a browser, that leak doesn't even register on the books; in embedded work or a short-lived tool you have to weigh it yourself.

Second: use it paired with magic statics. NoDestructor usually sits wrapped in a function-local static, leaning on C++11 magic statics to guarantee thread-safe first construction.

Put the two together and it's a single line, `static const NoDestructor<T> x(args...);`, handing you a global singleton that is constructed thread-safely and never destroyed. The global-ctor gate is sidestepped by deferring construction inside the local static; the global-dtor gate is sidestepped by NoDestructor not destructing. This is exactly the pattern the Chromium style guide blesses.

### A usage example

```cpp
#include "base/no_destructor.h"

const std::string& GetDefaultText() {
    static const base::NoDestructor<std::string> s("Hello world!");
    return *s;
}
```

`*s` goes through `operator*`, returning `std::string&`. `s` is a NoDestructor: the string is constructed on the first call (thread-safely), it doesn't destruct at program exit, and the memory is left for the OS to reclaim.

If initialization gets more involved, stuffing a lambda in and running it as an IIFE works nicely — say we need a random nonce generated on demand:

```cpp
const std::string& GetRandomNonce() {
    static const base::NoDestructor<std::string> nonce([] {
        std::string s(16);
        FillRandom(s.data(), s.size());
        return s;
    }());
    return *nonce;
}
```

---

## What the target API looks like

Let's put the target API on the table first, aligned with Chromium:

```cpp
namespace tamcpp::chrome {

template <typename T>
class NoDestructor {
public:
    // construct from arbitrary arguments (forwarded to T's constructor)
    template <typename... Args>
    explicit NoDestructor(Args&&... args);

    // copy/move construct directly from a T (handy for initializer_list, etc.)
    explicit NoDestructor(const T& x);
    explicit NoDestructor(T&& x);

    // non-copyable
    NoDestructor(const NoDestructor&) = delete;
    NoDestructor& operator=(const NoDestructor&) = delete;

    // destructor: defaulted (the key! does not call ~T())
    ~NoDestructor() = default;

    // use it like a T
    const T& operator*() const;
    T&       operator*();
    const T* operator->() const;
    T*       operator->();
    const T* get() const;
    T*       get();
};

}  // namespace tamcpp::chrome
```

Usage is direct: hold it as a function-local static and treat `*nd` or `nd->` as the T.

---

## A few signature decisions

The signature looks simple, but every line of it was pinned down by Chromium after stepping in real holes. Let's take apart the spots worth talking about.

**Why the copy operations are deleted.** What a NoDestructor holds is an inline buffer, `alignas(T) char storage_[sizeof(T)]`, not a pointer. Allow copying and you'd have to deep-copy the T inside `storage_` (placement-new a fresh one), and the semantics go muddy in an instant — T is not necessarily trivially copyable, a shallow copy moves bytes while a deep copy has to go through a constructor, so which one is it? Rather than let users step in that pit, just `delete` the copy operations and keep the type in its role as a "static-variable container" instead of a value type to be passed around.

**Why it doesn't inherit from T, and why no T& is exposed.** Inheritance would drag NoDestructor into an is-a relationship with T, while it is at heart a container — wrong semantics; besides, T might be `final`, and then inheritance doesn't even compile. Exposing an inner T& member doesn't work either: that would leak the `storage_` detail. Chromium picked the smart-pointer set — `operator*` / `operator->` / `get()` — so that NoDestructor behaves like "a pointer to T". That way `static const NoDestructor<std::string> s(...)` reads almost as naturally as a `std::string*`.

**`~NoDestructor() = default`: this is the vital gate.** The line looks unremarkable, but it is the root of the whole design. `= default` has the compiler-generated destructor destroy its member `storage_`, and `storage_` is a char array — trivially destructible, it does nothing. So when `~NoDestructor()` finishes, `~T()` is never called at all: T was pressed on with placement new, its destructor would have to be invoked by hand, and here we pointedly don't. Reading this the first time stopped us for a beat — why not just `= delete`? It clicked later: `= delete` blocks lifecycle management for the whole object, making the type unusable as a member or a base; `= default` is what lets NoDestructor itself live and die normally while the T inside plays deaf and dumb.

If we didn't mind the bother and wrote `~NoDestructor() { reinterpret_cast<T*>(storage_)->~T(); }`, we'd be back to a Meyers singleton in all but name — the shutdown race returns untouched and the whole tool was built for nothing. So "don't call `~T()`" is not an oversight; it is the deliberate core choice.

**Why not the `[[clang::no_destroy]]` attribute.** Clang actually has such an attribute: mark a variable with it and its destructor doesn't run:

```cpp
[[clang::no_destroy]] static const std::string s = "...";   // no destruction
```

We muttered the same question at first: if the attribute does the job, why wrap a class around it? Digging through Chromium's comments settled the accounting. The attribute is Clang-exclusive — it doesn't budge on GCC or MSVC, so the portability gate fails right there. And it only handles the single job of skipping destruction: it can't add `static_assert` gating, and it can't stop misuse on trivial types. Worse, the LSan-compatibility hack (see [04-4]) and the type-safe API facade can only be stuffed in if this is packaged as a class. The attribute is lower-level and lighter, but industrial code is steadier with a packaged tool, and easier to diagnose when something goes wrong.

---

## Trade-offs between the teaching version and Chromium

As with the earlier series, the teaching version keeps only the core mechanism — placement new + no destruction + magic statics integration + `static_assert` gating — and strips off the industrial-grade odds and ends around it:

| Dimension | Chromium | Teaching version |
|---|---|---|
| Storage and placement new | full | same |
| `~NoDestructor()=default` to skip destruction | full | same |
| static_assert gating | 2 checks (trivial ctor+dtor / trivial dtor) | same |
| LSan reachability hack | `#ifdef LEAK_SANITIZER` holds storage_ptr_ | omitted or noted (see 04-4) |
| Chromium macro `BASE_EXPORT` | present | omitted |

The core mechanism matches to the letter. The LSan-compatibility piece involves sanitizer black magic, so we pull it out into the 04-4 piece rather than let it distract from the main line here.

---

## Set up the environment first

NoDestructor itself gets by with C++17 (`alignas` / `std::forward`); some of the `static_assert`s come out cleaner with C++20's `_v` variable templates, but C++17 can write them too. We go with C++20, aligned with the earlier series.

### Compiler requirements

GCC 11+ or Clang 12+, with `-std=c++20`.

### Verification code

```cpp
#include <new>
#include <type_traits>

// verify alignas + placement new are usable
struct Foo { int x; };
alignas(Foo) char buf[sizeof(Foo)];

int main() {
    new (buf) Foo{42};                                   // placement new constructs on buf
    return reinterpret_cast<Foo*>(buf)->x - 42;          // 0, verifies the access is correct
}
```

If this runs clean, the environment is ready. The companion project lives in `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/`; starting from 04-2 we add the `23`–`25` batch of NoDestructor samples into it.

---

## References

- [Chromium `base/no_destructor.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [Clang `[[clang::no_destroy]]` attribute](https://clang.llvm.org/docs/AttributeReference.html#no-destroy)
- [isocpp FAQ — the Meyers singleton and the shutdown problem](https://isocpp.org/wiki/faq/ctors#construct-on-first-use)
- [NoDestructor prerequisite (0): static storage duration, initialization, and destruction](./pre-00-static-storage-and-init.md)
