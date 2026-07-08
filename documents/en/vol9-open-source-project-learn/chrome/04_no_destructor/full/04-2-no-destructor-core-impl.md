---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: "Build NoDestructor's core: an alignas storage_ buffer, placement-new construction, reinterpret_cast access, a =default destructor that skips ~T(), and two static_assert gates"
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'NoDestructor hands-on (I): motivation and API design'
- 'NoDestructor Prerequisite (I): placement new and aligned storage'
reading_time_minutes: 10
related:
- 'NoDestructor hands-on (III): when to use, when not to'
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- RAII
title: "NoDestructor hands-on (II): the core implementation"
---
# NoDestructor hands-on (II): the core implementation

In the [previous piece](./04-1-no-destructor-motivation-and-api.md) we pinned down NoDestructor's target API. Now we build it. The whole thing rests on the two old friends from [prerequisite (I)](./pre-01-placement-new-and-aligned-storage.md): placement new and aligned storage. Stack one deliberate "don't destruct" policy on top, add two `static_assert` gates, and you're done. The class is under 50 lines, but every line earns its keep, and we'll walk through the reasoning for each. It looks short. The sharp edges are not.

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

The entire state of the class is this one line: `alignas(T) char storage_[sizeof(T)]` (no_destructor.h:122). First time we read it, we paused. A char array, and that's it? That really is it. `sizeof(T)` bytes, exactly enough to hold one T. The `alignas(T)` out front lifts the array's alignment up to T's requirement; without it, placement new could hand back an address that isn't aligned for T, which is straight undefined behavior. The member is private, so nobody on the outside can touch the raw storage. They have to go through `get()`, `operator*`, and friends below.

That aligned inline buffer is the whole of NoDestructor's "data." No pointer, no heap allocation, no extra overhead. Everything else piles onto this foundation.

## Construction: placement new with perfect forwarding

```cpp
public:
    // Generic: perfect-forward any arguments to T's constructor
    template <typename... Args>
    explicit NoDestructor(Args&&... args) {
        new (storage_) T(std::forward<Args>(args)...);
    }

    // Construct from an existing T (for initializer_list and friends)
    explicit NoDestructor(const T& x) { new (storage_) T(x); }
    explicit NoDestructor(T&& x)      { new (storage_) T(std::move(x)); }
```

The generic constructor needs no long explanation: `template<typename... Args>` plus `std::forward<Args>(args)...` is textbook perfect forwarding, passing the arguments untouched to T's constructor. `new (storage_) T(...)` is placement new; it constructs a T in place over the `storage_` memory. No allocation happens. The memory is `storage_` itself.

The interesting part is those two seemingly redundant `const T&` and `T&&` overloads below. Your first reaction, like ours, is probably: the generic template already covers everything, why spell these out? The trap is `initializer_list`. Some initialization paths construct from an existing T, and the generic template's perfect forwarding can fail to match T's copy or move constructor, or produce ambiguity. Chromium writes these two overloads explicitly to keep `NoDestructor<std::vector<int>> v({1,2,3})` routing correctly into vector's `initializer_list` constructor. Skip this and the compiler error at the call site will eat an afternoon.

## Access: the legality of reinterpret_cast

```cpp
    const T& operator*()  const { return *get(); }
    T&       operator*()        { return *get(); }
    const T* operator->() const { return get(); }
    T*       operator->()       { return get(); }

    const T* get() const { return reinterpret_cast<const T*>(storage_); }
    T*       get()       { return reinterpret_cast<T*>(storage_); }
```

That `reinterpret_cast<T*>(storage_)` inside `get()` made us nervous the first time. Casting a char array's address to `T*`? Is that even legal? It is (no_destructor.h:118-119). The precondition is that placement new has already constructed a real T object in that memory. After that point the memory is being used as a T object, and a pointer to it cast to `T*` is safe. Put another way, legality rides on the "construct first, access second" order. If you cast without the new first, that's a different story.

`operator*` and `operator->` both forward to `get()`, smart-pointer style. The const overloads return `const T*` / `const T&`; the non-const ones return a mutable reference. That mirrors `std::unique_ptr`'s interface. In practice `NoDestructor<T>` behaves like a `T*`:

```cpp
static const NoDestructor<std::string> s("hi");
s->size();    // operator->
(*s)[0];      // operator*
s.get();      // explicit pointer
```

## Not destructing: =default hides the trick

```cpp
    NoDestructor(const NoDestructor&) = delete;
    NoDestructor& operator=(const NoDestructor&) = delete;

    ~NoDestructor() = default;   // <- the key: doesn't call ~T()!
```

Copy is deleted; the rationale is in [04-1](./04-1-no-destructor-motivation-and-api.md) and we won't repeat it. The load-bearing line is `~NoDestructor() = default`. The whole design hangs on it.

First time reading that line, we were confused. `= default` means "let the compiler generate the default destructor," so how does that skip `~T()`? It clicked once we matched it against the member list. The compiler-generated default destructor destroys the **members**, and NoDestructor's only member is `char storage_[sizeof(T)]`. A char array has a trivial destructor that does nothing. The compiler has no reason to treat `storage_` as a T and destruct it: T is something that "grows onto" `storage_` after placement new, and as far as the type system is concerned the two have nothing to do with each other. By the time `~NoDestructor()` finishes, `~T()` has not been called once. T just sits there in memory, alive, until the process exits and the OS reclaims the whole address space as ordinary process memory.

That is the implementation root of "don't destruct." One sentence: make NoDestructor's destructor see only the char member, never T.

---

## static_assert gates: using the type system to block misuse

```cpp
private:
    static_assert(!(std::is_trivially_constructible_v<T> &&
                    std::is_trivially_destructible_v<T>),
                  "T is trivially ctble+dtble: use constinit T directly, not NoDestructor");

    static_assert(!std::is_trivially_destructible_v<T>,
                  "T is trivially destructible: use a plain function-local static T, not NoDestructor");

    alignas(T) char storage_[sizeof(T)];
};
```

These two `static_assert`s (no_destructor.h:85-93) are, in our view, the most considerate touch in the whole design. They lock NoDestructor onto the case where it belongs: **only non-trivially-destructible T**. Let's take the cases one at a time.

First case. T is trivially constructible and trivially destructible. Think `int`, or a POD struct. You just write `static constexpr T x = ...;` or `constinit T x;` and move on; NoDestructor is not needed. If you force it on anyway, the first assertion stops you at compile time.

Second case. T is trivially destructible but non-trivially constructible, say some class with a non-trivial constructor. This isn't NoDestructor's business either. A function-local `static T x;` is enough; the destructor is trivial, so it generates no global destructor. The second assertion catches you here.

Third case is where NoDestructor actually earns its keep. T is non-trivially destructible: `std::string`, `std::vector`, `std::map`. Neither assertion fires, and the tool fits.

What we appreciate about this design is that the assertions don't just block, they tell you how to fix it in the error message. The day you slip and write `NoDestructor<int>`, the compiler hands you a clear instruction at compile time, instead of leaving a latent issue to surface in production six months later.

---

## Full implementation: five layers, run end to end

Stitched together, the header looks like this:

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

Here is a small verification program. Let's run it and see what comes out:

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
    // Program exit: ~NoDestructor runs (trivial); ~string and ~Noisy do not
    return 0;
}
```

The terminal prints `chromium` and `42`, and **`~Noisy()` never shows up**. The non-destruction is working.

That wraps the implementation. Five layers, recapped: the aligned `storage_` buffer; placement new with perfect forwarding; `reinterpret_cast<T*>` access through `get` / `operator*` / `operator->`; the `= default` destructor that skips `~T()` and destroys only the char member; and two `static_assert` gates that keep the tool aimed at non-trivially-destructible T. Under 50 lines, and we've been through the reasoning for each.

Implementation is one thing. Where it actually bites is a different question: when you should reach for it, and when you absolutely should not. That's the rich ground for NoDestructor misuse, and it's what the next piece breaks down.

## References

- [Chromium `base/no_destructor.h` full source](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [cppreference: placement new](https://en.cppreference.com/w/cpp/language/new#Placement_new)
- [cppreference: is_trivially_destructible](https://en.cppreference.com/w/cpp/types/is_destructible)
- [NoDestructor hands-on (I): motivation and API design](./04-1-no-destructor-motivation-and-api.md)
