---
chapter: 1
cpp_standard:
- 23
description: "Systematically design test cases for once_callback, compare its performance against the original Chromium implementation and the standard-library alternatives, and summarize the design trade-offs."
difficulty: advanced
order: 3
platform: host
prerequisites:
- 'once_callback Design Guide (I): motivation and API design'
- 'once_callback Design Guide (II): step-by-step implementation'
reading_time_minutes: 12
related:
- 'Callback cancellation and composition patterns'
tags:
- host
- cpp-modern
- advanced
- 回调机制
- 函数对象
title: 'once_callback Design Guide (III): test strategy and performance comparison'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/hands_on/03-once-callback-testing.md
  source_hash: 17746ba8a2df341e5afb9987a24055629d311f1e18c6b0c18d744ab485cc0a25
  translated_at: '2026-09-26T01:11:48+00:00'
  engine: anthropic
  token_count: 3500
---
# once_callback Design Guide (III): test strategy and performance comparison

At this point, the `OnceCallback` interface and its implementation are both complete. But I did not rush to call it done — for something like this, if you do not put it under the pressure of tests, you cannot even trust it yourself. In this post we settle the testing strategy and the performance ledger in one go: is it actually correct, how far does it sit from the original Chromium version, and which parts of that gap are we willing to accept.

## Slicing tests by invariants

How to organize the tests had me hesitating at first, too. Splitting by feature leaks easily, because features are written for your own eyes — you test whatever you thought of while writing, so blind spots come built in. Later I switched to splitting by **invariants**, and that cut felt much better: each invariant is itself a sentence of the form "I promise this always holds", and the job of testing is to torture that sentence in every posture imaginable and see whether it breaks. If it breaks, that is a genuine bug; if it holds, the whole class passes.

The test code hangs off Catch2, with dependencies pulled in by CMake + CPM. The cases listed below map one-to-one onto the actual code in `code/volumn_codes/vol9/chrome_design/test/test_once_callback.cpp` — with that code in hand, you can run them case by case.

### Category A: basic invocation and return values

The most basic check: construct a callback, run it, see whether the return value is right.

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

A void return goes down the other branch of `if constexpr (std::is_void_v<ReturnType>)`, so these two cases are insurance for the compile-time branching logic.

### Category B: move semantics

This category watches two things: that the move-only constraint is not a sham, and that a move does not lose state.

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

The move-only capture case stuffs a `std::make_unique<int>(42)` into the lambda — if the storage underneath had fallen back to `std::function` instead of `std::move_only_function`, this would not even compile, so the case doubles as a backstop for "did we genuinely get move-only or not". The move-semantics case verifies that after move construction the source object falls back to `kEmpty` and `is_null()` reports true, while the target object still runs as usual.

Here I must pull out a point that took me a while to untangle: moving only hands over ownership, it does not **consume**. What actually consumes the callback is `run()`. Both of them look like "the cb got moved", but semantically they are two different things. Chromium plays by the same rule — `PostTask(FROM_HERE, std::move(cb))` only carries ownership into the task queue; the callback stays alive until it is actually executed.

### Category C: the single-call constraint

Categories A and B have already walked the normal invocation paths; Category C stares at exactly one thing: invoking on an lvalue must fail to compile. We pinned this constraint onto the signature with deducing this plus `static_assert`, so it is not a runtime matter at all — if your hand slips and you write `cb.run()` instead of `std::move(cb).run()`, the compiler stops you on the spot and conveniently feeds "you need std::move" into the error message. Compiles = verified; you do not even need to run it.

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

`bind_once` wades through two typical scenarios: partial argument binding for an ordinary lambda, and member-function binding. The member-function one deserves a few extra words — `&Calc::multiply` is a pointer to member function, `&calc` is a pointer to the object, and `std::invoke` underneath expands it into `(calc.*multiply)(5, 8)`. Where the trap lies: `&calc` is a raw pointer, and `bind_once` does not look after its life or death. If `calc` gets destructed before the callback actually runs, `std::invoke` follows the dangling pointer and reaches into freed memory. Chromium keeps three tiers of insurance here — `base::Unretained` explicitly declares "pointer safety is on you", `base::Owned` takes over ownership outright, and `base::WeakPtr` cancels the callback automatically when the object is destructed. Our simplified version dumps this responsibility on the caller for now; we will come back to collect it in the cancellation-token post.

### Category E: the cancellation mechanism

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

The cancellation category presses on three actions: no cancellation while the token is still alive, a void callback that honestly refuses to execute once the token is invalidated, and a non-void callback that throws `std::bad_function_call` after invalidation. The third one deserves a pause to explain — we chose to throw for a cancelled non-void callback because, in the caller's eyes, it asked for a return value, and in the cancelled state we simply have no "meaningful value" to hand over. Fool it with a default value? That is sneakier than throwing: the bug propagates downstream along that fake value. Chromium plays this one even harder — a straight `CHECK` failure that blows the program up. We chose the exception purely because it is easy to catch and easy to verify in tests — that is a pedagogical trade-off, not a design superiority.

### Category F: Then composition

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

The three cases in the `then()` category each press a different posture: a two-level non-void pipeline, a multi-level pipeline across types, and a void-prefix callback. The multi-level pipeline one is, I think, the most telling — the number `(5*2)+10 = 20` ends up folded by `std::to_string` into the string `"20"`, every level's return type along the way is deduced correctly by `then()`, and the type erasure `std::move_only_function` performs across several completely different lambda types holds up as well. The void-prefix case presses specifically on the `if constexpr (std::is_void_v<ReturnType>)` branch — the first callback writes 7 into the external `value`, and the second callback reads `value` back out through the reference, multiplies by 3, and arrives at 21.

### Test framework and build configuration

The test framework of choice is Catch2 v3, with dependencies pulled automatically by CPM (CMake Package Manager). The CMake configuration is painless:

```cmake
# test/CMakeLists.txt
CPMAddPackage("gh:catchorg/Catch2@3.7.1")

add_executable(test_once_callback test_once_callback.cpp)
target_link_libraries(test_once_callback PRIVATE once_callback Catch2::Catch2WithMain)
target_compile_options(test_once_callback PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_once_callback COMMAND test_once_callback)
```

I use `REQUIRE` instead of `assert` for a very practical reason: when a `REQUIRE` fails, it throws out the failed expression, the file, and the line number, and later assertions in the same `TEST_CASE` keep running; when an `assert` blows up, the whole program halts, so you only get to see one error at a time. `REQUIRE_THROWS_AS` exists specifically to pin down exception types — the cancellation tests rely on it to confirm that what was thrown is `std::bad_function_call` and not something else.

Running the tests is a single line: from the `build/` directory, `cmake --build . && ctest`.

---

## The performance ledger: comparing against the Chromium original

### Object size

The most visible difference is right there in sizeof. Let us write a minimal program and measure:

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

On GCC the numbers come out roughly as follows: `std::function<void()>` is about 32 bytes, `std::move_only_function<void()>` about 32 bytes, and our `OnceCallback<void()>`, with the `Status` enum and the optional `CancelableToken` pointer added, lands somewhere around 56-64 bytes. Chromium's `OnceCallback<void()>` is just 8 bytes — a single `scoped_refptr` pointing at the `BindState`, and nothing more.

Where does the gap come from? At the root, the storage strategy. Chromium stuffs everything — the callable object and the bound arguments alike — into a heap-allocated `BindState`, and the callback object itself holds nothing but one pointer. Our version leans on `std::move_only_function`'s SBO to inline small objects directly into the callback object; the heap allocation is saved, at the cost of an object body that has put on weight.

### Allocation behavior

The SBO threshold of `std::move_only_function` is implementation-defined, typically somewhere around 2-3 pointers (16-24 bytes). A very lightly capturing lambda, such as `[x = 42]` or `[&ref]`, generally fits into the SBO and triggers no heap allocation; but if the lambda drags in a crowd of data — say a `std::string` plus a few `int`s — construction has to pay for one more heap allocation.

Chromium's scheme is a fixed heap allocation — `new BindState<Functor, BoundArgs...>` always runs, but it runs **exactly once**, at the very moment of `BindOnce`. After that, moving a `OnceCallback` is nothing more than copying one 8-byte pointer, featherweight. Our version allocates nothing for small objects (the SBO absorbs them), but once a move is needed, the whole `std::move_only_function` (32 bytes) plus the `token_` pointer have to be hauled away together, a visibly higher cost.

Neither strategy can win everywhere. For small callbacks posted at high frequency (the browser is Chrome's home turf), Chromium's scheme has the edge — moves are cheap, and the uniform size is friendly to the CPU cache. For large, low-frequency callbacks (one-shot initialization tasks, for example), our scheme is the better deal — one heap allocation saved. Which one to pick depends on your project's frequency distribution.

### Indirect-call overhead

On call overhead the two roads are even: both amount to one indirect call. `std::move_only_function::operator()` dispatches to the concrete callable through a function pointer or a vtable underneath; Chromium's `BindState::polymorphic_invoke_` is function-pointer dispatch as well. At `-O2` the compiler cannot eliminate this layer of indirection, so on the calling link the two designs are equivalent.

### What we gave up and what we got back

Let us settle the account.

What we gave away is object compactness (56-64 bytes versus 8), and what we bought back is a clean implementation — no hand-rolled reference counting, no function-pointer tables, no `TRIVIAL_ABI` annotations. The move side pays its price too (hauling 32 bytes plus a pointer versus copying 8 bytes), and what it buys back is zero heap allocation for small objects. We also conceded reference-counted sharing: there is no way for several callbacks to share one `BindState` — but `OnceCallback` is exclusive-ownership semantics to begin with, and sharing is a capability it never needed.

This trade-off stands up in a teaching context and in the overwhelming majority of real projects. If your project truly pushes into Chromium-class performance requirements, you can go straight to the Chromium source and squeeze out one more layer — the core ideas have already been laid out across the first three posts; what remains is engineering detail.

---

## Where the files live

With this, the design, implementation, and testing of the `OnceCallback` group are wrapped up. The complete file listing is below — follow it to locate the corresponding code:

```text
documents/vol9-open-source-project-learn/chrome/01_once_callback/hands_on/
├── 01-once-callback-design.md           # Design: motivation and API
├── 02-once-callback-implementation.md   # Implementation: step by step
└── 03-once-callback-testing.md          # Verification: testing and performance
```

The corresponding compilable code (headers + tests) sits in the project code directory:

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
    └── test_once_callback.cpp           # complete test cases
```

---

This testing post revolves around six invariants — basic invocation, move semantics, the single-call rule, argument binding, the cancellation mechanism, and chaining — and breaks them out into 12 Catch2 cases, with the core behavior of `OnceCallback` pretty much all pinned underneath. On the performance side, once we line it up against the original Chromium version, the books are open on all three links — size, allocation, and invocation: we traded compactness for simplicity, a deal that pays off in the vast majority of scenarios; if you ever truly need to squeeze into Chromium's weight class, it is never too late to go back and chew through the source.

That closes the chapter on the `OnceCallback` group. Up next come `RepeatingCallback` (the copyable, repeatedly invokable version), plus bolting the `Unretained` / `Owned` / `WeakPtr` lifetime helpers onto `bind_once` — and the latter happens to be the doorway into the next topic, `WeakPtr`.

## References

- [the Chromium `base/functional/` source directory](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/)
- [cppreference: `std::move_only_function`](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [Google Test documentation](https://google.github.io/googletest/)
- [Google Benchmark documentation](https://github.com/google/benchmark)
