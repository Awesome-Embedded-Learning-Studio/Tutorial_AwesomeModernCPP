---
chapter: 1
cpp_standard:
- 23
description: "Start from a real async-callback bug, dissect the three flaws std::function has in async settings, and lay out the full target API for OnceCallback"
difficulty: beginner
order: 1
platform: host
prerequisites:
- 'OnceCallback prerequisites (I): function types and template partial specialization'
- 'OnceCallback prerequisites (V): std::move_only_function'
- 'OnceCallback prerequisites (VI): Deducing this'
reading_time_minutes: 10
related:
- 'OnceCallback in practice (II): the core skeleton'
- 'OnceCallback prerequisite cheat sheet: a recap of C++11/14/17 core features'
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
title: 'OnceCallback in practice (I): motivation and API design'
---
# OnceCallback in practice (I): motivation and API design

## Starting from a bug

The bug that's bitten me the most times in async programming has a name: "the callback got called one extra time." The setup is mundane. You wrap an async file read, register a callback for I/O completion, and expect it to fire once and be done. Then some error-retry path slips up and triggers it a second time. Inside the callback `release_resources()` runs again, and on the second pass it touches memory that's already been freed. Segfault. The nasty part is that this almost never reproduces in tests, because the normal async path fires the callback exactly once. The real fuse is some race or retry that only shows up under production-level concurrency at a low probability. The first time I hit this I stared at a core dump for half an afternoon before realizing it wasn't a logic error at all. Nobody was checking the call count.

`std::function` is no help here. It can be called any number of times, and it can be copied all over the place, so there's no way to constrain the callback object. What we want is to weld the "called exactly once" rule into the type system and let the compiler enforce it, rather than relying on every person who writes a callback to remember. This piece works through the motivation and the interface. The next one starts writing code.

### Scenario: async file read

Suppose we're writing a wrapper for async file reading. The user calls `read_file_async(path, callback)`, and when I/O finishes, `callback` fires once with the file contents.

```cpp
void read_file_async(const std::string& path,
                     std::function<void(std::string)> callback);

// Usage
void on_file_read(std::string content) {
    process(content);        // handle the contents
    release_resources();     // free the associated resources
}

read_file_async("data.txt", on_file_read);
```

Looks harmless. But the moment the I/O subsystem retries on some error, the callback fires twice, `release_resources()` runs twice, and the second run touches already-freed memory. Segfault. That retry path never gets exercised in tests, so the bug only surfaces under high concurrency in production, and only at a low probability.

### std::function doesn't help us

Where does the problem come from? The type signature `std::function<void(std::string)>` carries zero information about how many times this callback should be called. The type system is absent here; the constraint lives entirely in runtime assertions, if you wrote any, or in programmer discipline.

What makes it worse is that a few of `std::function`'s properties push the bug further out of reach. It's copyable, so the callback can be cloned to any number of places. The day two execution paths each hold a copy and run them at the same time, you've planted a race. And its `operator()` is `const`-qualified, so calling it doesn't change the object's own state, which means the "to call is to consume" semantic can't even be expressed through the call interface.

---

## Three flaws of std::function

Let's systematize this. `std::function` as a general-purpose callable container is a successful design; I'm not disputing that. But dropped into the specific scenario of async callbacks, it has three lethal spots.

The first is copyability. `std::function` supports copy natively. Copy it once and its internal type erasure copies the stored callable along with it. Inside an async system that means a single callback can be replicated to any number of places, one in the task queue, one in the timer, one in the error handler, and each copy can be invoked independently. If the callback captured a move-only resource (a `std::unique_ptr`, say), the copy fails to compile outright; if it captured a raw pointer or reference, then multiple copies running at once is a race. The Chrome team's stance is blunt: async task callbacks shouldn't be copied in the first place, so make them uncopyable at the type level.

The second is repeated callability. `std::function::operator()` imposes zero control over how many times you call it; call the same object a thousand times and it'll happily run every time. But in an async callback setting, firing a file-read completion callback twice is a hard logic error: two resource releases, two state transitions, two messages sent, take your pick. And the type system can't catch a single word of it.

The third is the sneakiest: there's no way to express consumption semantics. In Chrome's task-posting model, once you do `PostTask(FROM_HERE, callback)`, that `callback` should never be touched again, because its ownership has already been handed to the task system. But `std::function::operator()` is `const`-qualified, so calling it doesn't mutate the object, which means the "to call is to consume" semantic can't be hung off the interface at all.

All three point at the same place: the `std::function` interface simply cannot express the constraint "this callback can be called only once, and is invalid afterward." Our OnceCallback exists to fill that gap.

---

## Chromium's answer: the OnceCallback design philosophy

Chrome's callback system rests on one core principle: message passing over locks, serialization over threads. Following that line, every callback posted to the task system is an independent, one-shot message. Once posted, ownership of the callback moves from the caller to the task system; once executed, the callback is destroyed. No sharing, no reuse, no ambiguity.

That philosophy is carved straight into `OnceCallback`'s type design. First, move-only: `OnceCallback` deletes both copy construction and copy assignment and keeps only the move operations, so at the type level a callback has exactly one owner at any moment. Second, an rvalue-qualified `Run()`: it can only be called on an rvalue, and calling it on an lvalue is a hard compile error, which is the type system grabbing the caller by the collar to say "you are consuming this callback, don't touch it again." Third, single-shot consumption: inside `Run()`, a reference-counting mechanism destroys the `BindState`, so any access to the same object after the call is a safe no-op. Add the three together and "called only once" stops being a discipline problem and becomes a type problem.

### A sketch of Chromium's internals

Chromium's callback system stacks three layers. At the bottom sits `BindStateBase`, a type-erased base class carrying a reference count. It skips virtual functions and gets polymorphism through function-pointer members instead. The middle layer is `BindState<Functor, BoundArgs...>`, a templated concrete class that actually stores the callable and the bound arguments. On top is `OnceCallback<Signature>`, the type users handle directly; under the hood it's a `BindState` wrapped in a smart-pointer shell, and it's only 8 bytes.

Our implementation keeps the layered skeleton of "outer interface plus internal storage plus type erasure," but we swap two pieces: `std::move_only_function` replaces Chromium's hand-rolled `BindState` plus reference-count combo, and deducing this replaces the double-overload-plus-`!sizeof` hack. Put bluntly, the modern syntax does the heavy lifting the old generation had to do by hand.

---

## Designing the target API

An engineer's rule: get "what I want" on the table first, then come back and argue each decision. Let's pin down the target API.

### Construction and invocation

```cpp
#include "once_callback/once_callback.hpp"

using namespace tamcpp::chrome;

// Construct from a lambda
auto cb = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
});

// Invocation: must go through an rvalue
int result = std::move(cb).run(3, 4);  // result == 7

// After the call, cb has been consumed
// std::move(cb).run(1, 2);  // runtime assertion failure
```

### Argument binding

```cpp
// bind_once: pre-bind part of the arguments and return a new OnceCallback
auto bound = bind_once<int(int)>(
    [](int x, int y, int z) { return x + y + z; },
    10, 20  // pre-bind the first two arguments
);

int r = std::move(bound).run(30);  // r == 60
```

### Cancellation check

```cpp
auto cb = OnceCallback<void(int)>([](int x) { /* ... */ });

// Check whether the callback is still valid
if (!cb.is_cancelled()) {
    std::move(cb).run(42);
}

// maybe_valid: an optimistic check
if (cb.maybe_valid()) {
    std::move(cb).run(42);
}
```

### Chaining

```cpp
auto pipeline = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
}).then([](int sum) {
    return sum * 2;
});

int final_result = std::move(pipeline).run(3, 4);
// final_result == 14, because (3+4)*2 = 14
```

---

## Walking through the interface decisions

### Why run() instead of operator()

Chromium uses `Run()` because Google style mandates a leading capital. We use `run()` to stay consistent with snake_case. There's a deeper layer to it, too, which is semantic separation. `operator()` is far too generic; anything callable has an `operator()`. The name `run()` itself is announcing "I'm executing a task," so in code review you can tell at a glance that a OnceCallback is being consumed here, not just some ordinary function being called.

### Why run() has to go through an rvalue

This is the one I care about most in the whole design. We lean on deducing this and let the compiler intercept lvalue calls for us. Write `cb.run(args)` instead of `std::move(cb).run(args)` and the compiler errors out on the spot, with a message that tells you exactly how to fix it. The mechanism was covered in prerequisites (VI), so I won't repeat it here.

### Why split is_cancelled() and maybe_valid()

The difference is in how strong the safety guarantee is. `is_cancelled()` gives a definitive answer: it can only be called on the sequence the callback is bound to, and the result is guaranteed accurate. `maybe_valid()` is an optimistic estimate: callable from any thread, but the result might already be stale. In Chromium's full implementation, the split between the two is tied directly to the thread-safety guarantee. Our simplified version lets the two share the same semantics for now, but we keep the interface around so we can split them later when the system actually needs it.

### Why then() consumes *this

What `then()` is trying to say is "pipe the current callback's result into the next callback." That requires the current callback to be swallowed whole inside the new callback that `then()` returns. If `then()` didn't consume `*this`, the same callback would be sitting in two places at once, and the move-only semantic would collapse on the spot. So `then()` is declared as an rvalue-qualified member function, and once it's called the original callback enters a consumed state.

---

## Setting up the environment

Before we touch any code, get the toolchain sorted. OnceCallback depends on `std::move_only_function` and deducing this, both C++23 features, and without both of them the rest of this is wasted effort.

### Compiler requirements

GCC 13+ or Clang 17+ fully support the features above; compile with `-std=c++23`.

### Verification code

```cpp
#include <functional>

// Verify std::move_only_function is available
static_assert(__cpp_lib_move_only_function >= 202110L);

// Verify deducing this is available
struct Check {
    void test(this auto&& self) {}
};

int main() {
    Check c;
    c.test();
    return 0;
}
```

If that compiles, the environment is ready.

### Minimal CMake configuration

```cmake
cmake_minimum_required(VERSION 3.20)
project(once_callback_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(once_callback INTERFACE)
target_include_directories(once_callback INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
```

---

With the motivation and the interface taking shape here, the next piece gets our hands dirty: from template partial specialization to three-state management, we'll build up the OnceCallback class skeleton one piece at a time.

## References

- [Chromium Callback documentation](https://chromium.googlesource.com/chromium/src/+/main/docs/callback.md)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - Deducing this proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
