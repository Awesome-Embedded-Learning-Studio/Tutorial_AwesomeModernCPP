---
chapter: 1
cpp_standard:
- 23
description: Design system test cases for `once_callback`, compare performance differences
  with the original Chromium version and the standard library approach, and summarize
  the design trade-offs.
difficulty: advanced
order: 3
platform: host
prerequisites:
- once_callback 设计指南（一）：动机与接口设计
- once_callback 设计指南（二）：逐步实现
reading_time_minutes: 11
related:
- 回调取消与组合模式
tags:
- host
- cpp-modern
- advanced
- 回调机制
- 函数对象
title: 'once_callback Design Guide (Part 3): Testing Strategy and Performance Comparison'
translation:
  engine: anthropic
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/hands_on/03-once-callback-testing.md
  source_hash: 25b2e6cda1efd104125afd610167dda7f5b19ee3cf346fb3ccf3b83bf83c8a54
  token_count: 2581
  translated_at: '2026-06-13T11:55:17.638057+00:00'
---
# once_callback Design Guide (Part 3): Testing Strategy and Performance Comparison

## Introduction

In the previous two parts, we completed the design and implementation of `once_callback`. In this part, we will do two things: First, we will systematically review the testing strategy and provide a complete checklist of test cases to ensure our implementation is correct under various boundary conditions. Second, we will analyze the performance differences between our implementation, the original Chromium version, and the standard library approach, to understand exactly what we sacrificed and what we gained.

> **Learning Objectives**
>
> - Master the six categories of test case design for `once_callback`
> - Understand the meaning of performance metrics like `std::function`, SBO threshold, and indirect call overhead
> - Clarify the trade-offs between our `once_callback` and Chromium's `OnceCallback`

---

## Testing Strategy

We organize our tests into six categories, each focusing on a specific design invariant. Organizing tests by invariants rather than by features makes it less likely to miss edge cases—because each invariant is itself a correctness guarantee, and the goal of testing is to verify that these guarantees hold in various scenarios.

Our actual test code uses the Catch2 framework, managed via CMake + CPM. The test cases listed below correspond one-to-one with the actual code in `test/test_once_callback.cpp`.

### Category A: Basic Invocation and Return Values

These tests verify the basic construction and invocation behavior of `once_callback`.

```cpp
// A1: Basic construction and invocation
TEST_CASE("construct and invoke") {
    auto cb = once_callback<int()>([]() { return 42; });
    REQUIRE(cb() == 42);
}

// A2: void return type
TEST_CASE("void return type") {
    bool called = false;
    auto cb = once_callback<void()>([&called]() { called = true; });
    cb();
    REQUIRE(called);
}
```

The most basic scenario—construct a callback, invoke it, and verify the return value. The `void` return type exercises a different branch of `operator()`, confirming that our compile-time branching logic is correct.

### Category B: Move Semantics

These tests verify the move-only constraint and the correctness of move operations.

```cpp
// B1: Move-only capture
TEST_CASE("move-only capture") {
    auto uptr = std::make_unique<int>(42);
    // std::unique_ptr is move-only, so the lambda is move-only
    auto cb = once_callback<int()>([up = std::move(uptr)]() { return *up; });
    REQUIRE(cb() == 42);
}

// B2: Move construction and empty state
TEST_CASE("move construction") {
    auto cb1 = once_callback<int()>([]() { return 1; });
    auto cb2 = std::move(cb1);

    // cb1 is now moved-from (empty)
    REQUIRE_FALSE(cb1.valid());
    // cb2 is valid
    REQUIRE(cb2() == 1);
}
```

The move-only capture test (where `std::unique_ptr` is captured into a lambda) confirms that `once_callback` truly supports move-only callables—if the underlying implementation used `std::function` instead of `std::move_only_function`, this code would fail to compile. The move semantics test verifies that after move construction, the source object enters an empty state (checked via `valid()`), and the target object remains valid and callable.

There is a conceptual point that is easily confused—move operations transfer ownership but do not trigger consumption. Only `operator()` consumes the callback. This distinction is important in Chromium as well: `std::move(callback)` simply transfers ownership; the callback remains active until the task is actually executed.

### Category C: Single-Invocation Constraint

These tests verify the core semantic of "consume upon invocation". While Categories A and B covered the normal invocation paths, Category C focuses on compile-time interception of lvalue invocation. This constraint is implemented via deducing this + `delete this`—if you write `cb()` instead of `std::move(cb)()`, the compiler will error out, explicitly telling the caller to use `std::move`. This part requires no runtime tests; the fact that it compiles is itself the verification.

### Category D: Argument Binding

```cpp
// D1: Partial argument binding
TEST_CASE("partial binding") {
    auto cb = once_callback<int(int, int)>([](int a, int b) { return a + b; });
    auto bound_cb = std::move(cb).bind(10);
    REQUIRE(bound_cb(5) == 15);
}

// D2: Member function binding
TEST_CASE("member function binding") {
    struct Adder {
        int add(int a, int b) const { return a + b; }
    };
    Adder adder;
    auto cb = once_callback<int(int)>(adder, &Adder::add, 10);
    REQUIRE(cb(5) == 15);
}
```

The `bind` tests cover two typical scenarios: partial argument binding for a normal lambda and member function binding. The member function binding test deserves attention—`&Adder::add` is a member function pointer, `&adder` is an object pointer, and `bind` internally expands this into a trampoline call. Note the lifetime trap here: `&adder` is a raw pointer, and `once_callback` does not manage its lifetime. If `adder` is destroyed before the callback is invoked, the trampoline will access freed memory through a dangling pointer. Chromium uses `base::RawPtr` to explicitly mark raw pointer safety, `base::PassThrough` to take ownership, and `base::WeakPtr` to automatically cancel the callback upon object destruction. In our simplified version, this safety responsibility is delegated to the caller.

### Category E: Cancellation Mechanism

```cpp
// E1: Token valid, no cancellation
TEST_CASE("cancel valid token") {
    auto token = cancellation_token::create_invalid();
    auto cb = once_callback<int()>([]() { return 42; }, token);
    REQUIRE(cb() == 42);
}

// E2: Token invalid, void callback does nothing
TEST_CASE("cancel void callback") {
    auto token = cancellation_token::create_invalid();
    bool called = false;
    auto cb = once_callback<void()>([&called]() { called = true; }, token);
    cb(); // Should not execute
    REQUIRE_FALSE(called);
}

// E3: Token invalid, non-void callback throws
TEST_CASE("cancel non-void callback throws") {
    auto token = cancellation_token::create_invalid();
    auto cb = once_callback<int()>([]() { return 42; }, token);
    REQUIRE_THROWS_AS(cb(), callback_cancelled);
}
```

Cancellation tests cover three key behaviors: no cancellation when the token is valid, void callbacks do not execute when the token is invalid, and non-void callbacks throw `callback_cancelled` when the token is invalid. The behavior of the third test is worth elaborating on—our implementation throws an exception in cancelled non-void callbacks because the caller expects a return value, and we cannot provide a meaningful one. Throwing is safer than returning an undefined value. Chromium's implementation would terminate the program here (via `CHECK` failure); we chose exceptions because they are easier to catch and verify in tests.

### Category F: Then Composition

```cpp
// F1: Two-stage non-void pipeline
TEST_CASE("then non-void") {
    auto cb1 = once_callback<int()>([]() { return 10; });
    auto cb2 = std::move(cb1).then([](int v) { return v * 2; });
    REQUIRE(cb2() == 20);
}

// F2: Multi-stage pipeline (type boundary crossing)
TEST_CASE("then chain") {
    auto cb1 = once_callback<int()>([]() { return 42; });
    auto cb2 = std::move(cb1).then([](int v) { return std::to_string(v); });
    auto cb3 = std::move(cb2).then([](std::string s) { return s + "!"; });
    REQUIRE(cb3() == "42!");
}

// F3: Void prefix callback
TEST_CASE("then void prefix") {
    auto cb1 = once_callback<void()>([]() { /* side effect */ });
    auto cb2 = std::move(cb1).then([]() { return 42; });
    REQUIRE(cb2() == 42);
}
```

The `then` tests cover three composition patterns: two-stage non-void pipelines, multi-stage pipelines (crossing type boundaries—from `int` to `std::string`), and void prefix callbacks. The multi-stage pipeline test is particularly interesting—`42` is converted to the string `"42"`, which is finally transformed into `"42!"`. This test verifies that `then` correctly deduces the return type at each stage and that type erasure (via `std::move_only_function`) works correctly between different lambda types. The void prefix test verifies the `void` branch—the first callback sets a side effect, and the second callback returns a value.

### Test Framework and Build Configuration

We use Catch2 v3 as our testing framework, automatically pulling dependencies via CPM (CMake Package Manager). The CMake configuration for the tests is very concise:

```cmake
# test/CMakeLists.txt
find_package(Catch2 3 REQUIRED)
add_executable(test_once_callback test_once_callback.cpp)
target_link_libraries(test_once_callback PRIVATE once_callback Catch2::Catch2WithMain)
include(CTest)
include(Catch)
catch_discover_tests(test_once_callback)
```

Catch2's `REQUIRE` macro is superior to `assert` because it reports the specific failed expression, file, and line number, and continues executing subsequent checks within the same `TEST_CASE` (instead of terminating the program like `assert`). `REQUIRE_THROWS_AS` is specifically used to verify exception types—in the cancellation mechanism tests, we need to confirm that the cancelled non-void callback throws `callback_cancelled`, not some other exception.

Running the tests is simple—just `cd test` and `ctest` in the build directory.

---

## Performance Considerations: Comparison with Chromium's Original Version

### Object Size

This is the most intuitive difference. Let's measure it with a simple program:

```cpp
#include <iostream>
#include "once_callback.hpp"

int main() {
    std::cout << "sizeof(once_callback<int()>): " << sizeof(once_callback<int()>) << '\n';
    // std::cout << "sizeof(base::OnceCallback<int()>): " << ...; // Hypothetical
}
```

On GCC, typical values are: `std::move_only_function<int()>` is about 32 bytes, `std::function<int()>` is about 32 bytes, and our `once_callback` plus the `state` enum and optional `cancellation_token` pointer is about 56-64 bytes. Chromium's `base::OnceCallback` is only 8 bytes—a pointer to a `base::internal::BindState`.

The root of the difference lies in the storage strategy. Chromium places all state (callable object + bound arguments) in a heap-allocated `BindState`, and the callback object itself holds only a pointer. We use the SBO (Small Buffer Optimization) of `std::move_only_function` to store small objects directly inline within the callback object, avoiding heap allocation but increasing object size.

### Allocation Behavior

The SBO threshold of `std::move_only_function` is implementation-defined, usually 2-3 pointer sizes (16-24 bytes). Lambdas capturing few arguments (like `[x]` or `[&x]`) usually fit in SBO and don't trigger heap allocation. However, if a lambda captures a large amount of data (like a `std::vector` + a few `std::string` objects), it will heap allocate upon construction.

Chromium's approach always heap allocates (`new BindState`), but allocation happens only once—during construction. Subsequent move operations of `OnceCallback` just copy a pointer (8 bytes), which is extremely cheap. Our approach allocates zero times for small objects (SBO), but move operations require copying the entire `move_only_function` (32 bytes) plus the `cancellation_token` pointer, which is slightly more expensive.

Both strategies have their advantages in different scenarios. For high-frequency delivery of small callbacks (the main scenario for the Chrome browser), Chromium's approach is better—low move cost and consistent size benefit CPU caches. For low-frequency large callbacks (like one-shot initialization tasks), our approach is better—saving one heap allocation.

### Indirect Call Overhead

The call overhead for both approaches is the same: one indirect function call. `std::move_only_function` internally dispatches to the specific callable object via a function pointer or virtual table; Chromium's `BindState` also uses function pointer dispatch. Under `-O2` optimization, this indirect call cannot be inlined away, so both approaches are performance-equivalent.

### What We Sacrificed and What We Gained

To summarize the trade-offs.

We sacrificed object compactness (56-64 bytes vs 8 bytes) in exchange for implementation simplicity—no need to manually write reference counting, function pointer tables, or `[[clang::trivial_abi]]` annotations. We sacrificed extreme move performance (copying 32 bytes + pointer vs copying 8 bytes) in exchange for zero heap allocation for small objects. We sacrificed reference-counted sharing (unable to let multiple callbacks share the same `BindState`), but `once_callback` implies exclusive semantics, so sharing is unnecessary.

These trade-offs are reasonable for educational purposes and most practical scenarios. If your project truly requires Chromium-level extreme performance, you can refer to Chromium's source code for further optimization—the core ideas have been clearly explained in these three design guides.

---

## Complete Component File Overview

At this point, the design, implementation, and testing strategy for the `once_callback` component are complete. The file list is as follows:

```text
include/
  once_callback/
    once_callback.hpp      # Core class definition
    cancellation_token.hpp # Cancellation support
    detail/
      small_unique_ptr.hpp # Helper for SBO (optional)
test/
  test_once_callback.cpp   # Catch2 test suite
src/
  once_callback.cpp        # Implementation (if split)
examples/
  basic_usage.cpp          # Simple examples
```

The corresponding compilable code (headers + tests) is located in the project code directory:

```text
project_root/
  include/once_callback/...
  test/test_once_callback.cpp
  CMakeLists.txt
```

---

## Summary

In this verification part, we did two things. Regarding testing, we designed 12 Catch2 test cases around six invariants (basic invocation, move semantics, single invocation, argument binding, cancellation mechanism, and chaining), covering all core behaviors of `once_callback`. Regarding performance, we compared the differences with Chromium's `OnceCallback` in terms of object size, allocation behavior, and call overhead—our implementation traded compactness for simplicity, which is worth it for the vast majority of scenarios.

Next steps to try: implement `repeating_callback` (copyable, repeatable version), add `base::PassThrough` / `base::RawPtr` / `base::WeakPtr` lifetime helpers to `once_callback`, or use Google Benchmark for precise performance measurement.

## Reference Resources

- [Chromium base/functional/ Source Directory](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [Google Test Documentation](https://google.github.io/googletest/)
- [Google Benchmark Documentation](https://github.com/google/benchmark)
