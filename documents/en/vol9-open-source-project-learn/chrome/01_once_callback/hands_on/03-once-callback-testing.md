---
title: 'once_callback Design Guide (Part 3): Testing Strategy and Performance Comparison'
description: Design test cases for once_callback, compare performance differences
  with the original Chromium and standard library solutions, and summarize the design
  trade-offs.
chapter: 1
order: 3
tags:
- host
- cpp-modern
- advanced
- 回调机制
- 函数对象
difficulty: advanced
platform: host
cpp_standard:
- 23
reading_time_minutes: 20
prerequisites:
- once_callback 设计指南（一）：动机与接口设计
- once_callback 设计指南（二）：逐步实现
related:
- 回调取消与组合模式
---
# once_callback Design Guide (Part 3): Testing Strategy and Performance Comparison

## Introduction

In the previous two parts, we completed the design and implementation of `OnceCallback`. In this part, we do two things: first, we systematically outline a testing strategy and provide a complete test case checklist to ensure our implementation is correct under various boundary conditions; second, we analyze the performance differences between our implementation, the original Chromium version, and the standard library approach, clarifying what we sacrificed and what we gained in return.

> **Learning Objectives**
>
> - Master the six categories of test case design for `OnceCallback`
> - Understand the meaning of performance metrics such as `sizeof`, SBO threshold, and indirect call overhead
> - Clearly understand the trade-offs between our `OnceCallback` and Chromium's `OnceCallback`

---

## Testing Strategy

We organize our tests into six categories, each focusing on an independent design invariant. Organizing tests by invariant rather than by feature makes it less likely to miss edge cases—because each invariant is itself a correctness guarantee, and the purpose of testing is to verify that these guarantees hold under various scenarios.

Our actual test code uses the Catch2 framework, with CMake + CPM for dependency management. The test cases listed below correspond one-to-one with the actual code in `code/volumn_codes/vol9/chrome_design/test/test_once_callback.cpp`.

### Category A: Basic Invocation and Return Values

These tests verify the basic construction and invocation behavior of `OnceCallback`.

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

The most basic scenario—construct a callback, invoke it, and verify the return value. The `void` return type takes a different branch in `if constexpr (std::is_void_v<ReturnType>)`, confirming that our compile-time branching logic is correct.

### Category B: Move Semantics

These tests verify the move-only constraint and the correctness of move operations.

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

The move-only capture test (`std::make_unique<int>(42)` captured into a lambda) confirms that `OnceCallback` truly supports move-only callables—if the underlying implementation used `std::function` instead of `std::move_only_function`, this code would fail to compile outright. The move semantics test verifies that after move construction, the source object enters the `kEmpty` state (checked via `is_null()`), while the destination object remains valid and can be invoked normally.

There is a concept that is easy to confuse—move operations transfer ownership but do not trigger consumption. Only `run()` consumes the callback. This distinction is also important in Chromium: `PostTask(FROM_HERE, std::move(cb))` merely transfers ownership, and the callback remains active until the task is executed.

### Category C: Single-Invocation Constraint

These tests verify the core semantic of "invoke once to consume." In the Category A and B tests, we already covered the normal invocation path. Category C focuses on the compile-time interception of lvalue invocations. This constraint is implemented through deducing this + `static_assert`—if we write `cb.run()` instead of `std::move(cb).run()`, the compiler will directly report an error, and the error message explicitly tells the caller to use `std::move`. This part does not require runtime tests; successful compilation is itself the verification.

### Category D: Argument Binding

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

The `bind_once` test covers two typical scenarios: partial argument binding for a plain lambda and member function binding. The member function binding test is particularly noteworthy—`&Calc::multiply` is a member function pointer, `&calc` is an object pointer, and `std::invoke` internally expands this into a `(calc.*multiply)(5, 8)` call. There is a lifetime trap to note here: `&calc` is a raw pointer, and `bind_once` does not manage its lifetime. If `calc` is destroyed before the callback is invoked, `std::invoke` will access freed memory through a dangling pointer. Chromium uses `base::Unretained` to explicitly mark the safety of raw pointers, uses `base::Owned` to take over ownership, and uses `base::WeakPtr` to automatically cancel the callback when the object is destructed. In our simplified version, this safety responsibility is temporarily left to the caller.

### Category E: Cancellation Mechanism

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

The cancellation tests cover three key behaviors: no cancellation when the token is valid, void callbacks not executing after the token is invalidated, and non-void callbacks throwing `std::bad_function_call` after the token is invalidated. The behavior of the third test is worth expanding on—our implementation throws an exception in a canceled non-void return callback because the caller expects a return value, but we cannot provide a meaningful one, so throwing an exception is safer than returning an undefined value. Chromium's implementation would directly terminate the program here (`CHECK` failure); we chose exceptions because they are easier to catch and verify in tests.

### Category F: Then Composition

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

The `then()` test covers three composition patterns: a two-level non-void pipeline, a multi-level pipeline (crossing type boundaries—from `int` to `std::string`), and a void prefix callback. The multi-level pipeline test is particularly interesting—`(5*2)+10 = 20`, which is ultimately converted to the string `"20"` by `std::to_string`. This test verifies that `then()` correctly deduces the return type at each level, and that type erasure (via `std::move_only_function`) works correctly between lambdas of different types. The void prefix test verifies the `if constexpr (std::is_void_v<ReturnType>)` branch—the first callback sets `value = 7`, and the second callback reads `value` by reference and returns `21`.

### Test Framework and Build Configuration

We use Catch2 v3 as our test framework, automatically pulling in dependencies via CPM (CMake Package Manager). The CMake configuration for the tests is very concise:

```cmake
# test/CMakeLists.txt
CPMAddPackage("gh:catchorg/Catch2@3.7.1")

add_executable(test_once_callback test_once_callback.cpp)
target_link_libraries(test_once_callback PRIVATE once_callback Catch2::Catch2WithMain)
target_compile_options(test_once_callback PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_once_callback COMMAND test_once_callback)
```

Catch2's `REQUIRE` macro is superior to `assert()` because it reports the specific failing expression, file, and line number, and continues executing subsequent checks within the same `TEST_CASE` (rather than terminating the program immediately like `assert()`). `REQUIRE_THROWS_AS` is specifically used to verify exception types—in the cancellation mechanism tests, we need to confirm that a canceled non-void callback throws a `std::bad_function_call`, not some other exception.

The workflow for running the tests is simple—in the `build/` directory, run `cmake --build . && ctest`.

---

## Performance Considerations: Comparison with the Original Chromium Version

### Object Size

This is the most intuitive difference. We use a simple program to measure it:

```cpp
#include <functional>
#include <iostream>
#include "once_callback/once_callback.hpp"

int main() {
    std::cout << "sizeof(std::function<void()>):      "
              << sizeof(std::function<void()>) << " bytes\n";
    std::cout << "sizeof(std::move_only_function<void()>): "
              << sizeof(std::move_only_function<void()>) << " bytes\n";
    // Chromium OnceCallback<void()> ≈ 8 bytes（一个指针）

    using namespace tamcpp::chrome;
    std::cout << "sizeof(OnceCallback<void()>): "
              << sizeof(OnceCallback<void()>) << " bytes\n";
    // 我们的 OnceCallback 大约是：
    // move_only_function (32) + status (1) + token ptr (16) + padding
    // 预估 56-64 bytes
}
```

On GCC, typical values are as follows: `std::function<void()>` is about 32 bytes, `std::move_only_function<void()>` is about 32 bytes, and our `OnceCallback<void()>` plus the `Status` enum and optional `CancelableToken` pointer is about 56–64 bytes. Chromium's `OnceCallback<void()>` is only 8 bytes—a `scoped_refptr` pointing to a `BindState`.

The root cause of this difference lies in the storage strategy. Chromium puts all state (the callable object + bound arguments) into a heap-allocated `BindState`, and the callback object itself only holds a pointer. We use the SBO of `std::move_only_function` to inline small objects directly inside the callback object, avoiding heap allocation but increasing the object size.

### Allocation Behavior

The SBO threshold of `std::move_only_function` is implementation-defined, usually two to three pointer sizes (16–24 bytes). Lambdas that capture a small number of arguments (such as `[x = 42]` or `[&ref]`) can usually fit into the SBO without triggering heap allocation. However, if a lambda captures a large amount of data (such as a `std::string` plus a few `int`), it will heap-allocate upon construction.

Chromium's approach always heap-allocates (`new BindState<Functor, BoundArgs...>`), but the allocation only happens once—during `BindOnce`. Subsequent move operations of `OnceCallback` merely copy a pointer (8 bytes), at an extremely low cost. Our approach does not allocate for small objects (SBO), but move operations require copying the entire `std::move_only_function` (32 bytes) plus the `token_` pointer, at a slightly higher cost.

The two strategies each have advantages in different scenarios. For high-frequency delivery of small callbacks (the main scenario for the Chrome browser), Chromium's approach is better—low move cost and consistent size benefit CPU caching. For low-frequency large callbacks (such as one-time initialization tasks), our approach is better—saving one heap allocation.

### Indirect Call Overhead

The invocation overhead of both approaches is the same: one indirect function call. Internally, `std::move_only_function::operator()` dispatches to the specific callable object via a function pointer or virtual function table; Chromium's `BindState::polymorphic_invoke_` also uses function pointer dispatch. Under `-O2` optimization, this indirect call cannot be inlined away, making the two approaches equivalent in performance.

### What We Sacrificed and What We Gained

Let us summarize the trade-offs.

We sacrificed object compactness (56–64 bytes vs. 8 bytes) in exchange for implementation simplicity—no need to hand-write reference counting, function pointer tables, or `TRIVIAL_ABI` annotations. We sacrificed extreme move operation performance (copying 32 bytes + a pointer vs. copying 8 bytes) in exchange for zero heap allocation for small objects. We sacrificed reference-counted sharing (unable to let multiple callbacks share the same `BindState`), but `OnceCallback` itself has exclusive semantics and does not need sharing.

These trade-offs are reasonable for educational purposes and most practical scenarios. If your project truly requires Chromium-level extreme performance, you can refer to Chromium's source code for further optimization—the core ideas have already been explained clearly in these three design guides.

---

## Complete Component File Overview

At this point, the design, implementation, and testing strategy of the `OnceCallback` component are all complete. The full file list:

```text
documents/vol9-open-source-project-learn/chrome/hands_on/
├── 01-once-callback-design.md           # 设计篇：动机与接口
├── 02-once-callback-implementation.md   # 实现篇：逐步实现
└── 03-once-callback-testing.md          # 验证篇：测试与性能
```

The corresponding compilable code (header files + tests) is located in the project code directory:

```text
code/volumn_codes/vol9/chrome_design/
├── CMakeLists.txt
├── cmake/CPM.cmake
├── cancel_token/
│   └── cancel_token.hpp                 # 取消令牌
├── once_callback/
│   ├── CMakeLists.txt
│   ├── once_callback.hpp                # 主接口（模板声明）
│   └── once_callback_impl.hpp           # 实现（模板定义）
└── test/
    ├── CMakeLists.txt                   # Catch2 测试配置
    └── test_once_callback.cpp           # 完整测试用例
```

---

## Summary

In this verification part, we did two things. On the testing side, we designed 11 Catch2 test cases around six invariants (basic invocation, move semantics, single invocation, argument binding, cancellation mechanism, and chained composition), covering all core behaviors of `OnceCallback`. On the performance side, we compared the differences with Chromium's `OnceCallback` in terms of object size, allocation behavior, and invocation overhead—our implementation traded compactness for simplicity, and for the vast majority of scenarios, this trade-off is worthwhile.

Possible directions for next steps: implement `RepeatingCallback` (a copyable, repeatedly invocable version), add lifecycle helper functions like `Unretained` / `Owned` / `WeakPtr` to `bind_once`, or use Google Benchmark for precise performance measurements.

## References

- [Chromium base/functional/ source directory](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [Google Test documentation](https://google.github.io/googletest/)
- [Google Benchmark documentation](https://github.com/google/benchmark)
