---
title: 'OnceCallback in Practice (Part 6): Testing and Performance Comparison'
description: We systematically design six categories of test cases to verify all core
  behaviors of `OnceCallback`, and compare the performance differences against the
  original Chromium implementation and standard library alternatives.
chapter: 1
order: 6
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
difficulty: beginner
platform: host
cpp_standard:
- 23
reading_time_minutes: 10
prerequisites:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 实战（三）：bind_once 实现
- OnceCallback 实战（四）：取消令牌设计
- OnceCallback 实战（五）：then 链式组合
related:
- OnceCallback 前置知识（五）：std::move_only_function
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/01-6-once-callback-testing-and-perf.md
  source_hash: 46dfe09ed16416d6337d24255d3d4c5cf2359d9636d20273d62e69749c7039b1
  translated_at: '2026-05-26T12:26:21.506854+00:00'
  engine: anthropic
  token_count: 1889
---
# OnceCallback in Practice (Part 6): Testing and Performance Comparison

## Introduction

At this point, the four core features of OnceCallback—the core skeleton, `bind_once`, cancellation token, and `then()` chaining—are fully implemented. In this article, we do two things: first, we systematically outline our testing strategy to ensure the implementation is correct under various boundary conditions; second, we analyze the performance differences between our implementation, the original Chromium version, and standard library approaches, clarifying what we sacrificed and what we gained.

> **Learning Objectives**
>
> - Master the method of organizing test cases by invariants
> - Understand the design intent and key assertions of the six test categories
> - Be clear on the performance trade-offs between our OnceCallback and the original Chromium version

---

## Test Framework Setup

We use Catch2 v3 as our testing framework, automatically fetching the dependency via CPM (CMake Package Manager).

```cmake
# test/CMakeLists.txt
CPMAddPackage("gh:catchorg/Catch2@3.7.1")

add_executable(test_once_callback test_once_callback.cpp)
target_link_libraries(test_once_callback PRIVATE once_callback Catch2::Catch2WithMain)
target_compile_options(test_once_callback PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_once_callback COMMAND test_once_callback)
```

Catch2's `REQUIRE` macro is stronger than `assert()` because it reports the specific failing expression, file, and line number, and continues executing subsequent checks within the same `TEST_CASE`. `REQUIRE_THROWS_AS` is specifically used to verify exception types.

Running the tests: in the `build/` directory, run `cmake --build . && ctest`.

---

## Six Categories of Test Cases

We organize the tests into six categories, each focusing on an independent design invariant. Organizing tests by invariant rather than by feature makes it less likely to miss edge cases.

### Category A: Basic Invocation and Return Values

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

Verifies the most basic construction and invocation behavior—non-void callbacks return the correct value, and void callbacks execute normally. The void return path takes a different branch in `if constexpr (std::is_void_v<ReturnType>)`.

### Category B: Move Semantics

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

The move-only capture test verifies that OnceCallback truly supports move-only callables—if the underlying implementation used `std::function` instead of `std::move_only_function`, this code would fail to compile. The move semantics test verifies that after a move construction, the source object transitions to the kEmpty state.

There is an easily confused concept here—move operations transfer ownership but do not trigger consumption. Only `run()` consumes the callback. `OnceCallback cb2 = std::move(cb1)` merely transfers ownership, and the callback remains active until `cb2.run()`.

### Category C: Single-Invocation Constraint

This constraint is implemented via deducing this + `static_assert`—`cb.run()` triggers a compile error, while only `std::move(cb).run()` can pass. No runtime testing is needed; successful compilation is itself the verification.

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

Covers partial argument binding for regular lambdas and member function binding. The lifetime trap of member function binding was discussed in previous articles—`&calc` is a raw pointer, so the caller is responsible for safety.

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

Three key behaviors: no cancellation when the token is valid, void callbacks do not execute after the token is invalidated, and non-void callbacks throw `std::bad_function_call` after the token is invalidated.

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
    REQUIRE(result == "20");
}

TEST_CASE("then with void first callback", "[then]") {
    int value = 0;
    auto cb = OnceCallback<void(int)>([&value](int x) { value = x; })
                  .then([&value] { return value * 3; });
    int result = std::move(cb).run(7);
    REQUIRE(result == 21);
}
```

Covers three composition patterns: two-stage non-void pipelines, multi-stage pipelines (crossing type boundaries from int to string), and void prefix callbacks.

---

## Performance Comparison: With the Original Chromium Version

### Object Size

```cpp
std::cout << "sizeof(std::function<void()>):        "
          << sizeof(std::function<void()>) << " bytes\n";
std::cout << "sizeof(std::move_only_function<void()>): "
          << sizeof(std::move_only_function<void()>) << " bytes\n";
// Chromium OnceCallback<void()> ≈ 8 bytes

std::cout << "sizeof(OnceCallback<void()>): "
          << sizeof(OnceCallback<void()>) << " bytes\n";
// 我们的：move_only_function (32) + status (1) + token ptr (16) + padding
// 预估 56-64 bytes
```

On GCC, typical values are `std::function` at about 32 bytes, `std::move_only_function` at about 32 bytes, and our `OnceCallback` at about 56-64 bytes. Chromium's is only 8 bytes.

The root cause of this difference lies in the storage strategy. Chromium puts all state in a heap-allocated `BindState`, and the callback object holds only a single pointer. We use SBO with `std::move_only_function` to inline small objects directly, avoiding heap allocation but increasing the object size.

### Allocation Behavior

The SBO threshold for `std::move_only_function` is typically two to three pointer sizes (16-24 bytes). Lambdas capturing a small number of arguments usually fit into the SBO and do not trigger heap allocation. Large lambdas, however, allocate on the heap upon construction.

Chromium always allocates on the heap (`new BindState`), but the allocation only happens once. After that, moving a OnceCallback simply copies a pointer (8 bytes), at an extremely low cost. Our approach does not allocate for small objects (SBO), but move operations require copying 32+ bytes.

### Indirect Invocation Overhead

The invocation overhead is the same for both approaches—one indirect function call. Both `std::move_only_function::operator()` and Chromium's `polymorphic_invoke_` dispatch through a function pointer. Under `-O2` optimization, this indirect call cannot be inlined away.

### Trade-off Summary

| Metric | Our Approach | Chromium Approach |
|--------|-------------|-------------------|
| Callback object size | 56-64 bytes | 8 bytes |
| Small lambda heap allocation | No allocation (SBO) | Always allocates |
| Move cost | Copy 32+ bytes | Copy 1 pointer |
| Implementation code size | ~200 lines | ~2000+ lines |

We sacrificed object compactness and extreme move performance in exchange for implementation simplicity—there is no need to manually write reference counting, function pointer tables, or `TRIVIAL_ABI` annotations. Zero heap allocation for small lambdas can actually be an advantage in certain low-frequency scenarios. For teaching purposes and most practical scenarios, this trade-off is worthwhile.

---

## Summary

In this article, we did two things. On the testing side, we designed 11 Catch2 test cases around six invariants (basic invocation, move semantics, single invocation, argument binding, cancellation mechanism, and chaining composition), covering all core behaviors of OnceCallback. On the performance side, we compared the differences with Chromium's OnceCallback in terms of object size, allocation behavior, and invocation overhead—our implementation trades compactness for simplicity.

With this, the design, implementation, and verification of the OnceCallback component are fully complete. Across 13 articles, from prerequisite knowledge to hands-on practice, we covered the complete knowledge chain from C++11 move semantics to C++23 deducing this. We hope this series helps you understand "how to design an industrial-grade component with modern C++"—it is not just about writing code, but more importantly, understanding the reasoning behind every design decision.

## References

- [Chromium base/functional/ source directory](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [Catch2 documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
