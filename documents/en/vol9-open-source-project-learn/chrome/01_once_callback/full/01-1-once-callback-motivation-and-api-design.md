---
chapter: 1
cpp_standard:
- 23
description: Starting from a real async-callback bug, dissecting the three flaws of std::function in async scenarios, and designing the complete target API of OnceCallback
difficulty: beginner
order: 1
platform: host
prerequisites:
- 'OnceCallback prerequisite (I): function types and template partial specialization'
- 'OnceCallback prerequisite (V): std::move_only_function (C++23)'
- 'OnceCallback prerequisite (VI): Deducing this (C++23)'
reading_time_minutes: 10
related:
- 'OnceCallback hands-on (II): building the core skeleton'
- 'OnceCallback prerequisite cheat sheet: a review of C++11/14/17 core features'
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
title: "OnceCallback hands-on (I): motivation and API design"
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/01-1-once-callback-motivation-and-api-design.md
  source_hash: 299c9940030a55a4d33b7125e48f0e0f9f1c6e29ee3402fe889fd624368d9235
  translated_at: '2026-09-26T00:27:54+00:00'
  engine: anthropic
  token_count: 6800
---
# OnceCallback hands-on (I): motivation and API design

## It all starts with a bug

The pit we've fallen into most often in async programming is called "the callback fired one time too many." The scenario is embarrassingly ordinary: you wrap an asynchronous file read, register a callback for I/O completion, and expect it to run once and be done with it. Then some error-retry path slips and triggers it an extra time, `release_resources()` inside the callback runs a second round, that second pass touches memory that was already freed, and segfault. The truly nasty part is that this almost never reproduces in tests — every normal async path fires the callback exactly once; the real fuse is some race or retry that only surfaces under production-level concurrency with vanishingly low probability. The first time we hit this pit, we stared at the dump for half an afternoon before realizing the logic wasn't wrong — nobody was keeping count of how many times the thing got called.

`std::function` can't help in this scenario. It can be invoked any number of times, and it can be copied to every corner of the program — if you want to keep the callback object on a leash, no chance. What we want is to weld the rule "callable exactly once" into the type system and let the compiler enforce it, instead of betting on every callback author having a good memory. This piece thinks the motivation and the interface through; the next one starts writing code.

### The scenario: an asynchronous file read

Suppose we're writing an async file-read wrapper. The user calls `read_file_async(path, callback)`; once the I/O completes, `callback` fires once, receiving the file contents.

```cpp
void read_file_async(const std::string& path,
                     std::function<void(std::string)> callback);

// Usage
void on_file_read(std::string content) {
    process(content);        // process the contents
    release_resources();     // release the associated resources
}

read_file_async("data.txt", on_file_read);
```

Harmless at a glance. But the moment the I/O subsystem retries on some error — the callback fires twice, `release_resources()` runs two rounds, and the second round touches memory that's already freed. Segfault. That retry path never executes in tests; the bug only surfaces under production-level concurrency with vanishingly low probability.

### Why std::function doesn't save us

Where's the problem? The type signature `std::function<void(std::string)>` carries zero information about how many times this callback should be invoked. The type system is simply absent here; the constraint lives entirely in runtime assertions — provided you wrote any — or in programmer discipline.

Worse, several properties of `std::function` push the bug further underground. It's copyable, so the callback can be replicated to any number of places; the day multiple execution paths each hold a copy of it at the same time, a race is already planted there. And its `operator()` is `const`-qualified — invoking it doesn't mutate the object's own state — so the "invocation is consumption" semantics can't even be expressed through the calling interface.

---

## The three fatal flaws of std::function

Let's systematize the problem. As a general-purpose callable container, `std::function` is a successful design — we won't deny that. But jammed into the specific scenario of async callbacks, it has three fatal spots.

Flaw one: copyability. `std::function` supports copying natively; copy one over, and its internal type-erasure machinery copies the stored callable along with it. What does that mean in an async system? One callback can be replicated to any number of places — one copy in the task queue, one in a timer, yet another in an error handler — and each copy can be invoked independently. If the callback captures a move-only resource (say, a `std::unique_ptr`), the copy fails to compile outright; if it captures raw pointers or references, multiple copies running at once is a race. The Chrome team's line of thinking is blunt: async task callbacks should never be copied in the first place, so make them uncopyable at the type level.

Flaw two: repeated invocability. `std::function::operator()` puts zero restraint on call count — invoke the same object a thousand times and it will run every single time. Yet in async callback scenarios, the file-read-completed callback being invoked twice is a full-blown logic error: two resource releases, two state transitions, two messages sent — whichever flavor of breakage you fancy. The type system cannot catch this kind of bug at all.

Flaw three, and the most insidious: no way to express consumption semantics. In Chrome's task-posting model, after one `PostTask(FROM_HERE, callback)`, `callback` should never be touched again — its ownership has already been handed over to the task system. But `std::function::operator()` is `const`-qualified and invoking it doesn't change the object's state, so the "invocation is consumption" semantics can't be hung on the interface at all.

All three flaws poke the same spot: `std::function`'s interface simply cannot express the constraint "this callback may be invoked exactly once, and it's spent after that." Our OnceCallback exists precisely to fill that gap.

---

## Chromium's answer: the design philosophy of OnceCallback

Chrome's callback system rests on one core principle: message passing over locks, serialization over threads. Following that line, every callback posted to the task system is an independent, one-shot message. Once posted, ownership of the callback moves from the caller to the task system; once executed, the callback is destroyed. No sharing, no reuse, no ambiguity.

This philosophy is carved directly into `OnceCallback`'s type design. First, move-only: `OnceCallback` deletes both the copy constructor and copy assignment, keeping only move operations, so at the type level a callback has exactly one owner at any moment. Second, an rvalue-qualified `Run()` — it can only be invoked on an rvalue; calling it on an lvalue is a compile error on the spot, which amounts to the syntax yanking the caller's sleeve: "you are consuming this callback; don't touch it afterwards." Third, single consumption: internally, `Run()` destroys the `BindState` through a reference-counting mechanism, so any access to the same object afterwards is a safe no-op. Add the three together, and "callable exactly once" turns from a discipline problem into a type problem.

### An overview of Chromium's internal architecture

Chromium's callback system stacks three layers. At the bottom sits `BindStateBase`, the type-erased base class carrying the reference count — it skips the virtual-function route and does polymorphism through function-pointer members instead. The middle layer is `BindState<Functor, BoundArgs...>`, the templated concrete class where the actual callable and the bound arguments live. On top is `OnceCallback<Signature>`, the one users touch directly — essentially a `BindState` wearing a smart-pointer shell, weighing in at just 8 bytes.

Our implementation keeps the layered skeleton of "outer interface + internal storage + type erasure," but the places we lay hands on are two: `std::move_only_function` replaces Chromium's hand-rolled `BindState` + reference-count combination, and deducing this replaces the double-overload-plus-`!sizeof` hack. Plainly put, the grunt work of the old generation is now done by modern syntax.

---

## Designing the target API

Engineer's rule: lay out "what I want" first, then come back and interrogate the why behind each decision. So let's pin down the target API right now.

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

// cb is consumed after the call
// std::move(cb).run(1, 2);  // runtime assertion failure
```

### Argument binding

```cpp
// bind_once: pre-binds some arguments and returns a new OnceCallback
auto bound = bind_once<int(int)>(
    [](int x, int y, int z) { return x + y + z; },
    10, 20  // pre-bind the first two arguments
);

int r = std::move(bound).run(30);  // r == 60
```

### Cancellation checks

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

## Analyzing the interface design decisions

### Why run() instead of operator()

Chromium spells it `Run()` — Google style demands the leading capital. We use `run()`, aligning with the snake_case convention. One level deeper, this is really about semantic separation: `operator()` is too generic — anything callable has an `operator()`; the name `run()` itself shouts "I'm executing a task," so during code review you can tell at a glance that a OnceCallback is being consumed here, not some ordinary function casually called.

### Why run() must be invoked through an rvalue

This is the spot we care about most in the whole design. We use deducing this to make the compiler intercept lvalue calls for us — write `cb.run(args)` instead of `std::move(cb).run(args)` and the compiler errors out on the spot, with an error message that plainly tells you how to fix it. This machinery was covered in prerequisite (VI), so we won't repeat it here.

### Why is_cancelled() and maybe_valid() are distinct

The difference lies in the strength of the safety guarantee. `is_cancelled()` gives a definitive answer: it may only be called on the sequence the callback was bound to, and it guarantees an accurate result. `maybe_valid()` takes the optimistic route: callable from any thread, but the answer may already be stale. In Chromium's full implementation, this distinction is tied directly to the thread-safety guarantees. Our simplified version temporarily makes the two semantically identical — we keep the interface in place and split them later if a real need shows up.

### Why then() consumes *this

What `then()` wants to express is "hand the current callback's execution result to the next callback." That requires the current callback to be fully swallowed into the new callback `then()` returns. If `then()` didn't consume `*this`, the same callback would be sitting in two places at once — the move-only semantics would break on the spot. So `then()` is declared as an rvalue-qualified member function; once called, the original callback enters the consumed state.

---

## Setting up the environment

Before touching code, let's take stock of the toolchain. OnceCallback depends on `std::move_only_function` and deducing this — both C++23 features; with the environment incomplete, everything afterwards is wasted effort.

### Compiler requirements

GCC 14+ or Clang 18+ fully support the feature set above; compile with `-std=c++23`.

### Verification code

```cpp
#include <functional>

// Verify that std::move_only_function is available
static_assert(__cpp_lib_move_only_function >= 202110L);

// Verify that deducing this is available
struct Check {
    void test(this auto&& self) {}
};

int main() {
    Check c;
    c.test();
    return 0;
}
```

If this code compiles, the environment is ready.

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

With this, the motivation and the interface have taken shape. The next piece gets our hands dirty — from template partial specialization to three-state management, we'll raise OnceCallback's class skeleton beam by beam.

## References

- [Chromium Callback documentation](https://chromium.googlesource.com/chromium/src/+/main/docs/callback.md)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - the Deducing this proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
