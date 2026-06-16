---
chapter: 1
cpp_standard:
- 23
description: We systematically design six categories of test cases to verify all core
  behaviors of `OnceCallback`, and compare performance differences with the original
  Chromium version and the standard library solution.
difficulty: beginner
order: 6
platform: host
prerequisites:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 实战（三）：bind_once 实现
- OnceCallback 实战（四）：取消令牌设计
- OnceCallback 实战（五）：then 链式组合
reading_time_minutes: 8
related:
- OnceCallback 前置知识（五）：std::move_only_function
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
title: 'OnceCallback in Practice (Part 6): Testing and Performance Comparison'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/01-6-once-callback-testing-and-perf.md
  source_hash: 19141c279afaf333a9cf1a34b00ae8feb925ac191b4c0bb750915f1f7caac3b5
  translated_at: '2026-06-16T04:13:12.081300+00:00'
  engine: anthropic
  token_count: 1880
---
# OnceCallback in Practice (Part 6): Testing and Performance Comparison

## Introduction

At this point, the four core features of OnceCallback—the core skeleton, move semantics, cancellation tokens, and `Then` chaining—have all been implemented. In this article, we will do two things: first, systematically review the testing strategy to ensure the implementation is correct under various boundary conditions; second, analyze the performance differences between our implementation, the original Chromium version, and standard library approaches, to understand exactly what we traded off and what we gained.

> **Learning Objectives**
>
> - Master the method of organizing test cases by invariants.
> - Understand the design intent and key assertions of the six test categories.
> - Clarify the performance trade-offs between our OnceCallback and the original Chromium version.

---

## Building the Test Framework

We use Catch2 v3 as our testing framework, automatically pulling dependencies via CPM (CMake Package Manager).

```cmake
# test/CMakeLists.txt
CPMAddPackage("gh:catchorg/Catch2@3.7.1")

add_executable(test_once_callback test_once_callback.cpp)
target_link_libraries(test_once_callback PRIVATE once_callback Catch2::Catch2WithMain)
target_compile_options(test_once_callback PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_once_callback COMMAND test_once_callback)
```

Catch2's `REQUIRE` macro is superior to `assert` because it reports the specific failed expression, file, and line number, and continues executing subsequent checks within the same `SECTION`. `REQUIRE_THROWS` is specifically used to verify exception types.

Running tests: In the `build` directory, run `ctest`.

---

## Six Categories of Test Cases

We organize the tests into six categories, each focusing on a specific design invariant. Organizing tests by invariants rather than by functionality makes it less likely to miss boundary conditions.

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

Verifies the most basic construction and invocation behavior—non-void callbacks return the correct values, and void callbacks execute normally. The void return path takes a different branch in `operator()`.

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

The move-only capture test verifies that OnceCallback truly supports move-only callables—if the underlying implementation used `std::function` instead of a custom wrapper, this code would fail to compile. The move semantics test verifies that after a move construction, the source object enters the `kEmpty` state.

There is a conceptual point that is easy to confuse—move operations transfer ownership but do not trigger consumption. Only `operator()` consumes the callback. `std::move` merely transfers ownership; the callback remains active until `operator()` is called.

### Category C: Single-Invocation Constraint

This constraint is implemented via deducing this + `delete` on `const`—calling `operator()` on a const object triggers a compile error, while calling on a non-const object passes. No runtime test is needed; the compilation success itself is the verification.

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

Covers partial argument binding for normal lambdas and member function binding. The lifetime trap of member function binding was discussed in previous articles—`this` is a raw pointer, so the caller is responsible for safety.

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

Three key behaviors: no cancellation when the token is valid, void callbacks do not execute when the token is expired, and non-void callbacks throw `BadOnceCall` when the token is expired.

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

Covers three composition patterns: two-level non-void pipelines, multi-level pipelines (crossing type boundaries from int to string), and void prefix callbacks.

---

## Performance Comparison: vs. Original Chromium

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

The root of the difference lies in the storage strategy. Chromium places all state in a heap-allocated control block, and the callback object holds only a pointer. We use SBO (Small Buffer Optimization) to inline small objects directly, avoiding heap allocation but increasing object size.

### Allocation Behavior

The SBO threshold for `std::function` is typically 2-3 pointer sizes (16-24 bytes). Lambdas capturing a few arguments usually fit in SBO and do not trigger heap allocation. Large lambdas, however, trigger heap allocation upon construction.

Chromium always allocates on the heap, but allocation happens only once. Subsequent move operations of OnceCallback simply copy a pointer (8 bytes), which is extremely cheap. Our approach allocates nothing for small objects (SBO), but move operations require copying 32+ bytes.

### Indirect Invocation Overhead

The invocation overhead is identical for both approaches—one indirect function call. Both our implementation and Chromium's dispatch via function pointers. Under optimization, this indirect call cannot be inlined away.

### Trade-off Summary

| Metric | Our Approach | Chromium Approach |
|--------|--------------|-------------------|
| Callback Object Size | 56-64 bytes | 8 bytes |
| Small Lambda Heap Alloc | No allocation (SBO) | Always allocates |
| Move Cost | Copy 32+ bytes | Copy 1 pointer |
| Implementation Code Size | ~200 lines | ~2000+ lines |

We sacrificed object compactness and极致 performance of move operations for implementation simplicity—no need to manually write reference counting, function pointer tables, or `annotate` attributes. Zero heap allocation for small lambdas can actually be an advantage in low-frequency scenarios. For educational purposes and most practical scenarios, this trade-off is worth it.

---

## Summary

In this article, we did two things. Regarding testing, we designed 12 Catch2 test cases around six invariants (basic invocation, move semantics, single invocation, argument binding, cancellation mechanism, and chaining), covering all core behaviors of OnceCallback. Regarding performance, we compared differences with Chromium OnceCallback in object size, allocation behavior, and invocation overhead—our implementation traded compactness for simplicity.

With this, the design, implementation, and verification of the OnceCallback component are fully complete. Across 13 articles, from prerequisite knowledge to practice, we have covered the complete knowledge chain from C++11 move semantics to C++23 deducing this. I hope this series helps you understand "how to design an industrial-grade component with modern C++"—not just writing code, but more importantly, understanding the reasoning behind every design decision.

## Reference Resources

- [Chromium base/functional/ source directory](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [Catch2 Documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
