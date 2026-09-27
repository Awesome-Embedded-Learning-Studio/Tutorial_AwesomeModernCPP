---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: "Implement the NoDestructor core: an alignas storage_ buffer, placement-new construction, reinterpret_cast access, a =default destructor that skips ~T(), plus static_assert gating"
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'NoDestructor hands-on (I): motivation and API design'
- 'NoDestructor prerequisite (I): placement new and aligned storage'
reading_time_minutes: 10
related:
- 'NoDestructor hands-on (III): when to use it, and when not to'
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- RAII
title: "NoDestructor hands-on (II): the core implementation"
translation:
  source: documents/vol9-open-source-project-learn/chrome/04_no_destructor/full/04-2-no-destructor-core-impl.md
  source_hash: 4cf17826519ba66c507cadb9fec7e2c1687b7025b61229de3aa792488bbd15fb
  translated_at: '2026-09-26T03:16:13+00:00'
  engine: anthropic
  token_count: 3400
---
# NoDestructor hands-on (II): the core implementation

Last time we hammered out NoDestructor's target API. Now we roll up our sleeves and build the implementation — and we'll say it straight away: the core is exactly the two old friends from [prerequisite (I)](./pre-01-placement-new-and-aligned-storage.md), placement new plus aligned storage. Layer on a deliberate "don't destruct" policy, add two static_asserts standing guard, and it's complete. The whole class is under 50 lines all told, yet we owe you the reasoning behind every single line — short as it looks, not one pitfall is missing.

## storage_: an aligned inline buffer

```cpp
// Platform: host | C++ Standard: C++20
#pragma once
#include <new>
#include <type_traits>
#include <utility>

namespace tamcpp::chrome {

template <typename T>
class NoDestructor {
    // detailed below
private:
    alignas(T) char storage_[sizeof(T)];
};

}  // namespace tamcpp::chrome
```

The entire state of the class is this one line: `alignas(T) char storage_[sizeof(T)]` (no_destructor.h:122). The first time we read it, we did a double take — a char array, and that's it? That really is it. `sizeof(T)` bytes, exactly enough room to hold one T; the `alignas(T)` out front lifts the array's alignment up to T's tier, because otherwise the address placement new hands you might be under-aligned, and you'd slam straight into undefined behavior. It's private, so nobody outside can touch the raw storage — they have to behave and go through the `get()` and `operator*` overloads below.

Stripped to its essence, NoDestructor's entire "data" is this one aligned inline buffer. No pointers, no heap allocation, no extra overhead. Every trick that follows is built on this foundation.

## Construction: placement new plus perfect forwarding

```cpp
public:
    // Generic: perfectly forwards arbitrary arguments to T's constructor
    template <typename... Args>
    explicit NoDestructor(Args&&... args) {
        new (storage_) T(std::forward<Args>(args)...);
    }

    // Copy/move-construct directly from a T (handy for initializer_list and friends)
    explicit NoDestructor(const T& x) { new (storage_) T(x); }
    explicit NoDestructor(T&& x)      { new (storage_) T(std::move(x)); }
```

The generic constructor needs little explanation: `template<typename... Args>` plus `std::forward<Args>(args)...` is textbook perfect forwarding, handing the arguments untouched to T's constructor. `new (storage_) T(...)` is placement new — it constructs a T in place on the `storage_` memory. Nothing is allocated; the memory is `storage_` itself.

What we do want to dwell on is the two seemingly redundant `const T&` / `T&&` overloads below. You may be puzzled the same way we were at first: doesn't the generic template already cover everything? Why spell these two out? The trap is initializer_list. Some initialization sites have to construct from an existing T, and there the generic template's perfect forwarding may fail to match T's copy/move constructor, or turn outright ambiguous. Chromium makes these two overloads explicit precisely so that spellings like `NoDestructor<std::vector<int>> v({1,2,3})` reach vector's initializer_list constructor correctly. Skip this step, and the day you actually need it, the compiler errors will keep you digging for a good while.

## Access: the legality of reinterpret_cast

```cpp
    const T& operator*()  const { return *get(); }
    T&       operator*()        { return *get(); }
    const T* operator->() const { return get(); }
    T*       operator->()       { return get(); }

    const T* get() const { return reinterpret_cast<const T*>(storage_); }
    T*       get()       { return reinterpret_cast<T*>(storage_); }
```

That `reinterpret_cast<T*>(storage_)` inside `get()` made our heart skip a beat on first reading — hard-casting a char array's address to `T*`, how can that be legal? It can (no_destructor.h:118-119). The precondition is that placement new has already genuinely constructed a T in that memory: from that point on, the memory is being used as a T object, and converting a pointer to it into a `T*` is safe. Put differently, the legality rests on the "construct first, access later" ordering — if you cast without having new'd, that's another story entirely.

`operator*` and `operator->` both forward to `get()`, smart-pointer style. The const versions yield `const T*` / `const T&`, the non-const versions yield mutable references — an interface aligned with `std::unique_ptr`'s. In use, a `NoDestructor<T>` behaves essentially like a `T*`:

```cpp
static const NoDestructor<std::string> s("hi");
s->size();    // operator->
(*s)[0];      // operator*
s.get();      // get the pointer explicitly
```

## Not destructing: the trick hidden in =default

```cpp
    NoDestructor(const NoDestructor&) = delete;
    NoDestructor& operator=(const NoDestructor&) = delete;

    ~NoDestructor() = default;   // ← the key move: ~T() is never called!
```

Copying is deleted, for reasons already covered in [04-1](./04-1-no-destructor-motivation-and-api.md) — we won't repeat them here. The load-bearing line is the one below, `~NoDestructor() = default`; the entire design's lifeline rests right there.

The first time we read that line, we were baffled: isn't `= default` just "let the compiler generate the default destructor"? How does that skip `~T()`? It clicked once we lined it up against the member list. The compiler-generated default destructor destroys the **members** — and NoDestructor's only member is `char storage_[sizeof(T)]`. A char array's destruction is trivial and does nothing. Nor will the compiler take it upon itself to destroy `storage_` as a T — T only "grew" onto `storage_` later, via placement new, and its "identity" in the type system has nothing to do with `storage_`'s. By the time `~NoDestructor()` finishes, `~T()` has not been called even once; T just stays "alive" in memory, living all the way to program exit, where the OS sweeps it up as ordinary process memory.

That is the implementation root of "not destructing", and boiled down it's one sentence: let NoDestructor's destructor see only the char member, never T.

---

## static_assert gating: using the type system to block misuse

```cpp
private:
    static_assert(!(std::is_trivially_constructible_v<T> &&
                    std::is_trivially_destructible_v<T>),
                  "T 平凡可构造且平凡析构:请直接用 constinit T,不需要 NoDestructor");

    static_assert(!std::is_trivially_destructible_v<T>,
                  "T 平凡析构:请直接用函数局部静态 T,不需要 NoDestructor");

    alignas(T) char storage_[sizeof(T)];
};
```

These two static_asserts (no_destructor.h:85-93) are, in our view, the most considerate touch in the whole design — they lock NoDestructor down to "the scenarios where it belongs": **serving only T with non-trivial destruction**. Let's take the cases one at a time.

First case: T is both trivially constructible and trivially destructible — `int`, POD structs, that crowd. For these you just write `static constexpr T x = ...;` or `constinit T x;` and be done with it; NoDestructor is not needed at all. Force it on anyway, and the first assertion stops you on the spot.

Next case: T is trivially destructible but non-trivially constructible — some classes with non-trivial constructors, say. Not NoDestructor's business either: a function-local `static T x;` suffices, since destruction is trivial and generates no global destructor. Use NoDestructor in this situation, and the second assertion blocks you.

Only the last case is where NoDestructor truly earns its keep: T with non-trivial destruction, like `std::string`, `std::vector`, `std::map`. Neither assertion fires, and it is the right tool for the job.

What we especially appreciate about this design: it doesn't just block you — the error message tells you on the spot how to fix it. The day you slip and write `NoDestructor<int>`, you take a clear diagnostic at compile time, instead of leaving a latent hazard running in production for half a year before it bites.

---

## The complete implementation: five layers run together

Assemble the five layers from above, and the complete header is this:

```cpp
// no_destructor.hpp
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
        new (storage_) T(std::forward<Args>(args)...);
    }
    explicit NoDestructor(const T& x) { new (storage_) T(x); }
    explicit NoDestructor(T&& x)      { new (storage_) T(std::move(x)); }

    NoDestructor(const NoDestructor&) = delete;
    NoDestructor& operator=(const NoDestructor&) = delete;
    ~NoDestructor() = default;

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
};

}  // namespace tamcpp::chrome
```

Here is a verification snippet — let's take it to the machine and see what happens:

```cpp
#include <iostream>
#include <string>
#include "no_destructor.hpp"

struct Noisy {
    Noisy(int x) : v(x) { std::puts("Noisy()"); }
    ~Noisy() { std::puts("~Noisy()"); }   // never printed
    int v;
};

const std::string& DefaultName() {
    static const tamcpp::chrome::NoDestructor<std::string> s("chromium");
    return *s;
}

int main() {
    std::cout << DefaultName() << "\n";          // chromium
    static const tamcpp::chrome::NoDestructor<Noisy> n(42);
    std::cout << n->v << "\n";                   // 42
    // Program exit: ~NoDestructor runs (trivial); ~string and ~Noisy never run
    return 0;
}
```

The terminal prints `chromium` and `42`, but **the `~Noisy()` line never appears from start to finish** — the no-destruction policy is working.

At this point NoDestructor's implementation is fully built. Five layers in total — let's run through them once more: the aligned `storage_` buffer; construction via placement new plus perfect forwarding; access through `reinterpret_cast<T*>` (`get` / `operator*` / `->`); the `= default` move that skips `~T()` and destroys only the char member; and finally the two static_asserts standing guard, pinning the tool to T with non-trivial destruction. The whole class is under 50 lines, and we have walked through the reasoning for every one of them.

Implementation is one thing; what actually wrecks projects is a different matter: when to use it, and when never to. That is the richest seam of NoDestructor misuse, and the next piece is dedicated entirely to picking it apart.

## References

- [The complete Chromium `base/no_destructor.h` source](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [cppreference: placement new](https://en.cppreference.com/w/cpp/language/new#Placement_new)
- [cppreference: is_trivially_destructible](https://en.cppreference.com/w/cpp/types/is_destructible)
- [NoDestructor hands-on (I): motivation and API design](./04-1-no-destructor-motivation-and-api.md)
