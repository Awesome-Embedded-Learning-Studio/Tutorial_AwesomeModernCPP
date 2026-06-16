---
chapter: 1
cpp_standard:
- 23
description: Design system test cases for `once_callback`, compare performance differences
  against the original Chromium version and the standard library solution, and summarize
  the design trade-offs.
difficulty: advanced
order: 3
platform: host
prerequisites:
- once_callback 设计指南（一）：动机与接口设计
- once_callback 设计指南（二）：逐步实现
reading_time_minutes: 12
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
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/hands_on/03-once-callback-testing.md
  source_hash: c1310333e60d2b8f8eb9e67f4231def488f910d1cc9f2414bbfb4769048ff058
  translated_at: '2026-06-16T04:14:16.648804+00:00'
  engine: anthropic
  token_count: 2574
---
# once_callback Design Guide (Part 3): Testing Strategy and Performance Comparison

## Introduction

In the previous two parts, we completed the design and implementation of `once_callback`. In this part, we will do two things: first, systematically review the testing strategy and provide a complete list of test cases to ensure our implementation is correct under various boundary conditions; second, analyze the performance differences between our implementation, the original Chromium version, and the standard library approach, to understand what we sacrificed and what we gained.

> **Learning Objectives**
>
> - Master the design method for six categories of `once_callback` test cases
> - Understand the meaning of performance metrics such as object size, SBO threshold, and indirect call overhead
> - Clarify the trade-offs between our `once_callback` and Chromium's `OnceCallback`

---

## Testing Strategy

We organize tests into six categories, each focusing on a specific design invariant. Organizing tests by invariants rather than by functionality makes it less likely to miss edge cases—since each invariant is itself a correctness guarantee, the goal of testing is to verify that these guarantees hold in various scenarios.

Our actual test code uses the Catch2 framework, managed with CMake + CPM. The test cases listed below correspond one-to-one with the actual code in `test/test_once_callback.cpp`.

### Category A: Basic Invocation and Return Values

This category verifies the basic construction and invocation behavior of `once_callback`.

```cpp
// A1: Basic invocation and return value
auto cb = make_once_callback([]() { return 42; });
REQUIRE(cb() == 42);

// A2: void return type
bool invoked = false;
auto void_cb = make_once_callback([&invoked]() { invoked = true; });
void_cb();
REQUIRE(invoked);
```

The most basic scenario—construct a callback, invoke it, and verify the return value. The `void` return type exercises a different branch of `operator()`, confirming that our compile-time branching logic is correct.

### Category B: Move Semantics

This category verifies move-only constraints and the correctness of move operations.

```cpp
// B1: Move-only capture
auto ptr = std::make_unique<int>(99);
auto cb = make_once_callback([p = std::move(ptr)]() { return *p; });
REQUIRE(cb() == 99);

// B2: Move construction and state consumption
auto cb2 = make_once_callback([]() { return 42; });
auto cb3 = std::move(cb2);
REQUIRE(!cb2); // Source is empty
REQUIRE(cb3);  // Target is valid
REQUIRE(cb3() == 42);
```

The move-only capture test (`unique_ptr` captured into lambda) confirms that `once_callback` truly supports move-only callables—if the underlying implementation used `std::function` instead of `std::move_only_function`, this code would fail to compile. The move semantics test verifies that after move construction, the source object becomes an empty state (checked via `operator bool`), and the target object remains valid and can be invoked normally.

There is a conceptual point that is easily confused—move operations transfer ownership but do not trigger consumption. Only `operator()` consumes the callback. This distinction is also important in Chromium: `std::move(callback)` simply transfers ownership; the callback remains active until the task is actually executed.

### Category C: Single-Invocation Constraint

This category verifies the core semantic of "invoke once to consume". In Category A and B tests, we covered the normal invocation path. Category C focuses on compile-time interception of lvalue invocation. This constraint is implemented via deducing this + `delete`—if you write `cb()` instead of `std::move(cb)()`, the compiler will directly report an error, explicitly telling the caller to use `std::move`. This part does not require runtime testing; compilation success is the verification itself.

### Category D: Argument Binding

```cpp
// D1: Partial argument binding
auto cb1 = make_once_callback([](int x, int y) { return x + y; }, 10);
REQUIRE(cb1(5) == 15);

// D2: Member function binding
struct Adder {
    int add(int x, int y) const { return x + y; }
};
Adder adder;
auto cb2 = make_once_callback(&Adder::add, &adder);
REQUIRE(cb2(3, 4) == 7);
```

`bind_front` tests cover two typical scenarios: partial argument binding for normal lambdas and member function binding. The member function binding test deserves attention—`&Adder::add` is a member function pointer, `&adder` is an object pointer, and `bind_front` internally expands it into a `(adder.*add)(x, y)` call. Note a lifetime trap here: `&adder` is a raw pointer, and `bind_front` does not manage its lifetime. If `adder` is destroyed before the callback is invoked, `bind_front` will access freed memory through a dangling pointer. Chromium uses `raw_ptr` to explicitly mark raw pointer safety, uses `std::unique_ptr` to take ownership, and uses `WeakPtr` to automatically cancel the callback when the object is destroyed. In our simplified version, this safety responsibility is temporarily handed to the caller.

### Category E: Cancellation Mechanism

```cpp
// E1: Token is valid, no cancellation
auto token = std::make_shared<int>(42);
auto cb = make_once_callback([token]() { return *token; });
REQUIRE(cb() == 42);

// E2: Token invalidated, void callback does nothing
auto weak_token = std::weak_ptr(token);
auto void_cb = make_once_callback([weak_token]() { /* no-op */ });
token.reset();
REQUIRE_NOTHROW(std::move(void_cb)());

// E3: Token invalidated, non-void callback throws
auto int_cb = make_once_callback([weak_token]() { return *weak_token.lock(); });
token.reset();
REQUIRE_THROWS_AS(std::move(int_cb)(), std::bad_function_call);
```

Cancellation tests cover three key behaviors: no cancellation when the token is valid; void callback does not execute after the token is invalidated; non-void callback throws `std::bad_function_call` after the token is invalidated. The behavior of the third test is worth expanding on—our implementation throws an exception in a cancelled non-void callback because the caller expects a return value, but we cannot provide a meaningful one, so throwing is safer than returning an undefined value. Chromium's implementation would terminate the program directly (`CHECK` failure) here; we chose exceptions because they are easier to catch and verify in tests.

### Category F: Then Composition

```cpp
// F1: Two-level non-void pipeline
auto cb = make_once_callback([]() { return 42; })
    .then([](int x) { return x * 2; });
REQUIRE(cb() == 84);

// F2: Multi-level pipeline (crossing type boundaries)
auto pipeline = make_once_callback([]() { return 3.14; })
    .then([](double d) { return static_cast<int>(d); })
    .then([](int i) { return std::to_string(i); });
REQUIRE(pipeline() == "3");

// F3: Void prefix callback
int value = 0;
auto void_pipe = make_once_callback([&value]() { value = 10; })
    .then([&value]() { return value + 5; });
REQUIRE(void_pipe() == 15);
```

`then` tests cover three composition patterns: two-level non-void pipeline, multi-level pipeline (crossing type boundaries—from `double` to `int` to `string`), and void prefix callback. The multi-level pipeline test is particularly interesting—`3.14` is converted to integer `3`, which is finally converted to string `"3"`. This test verifies that `then` correctly deduces the return type at each level and that type erasure (via `std::move_only_function`) works correctly between different types of lambdas. The void prefix test verifies the `void` branch—the first callback sets `value`, and the second callback reads `value` by reference and returns `value + 5`.

### Test Framework and Build Configuration

We use Catch2 v3 as our test framework, automatically pulling dependencies via CPM (CMake Package Manager). The CMake configuration for tests is very concise:

```cmake
cmake_minimum_required(VERSION 3.20)
project(once_callback_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Catch2 3 REQUIRED)
add_executable(test_once_callback test/test_once_callback.cpp)
target_link_libraries(test_once_callback PRIVATE Catch2::Catch2WithMain)
enable_testing()
include(CTest)
add_test(NAME all_tests COMMAND test_once_callback)
```

Catch2's `REQUIRE` macro is superior to `assert` because it reports the specific failed expression, file, and line number, and continues executing subsequent checks within the same `SECTION` (instead of terminating the program like `assert`). `REQUIRE_THROWS_AS` is specifically used to verify exception types—in the cancellation mechanism tests, we need to confirm that the cancelled non-void callback throws `std::bad_function_call`, not some other exception.

Running the tests is simple—under the project root directory, run `cmake --build build --target test_once_callback`.

---

## Performance Considerations: Comparison with Chromium Original

### Object Size

This is the most intuitive difference. We use a simple program to measure:

```cpp
#include "once_callback.hpp"
#include <iostream>

int main() {
    std::cout << "sizeof(once_callback<int()>): "
              << sizeof(once_callback<int()>) << '\n';
    std::cout << "sizeof(std::move_only_function<int()>): "
              << sizeof(std::move_only_function<int()>) << '\n';
    return 0;
}
```

On GCC, typical values are: `std::move_only_function` is about 32 bytes, `std::function` is about 32 bytes, and our `once_callback` plus the `State` enum and optional `vtable` pointer is about 56-64 bytes. Chromium's `OnceCallback` is only 8 bytes—a pointer to an internal `CallbackBase`.

The root of the difference lies in storage strategy. Chromium places all state (callable object + bound arguments) in a heap-allocated `CallbackBase`, and the callback object itself holds only a pointer. We use SBO (Small Buffer Optimization) via `std::move_only_function` to store small objects directly inside the callback object, avoiding heap allocation but increasing object size.

### Allocation Behavior

The SBO threshold of `std::move_only_function` is implementation-defined, usually 2-3 pointer sizes (16-24 bytes). Lambdas capturing few parameters (like `[x] { return x; }` or `[p = std::unique_ptr<int>()] {}`) usually fit in SBO and do not trigger heap allocation. However, if a lambda captures a large amount of data (like a `std::string` + several `std::vector`s), it will heap allocate upon construction.

Chromium's approach always heap allocates (`new`), but allocation only happens once—during construction. Subsequent move operations of `OnceCallback` just copy a pointer (8 bytes), which is extremely cheap. Our approach allocates nothing for small objects (SBO), but move operations need to copy the entire `std::move_only_function` (32 bytes) plus the `vtable` pointer, which is slightly more expensive.

Both strategies have their advantages in different scenarios. For high-frequency delivery of small callbacks (Chrome browser's main scenario), Chromium's approach is better—low move cost and consistent size benefit CPU cache. For low-frequency large callbacks (like one-time initialization tasks), our approach is better—saving one heap allocation.

### Indirect Call Overhead

The call overhead for both approaches is the same: one indirect function call. `std::move_only_function` internally dispatches to the specific callable object via a function pointer or virtual function table; Chromium's `OnceCallback` also uses function pointer dispatch. Under `-O2` optimization, this indirect call cannot be inlined away, so performance is equivalent for both.

### What We Sacrificed and What We Gained

Let's summarize the trade-offs.

We sacrificed object compactness (56-64 bytes vs 8 bytes) in exchange for implementation simplicity—no need to manually write reference counting, function pointer tables, or `raw_ptr` annotations. We sacrificed extreme move performance (copying 32 bytes + pointer vs copying 8 bytes) in exchange for zero heap allocation for small objects. We sacrificed reference-counted sharing (unable to share a single `CallbackBase` among multiple callbacks), but `once_callback` is inherently exclusive semantics and does not need sharing.

These trade-offs are reasonable for educational purposes and most practical scenarios. If your project truly requires Chromium-level extreme performance, you can refer to Chromium's source code for further optimization—the core ideas have been explained in these three design guide parts.

---

## Complete Component File Overview

At this point, the design, implementation, and testing strategy for the `once_callback` component are complete. The complete file list:

```text
include/once_callback.hpp  # Core implementation
test/test_once_callback.cpp # Catch2 test suite
```

The corresponding compilable code (header + tests) is located in the project code directory:

```text
project/
├── include/
│   └── once_callback.hpp
└── test/
    └── test_once_callback.cpp
```

---

## Summary

In this verification part, we did two things. Regarding testing, we designed 12 Catch2 test cases around six invariants (basic invocation, move semantics, single invocation, argument binding, cancellation mechanism, and chaining), covering all core behaviors of `once_callback`. Regarding performance, we compared the differences with Chromium's `OnceCallback` in object size, allocation behavior, and call overhead—our implementation traded compactness for simplicity, which is worth it for the vast majority of scenarios.

Next steps to try: implement `shared_callback` (a copyable, repeatable version), add `weak_from_this` / `shared_from_this` / `observe()` lifetime helpers to `once_callback`, or use Google Benchmark for precise performance measurement.

## Reference Resources

- [Chromium base/functional/ Source Directory](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [Google Test Documentation](https://google.github.io/googletest/)
- [Google Benchmark Documentation](https://github.com/google/benchmark)
