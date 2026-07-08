---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: "Start from the pain of a global config table (Chromium bans global ctors/dtors), pin down the hole NoDestructor fills, and lock down the full target API and its signature decisions"
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'NoDestructor prerequisite (0): static storage duration, initialization, and destruction'
- 'NoDestructor prerequisite (1): placement new and aligned storage'
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
---
# NoDestructor hands-on (I): motivation and API design

In [prerequisite (0)](./pre-00-static-storage-and-init.md) we went over why Chromium bans global constructors and destructors: SIOF, shutdown races, startup latency. The problem is that `//base` is full of places that want a global singleton: a default config table, a feature-flag map, a lazily generated random nonce. The rule is on the books, the work still has to get done, and that's where `base::NoDestructor<T>` comes in. This piece works through the motivation and the API; implementation lands in the next one.

## Start from a global config table

Say we have a "default config table" that the whole program needs, with fixed contents:

```cpp
const std::map<std::string, Config>& DefaultConfig() {
    // how do we implement this global?
}
```

The obvious move is to throw out a global variable:

```cpp
const std::map<std::string, Config> g_default = LoadDefault();   // ❌ banned in Chromium
```

It reads cleanly, but it generates a global constructor before `main` (calling `LoadDefault` and constructing the `std::map`), and a global destructor at exit to tear the map down. Chromium's `-Wglobal-constructors` and `-Wexit-time-destructors` flags reject it outright. What else can we do?

---

## Three obvious paths, and why none of them are enough

**A plain function-local static** is the recipe from Scott Meyers' book, what the community calls a Meyers singleton:

```cpp
const std::map<...>& DefaultConfig() {
    static const std::map<std::string, Config> g = LoadDefault();   // magic statics: thread-safe init
    return g;
}
```

This dodges the global constructor: `g` only gets built on the first call to `DefaultConfig()`, and magic statics handle the thread safety (see [pre-00](./pre-00-static-storage-and-init.md)). What it does not dodge is destruction. When `g` goes out of scope at exit, it still destructs. The `std::map` destructor still gets registered as a global destructor. The construction gate passes; the destruction gate doesn't.

The nastier part is shutdown races. Say `g` holds a reference to another global (some logger pointer), or the other way around: another global, mid-destruction, calls back into `DefaultConfig()`. By then `g` may already have been destroyed, and you're holding a dangling reference. The program hits undefined behavior. Chromium's shutdown paths are twisty enough that this kind of race has bitten me in production more than once. It genuinely hurts.

**Hand-rolled placement new with no destructor** means writing `alignas(T) char buf[...]`, placement-new'ing the object on top, and just not calling the destructor. It works. But after we sketched one for expediency and looked back at it, the holes were denser than expected: LSan compatibility (see [04-4]), `static_assert` gating, alignment, lifetime, all on us. Write it twice and you're reinventing the wheel.

So the three paths are: raw global banned, Meyers singleton has shutdown races, hand-rolled placement new is repetitive and error-prone. NoDestructor is Chromium's official tool: it pulls "the destructor nail" out of the second path, and packages up the boilerplate of the third.

---

## Chromium's answer: NoDestructor

The design idea behind NoDestructor comes down to two lines.

First, no destructor. Once the object is constructed, `~T()` is never called again. No destructor means no destruction order, which removes shutdown races at the root. The cost is an "intentional leak"; the OS reclaims the memory when the process exits. For a long-running process like a browser, that leak is rounding error. In embedded work or a short-lived tool you'd have to weigh it yourself.

Second, pair it with magic statics. NoDestructor is usually wrapped in a function-local static, leaning on C++11 magic statics for thread-safe first-construction.

Put the two together: `static const NoDestructor<T> x(args...);` on one line gives you a thread-safely-constructed, never-destroyed global singleton. The global-ctor gate is sidestepped by deferring construction through the local static; the global-dtor gate is sidestepped by NoDestructor not destructing. This is exactly the pattern the Chromium style guide blesses.

### Usage example

```cpp
#include "base/no_destructor.h"

const std::string& GetDefaultText() {
    static const base::NoDestructor<std::string> s("Hello world!");
    return *s;
}
```

`*s` goes through `operator*` and returns `std::string&`. `s` is a NoDestructor: on the first call it constructs the string (thread-safely), and at program exit it doesn't destruct; the memory goes back to the OS.

If the init is more involved, dropping a lambda in as an IIFE works well, say for a lazily generated random nonce:

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

Here's the target API, aligned with Chromium:

```cpp
namespace tamcpp::chrome {

template <typename T>
class NoDestructor {
public:
    // construct from arbitrary arguments (forwarded to T's constructor)
    template <typename... Args>
    explicit NoDestructor(Args&&... args);

    // copy/move construct directly from T (handy for initializer_list, etc.)
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

Usage is direct: hold it as a function-local static, and treat `*nd` or `nd->` as a T.

---

## A few signature decisions

The signature looks simple, but every line is something Chromium nailed down after stepping on a real bug. Let's pull apart the parts worth talking about.

**Why delete the copy operations.** NoDestructor holds an inline buffer `alignas(T) char storage_[sizeof(T)]`, not a pointer. If we allowed copying, we'd have to deep-copy the T inside `storage_` (placement-new a fresh one), and the semantics get muddy fast. T isn't necessarily trivially copyable: a shallow copy is a byte move, a deep copy has to go through a constructor, and which one are we doing? Rather than let users fall into that trap, just `delete` copy and keep the type in its lane as a "static-variable container," not a value type to pass around.

**Why not inherit from T, and why not expose the internal T& directly.** Inheritance drags NoDestructor into an is-a relationship with T, but it's really a container, and the semantics are wrong; besides, T might be `final`, and then inheritance doesn't even compile. Exposing the inner T& as a member is just as bad, since it leaks the `storage_` detail. Chromium borrows the smart-pointer idiom: `operator*`, `operator->`, `get()`, so NoDestructor behaves like "a pointer to T." That way `static const NoDestructor<std::string> s(...)` reads almost as naturally as a `std::string*`.

**`~NoDestructor() = default`: this is the load-bearing line.** It looks unremarkable, but it's the root of the whole design. `= default` has the compiler generate a destructor that destroys the members, and `storage_` is a char array, trivially destructible, so it does nothing. After `~NoDestructor()` runs, `~T()` is never invoked. T was placed via placement new, so its destructor would have to be called by hand, and here we deliberately don't. The first time we read this it stopped us short: why not `= delete`? The answer is that `= delete` blocks the whole object's lifecycle management; NoDestructor couldn't be used as a member or a base. `= default` lets NoDestructor itself live and die normally, while the T inside plays dead.

If we wanted to be tedious about it, we could write `~NoDestructor() { reinterpret_cast<T*>(storage_)->~T(); }`, and then we'd have a Meyers singleton all over again, with shutdown races back intact and the whole tool pointless. So "don't call `~T()`" isn't an oversight; it's the deliberate core choice.

**Why not the `[[clang::no_destroy]]` attribute.** Clang does have such an attribute; mark a variable with it and its destructor doesn't run:

```cpp
[[clang::no_destroy]] static const std::string s = "...";   // no destructor
```

We wondered the same thing: if the attribute does the job, why wrap it in a class? After reading Chromium's comments, the accounting makes sense. The attribute is Clang-only; it doesn't port to GCC or MSVC, which kills portability. It also only handles the one job of skipping destruction; it can't add `static_assert` gating or catch misuse on trivial types. Worse, the LSan-compatibility hack (see [04-4]) and the type-safe API facade both need to live inside a class. The attribute is lower-level and lighter, but industrial code is safer with a packaged tool, and easier to debug when something goes wrong.

---

## Where the teaching version diverges from Chromium

Like the earlier series, the teaching version keeps only the core mechanism: placement new + no destructor + magic statics integration + `static_assert` gating. The industrial-grade extras get stripped:

| Dimension | Chromium | Teaching version |
|---|---|---|
| Storage and placement new | full | same |
| `~NoDestructor()=default` to skip destruction | full | same |
| static_assert gating | 2 (trivial ctor+dtor / trivial dtor) | same |
| LSan reachability hack | `#ifdef LEAK_SANITIZER` holds storage_ptr_ | omitted or noted (see 04-4) |
| Chromium macro `BASE_EXPORT` | yes | omitted |

The core mechanism is identical. The LSan-compatibility piece involves sanitizer dark arts, and we pull it out to the 04-4 piece so it doesn't distract from the main line here.

---

## Get the environment ready

NoDestructor itself only needs C++17 (`alignas` / `std::forward`). Some of the `static_assert`s read more cleanly with C++20's `_v` variable templates, but C++17 works too. We use C++20, matching the earlier series.

### Compiler requirements

GCC 11+ or Clang 12+, with `-std=c++20`.

### Verification code

```cpp
#include <new>
#include <type_traits>

// verify alignas + placement new work
struct Foo { int x; };
alignas(Foo) char buf[sizeof(Foo)];

int main() {
    new (buf) Foo{42};                                   // placement new constructs on buf
    return reinterpret_cast<Foo*>(buf)->x - 42;          // 0, verifies access works
}
```

If this builds and runs, the environment is ready. The companion project lives in `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/`; starting from 04-2 we add the `23` through `25` batch of NoDestructor samples.

---

## References

- [Chromium `base/no_destructor.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [Clang `[[clang::no_destroy]]` attribute](https://clang.llvm.org/docs/AttributeReference.html#no-destroy)
- [isocpp FAQ — Meyers singleton and the shutdown problem](https://isocpp.org/wiki/faq/ctors#construct-on-first-use)
- [NoDestructor prerequisite (0): static storage duration, initialization, and destruction](./pre-00-static-storage-and-init.md)
