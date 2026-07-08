---
chapter: 1
cpp_standard:
- 23
description: "Design test cases for once_callback by invariant, compare its object size, allocation behavior, and call overhead against Chromium's original, and tally up what we traded away and what we got back."
difficulty: advanced
order: 3
platform: host
prerequisites:
- 'once_callback design guide (I): motivation and API design'
- 'once_callback design guide (II): step-by-step implementation'
reading_time_minutes: 12
related:
- 'Callback cancellation and composition patterns'
tags:
- host
- cpp-modern
- advanced
- 回调机制
- 函数对象
title: 'once_callback Design Guide (III): Test Strategy and Performance Comparison'
---
# once_callback Design Guide (III): Test Strategy and Performance Comparison

At this point the `OnceCallback` interface and its implementation are both in place. But I'm not calling it done yet. Something like this, if you don't lean on it with tests, you don't trust it yourself. So in this piece we settle the test strategy and the performance bill in one go: is it actually correct, how far is it from Chromium's original, and do we accept the gap.

## Slicing tests by invariant

How to organize the tests gave me pause at first. Grouping by feature leaks, because features are something you write for yourself: you test what you thought of while writing, so the blind spots are baked in. Switching to grouping by **invariant** felt much better. Each invariant is itself a sentence of the form "I promise this always holds," and the job of testing is to torture that sentence in every posture you can think of and see if it breaks. If it breaks, it's really wrong. If it doesn't, that whole class passes.

The test code sits on top of Catch2, with dependencies pulled in by CMake + CPM. The cases listed below correspond one-to-one with the actual code in `code/volumn_codes/vol9/chrome_design/test/test_once_callback.cpp`. If you have that file in hand you can run them case by case.

### Category A: basic invocation and return value

The most basic thing: build a callback, run it, check the return value.

```cpp
TEST_CASE("non-void return", "[once_callback]") {
    OnceCallback<int(int, int)> cb([](int a, int b) { return a + b; });
    int result = std::move(cb).run(3, 4);
    REQUIRE(result == 7);
}

TEST_CASE("void return", "[once_callback]") {
    bool called = false;
    OnceCallback<void()> cb([&called] { called = true; });
    std::move(cb).run();
    REQUIRE(called);
}
```

The void return goes down the other branch of `if constexpr (std::is_void_v<ReturnType>)`. These two cases are insurance on the compile-time branch logic.

### Category B: move semantics

This category watches two things: that the move-only constraint isn't faked open, and that moving doesn't lose state.

```cpp
TEST_CASE("move-only capture", "[once_callback]") {
    auto ptr = std::make_unique<int>(42);
    OnceCallback<int()> cb([p = std::move(ptr)] { return *p; });
    int result = std::move(cb).run();
    REQUIRE(result == 42);
}

TEST_CASE("move semantics: source becomes null", "[once_callback]") {
    OnceCallback<int()> cb([] { return 1; });
    OnceCallback<int()> cb2 = std::move(cb);
    REQUIRE(cb.is_null());

    int result = std::move(cb2).run();
    REQUIRE(result == 1);
}
```

The move-only capture case stuffs `std::make_unique<int>(42)` into the lambda. If the underlying storage quietly fell back to `std::function` instead of `std::move_only_function`, this wouldn't even compile, so the case doubles as a backstop on "did we actually use move-only." The move semantics case verifies that after move construction the source object falls back to `kEmpty` and `is_null()` reports true, while the target still runs normally.

There's a point I burned half a day circling around, and it's worth pulling out: moving only transfers ownership, it does **not** consume. The thing that actually consumes the callback is `run()`. The two look like they both "moved cb," but the semantics are completely different animals. Chromium runs the same rule on its side. `PostTask(FROM_HERE, std::move(cb))` only moves ownership into the task queue; the callback stays alive right up until it actually executes.

### Category C: the single-call constraint

Categories A and B have already walked the normal call path. Category C stares at one thing: lvalue invocation must fail to compile. We pin this constraint onto the signature with deducing this + `static_assert`, so it doesn't belong to runtime at all. If your hand slips and you write `cb.run()` instead of `std::move(cb).run()`, the compiler stops you on the spot and folds "you need std::move" into the error message. Compiles = verified, no run needed.

### Category D: argument binding

```cpp
TEST_CASE("bind_once basic", "[bind_once]") {
    auto bound = bind_once<int(int)>([](int a, int b) { return a * b; }, 5);
    int result = std::move(bound).run(8);
    REQUIRE(result == 40);
}

TEST_CASE("bind_once with member function", "[bind_once]") {
    struct Calc {
        int multiply(int a, int b) { return a * b; }
    };
    Calc calc;
    auto bound = bind_once<int(int)>(&Calc::multiply, &calc, 5);
    int result = std::move(bound).run(8);
    REQUIRE(result == 40);
}
```

`bind_once` wades through two typical scenarios: partial argument binding for an ordinary lambda, and member function binding. The member function one needs a few extra words. `&Calc::multiply` is a member function pointer, `&calc` is an object pointer, and `std::invoke` underneath unfolds it into `(calc.*multiply)(5, 8)`. The trap is here: `&calc` is a raw pointer, and `bind_once` does not manage its life or death. If `calc` destructs before the callback actually runs, `std::invoke` will follow the dangling pointer and grope through already-freed memory. Chromium keeps three layers of insurance here: `base::Unretained` explicitly declares "this pointer is safe at your own risk," `base::Owned` takes over ownership outright, and `base::WeakPtr` lets the callback auto-cancel when the object destructs. Our simplified version temporarily shoves that responsibility onto the caller, and we'll come back to collect it in the piece on cancellation tokens.

### Category E: cancellation

```cpp
TEST_CASE("is_cancelled respects cancel token", "[once_callback]") {
    auto token = std::make_shared<CancelableToken>();
    OnceCallback<void()> cb([] {});
    cb.set_token(token);

    REQUIRE_FALSE(cb.is_cancelled());
    token->invalidate();
    REQUIRE(cb.is_cancelled());
}

TEST_CASE("cancelled void callback does not execute", "[once_callback]") {
    auto token = std::make_shared<CancelableToken>();
    bool called = false;
    OnceCallback<void()> cb([&called] { called = true; });
    cb.set_token(token);
    token->invalidate();

    std::move(cb).run();
    REQUIRE_FALSE(called);
}

TEST_CASE("cancelled non-void callback throws", "[once_callback]") {
    auto token = std::make_shared<CancelableToken>();
    OnceCallback<int()> cb([] { return 1; });
    cb.set_token(token);
    token->invalidate();

    REQUIRE_THROWS_AS(std::move(cb).run(), std::bad_function_call);
}
```

The cancellation category presses on three actions: no cancellation while the token is alive; a void callback honestly does not execute after the token is invalidated; a non-void callback throws `std::bad_function_call` after the token is invalidated. The third one needs a pause to explain. We choose to throw on a cancelled non-void callback because, from the caller's perspective, it wants a return value, but in the cancelled state we have no "meaningful value" on hand to give. Hand back a default value to fool it? That's sneakier than throwing. The bug would travel onward through that fake value. Chromium plays this one harder: it fails a `CHECK` outright and detonates the program. We pick the exception purely because it's easy to catch and easy to verify inside tests. This is a teaching-version tradeoff, not a design that's objectively better.

### Category F: then composition

```cpp
TEST_CASE("then chains two callbacks", "[then]") {
    auto cb = OnceCallback<int(int)>([](int x) { return x * 2; })
                  .then([](int x) { return x + 10; });
    int result = std::move(cb).run(5);
    REQUIRE(result == 20);  // 5 * 2 + 10
}

TEST_CASE("then multi-level pipeline", "[then]") {
    auto pipeline = OnceCallback<int(int)>([](int x) { return x * 2; })
                        .then([](int x) { return x + 10; })
                        .then([](int x) { return std::to_string(x); });
    std::string result = std::move(pipeline).run(5);
    REQUIRE(result == "20");  // (5*2)+10 = "20"
}

TEST_CASE("then with void first callback", "[then]") {
    int value = 0;
    auto cb = OnceCallback<void(int)>([&value](int x) { value = x; })
                  .then([&value] { return value * 3; });
    int result = std::move(cb).run(7);
    REQUIRE(result == 21);
}
```

The three cases in the `then()` category each press on one posture: a two-level non-void pipeline, a multi-level pipeline across types, and a void-prefix callback. The multi-level pipeline case is, I think, the most telling. The number `(5*2)+10 = 20` finally gets folded into the string `"20"` by `std::to_string`. Along the way `then()` deduces the return type of each level correctly, and the type erasure done by `std::move_only_function` across several completely different lambda types doesn't collapse either. The void-prefix case specifically presses on the `if constexpr (std::is_void_v<ReturnType>)` branch: the first callback writes 7 into the external `value`, the second reads `value` out through the reference and multiplies by 3 to get 21.

### Test framework and build configuration

The test framework we picked is Catch2 v3, with dependencies auto-pulled by CPM (CMake Package Manager). The CMake config is worry-free:

```cmake
# test/CMakeLists.txt
CPMAddPackage("gh:catchorg/Catch2@3.7.1")

add_executable(test_once_callback test_once_callback.cpp)
target_link_libraries(test_once_callback PRIVATE once_callback Catch2::Catch2WithMain)
target_compile_options(test_once_callback PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_once_callback COMMAND test_once_callback)
```

I use `REQUIRE` instead of `assert` for a very practical reason. `REQUIRE` spits out the failed expression, file, and line number on error, and subsequent assertions in the same `TEST_CASE` keep running. `assert` kills the whole program the moment it fires, so you only see one error at a time. `REQUIRE_THROWS_AS` is specifically for pressing on exception type. The cancellation test leans on it to confirm what gets thrown is `std::bad_function_call`, not something else.

The way to run the tests is one line: under the `build/` directory, `cmake --build . && ctest`.

---

## The performance bill: lining up against Chromium's original

### Object size

The most visible difference is in sizeof. We write a minimal program to measure:

```cpp
#include <functional>
#include <iostream>
#include "once_callback/once_callback.hpp"

int main() {
    std::cout << "sizeof(std::function<void()>):      "
              << sizeof(std::function<void()>) << " bytes\n";
    std::cout << "sizeof(std::move_only_function<void()>): "
              << sizeof(std::move_only_function<void()>) << " bytes\n";
    // Chromium OnceCallback<void()> ≈ 8 bytes (one pointer)

    using namespace tamcpp::chrome;
    std::cout << "sizeof(OnceCallback<void()>): "
              << sizeof(OnceCallback<void()>) << " bytes\n";
    // Our OnceCallback is roughly:
    // move_only_function (32) + status (1) + token ptr (16) + padding
    // estimated 56-64 bytes
}
```

Running this on GCC gives you roughly this set of numbers: `std::function<void()>` about 32 bytes, `std::move_only_function<void()>` about 32 bytes, and our `OnceCallback<void()>` once you add in the `Status` enum and the optional `CancelableToken` pointer, about 56-64 bytes. Chromium's `OnceCallback<void()>` is just 8 bytes. One `scoped_refptr` pointing at a `BindState`, and that's it.

Where does the gap come from? At the root, it's the storage strategy. Chromium jams everything, callable object and bound arguments alike, into a heap-allocated `BindState`, and the callback object itself holds only one pointer. Our version relies on `std::move_only_function`'s SBO to inline small objects directly into the callback object. We save the heap allocation, but the price is that the object body is a notch chubbier.

### Allocation behavior

The SBO threshold of `std::move_only_function` is implementation-defined, typically around 2-3 pointers (16-24 bytes). A very light-capturing lambda, like `[x = 42]` or `[&ref]`, generally fits inside SBO and doesn't trigger a heap allocation. If the lambda drags in a load of data, say a `std::string` plus a few `int`s, then construction has to fork over one more heap allocation.

Chromium's setup is a fixed heap allocation. `new BindState<Functor, BoundArgs...>` always runs once, but **only once**, and it happens at the `BindOnce` moment. After that, moving a `OnceCallback` is just copying an 8-byte pointer, extremely light. Our version doesn't allocate for small objects (SBO catches them), but once a move is needed, the whole `std::move_only_function` (32 bytes) plus the `token_` pointer has to be carried along, clearly a notch more expensive.

Neither strategy sweeps the board. For high-frequency delivery of small callbacks (the browser is Chrome's main battleground), Chromium's setup wins. Moves are cheap, sizes are uniform and CPU-cache friendly. For low-frequency large callbacks (like one-shot initialization tasks), our setup is the better deal, one less heap allocation. Which one to pick depends on your project's frequency distribution.

### Indirect call overhead

Call overhead is flat between the two paths: both are one indirect call. `std::move_only_function::operator()` dispatches to the concrete callable object through a function pointer or vtable underneath; Chromium's `BindState::polymorphic_invoke_` is also function pointer dispatch. Under `-O2` the compiler can't inline away this layer of indirection, so the two are equivalent on the call link.

### What we gave up and what we got back

Let's settle the bill.

What we gave out is object compactness (56-64 bytes versus 8 bytes). What we got back is a clean implementation. No need to hand-roll reference counting, function pointer tables, or `TRIVIAL_ABI` annotations. The move side also paid a price (moving 32 bytes + a pointer versus copying 8 bytes), and what we got back is zero heap allocation for small objects. We also gave up reference-counted sharing: there's no way to let multiple callbacks share one `BindState`. But `OnceCallback` is exclusive semantics to begin with, and sharing is something it has no use for anyway.

This set of tradeoffs holds up in a teaching scenario, and in the vast majority of real projects. If your project really presses to Chromium's level of performance requirements, you can go straight to Chromium's source and squeeze out one more layer. The core idea has been laid out across the previous three pieces; what's left is engineering detail.

---

## Where the files live

The design, implementation, and testing of the `OnceCallback` group close out here. The full file list is below; if you want to find the corresponding code, just follow the map.

```text
documents/vol9-open-source-project-learn/chrome/hands_on/
├── 01-once-callback-design.md           # Design: motivation and API
├── 02-once-callback-implementation.md   # Implementation: step by step
└── 03-once-callback-testing.md          # Verification: tests and performance
```

The corresponding compilable code (header + tests) lives under the project code directory:

```text
code/volumn_codes/vol9/chrome_design/
├── CMakeLists.txt
├── cmake/CPM.cmake
├── cancel_token/
│   └── cancel_token.hpp                 # cancellation token
├── once_callback/
│   ├── CMakeLists.txt
│   ├── once_callback.hpp                # main interface (template declarations)
│   └── once_callback_impl.hpp           # implementation (template definitions)
└── test/
    ├── CMakeLists.txt                   # Catch2 test configuration
    └── test_once_callback.cpp           # full test cases
```

---

This testing piece revolved around six invariants: basic invocation, move semantics, single invocation, argument binding, cancellation, and chaining. Out of them came 12 Catch2 cases that pin down the core behavior of `OnceCallback` pretty thoroughly. On the performance side, lined up against Chromium's original, the bills for size, allocation, and calls are all laid out. We traded compactness for simplicity, and that deal is worth it in the vast majority of scenarios. If you really want to squeeze to Chromium's order of magnitude, going back to chew on the source is fine too.

The `OnceCallback` group wraps up here. What comes next is `RepeatingCallback` (the copyable, repeatedly-callable version), and bolting the `Unretained` / `Owned` / `WeakPtr` lifetime helpers onto `bind_once`. That last one happens to be the entry point for the next topic, `WeakPtr`.

## References

- [Chromium base/functional/ source directory](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [Google Test documentation](https://google.github.io/googletest/)
- [Google Benchmark documentation](https://github.com/google/benchmark)
