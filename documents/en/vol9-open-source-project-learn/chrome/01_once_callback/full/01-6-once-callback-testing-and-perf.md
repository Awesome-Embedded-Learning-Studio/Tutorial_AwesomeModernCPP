---
title: 'OnceCallback in Practice (Part 6): Testing and Performance Comparison'
description: We systematically design six categories of test cases to verify all core
  behaviors of `OnceCallback`, and compare the performance differences against the
  original Chromium implementation and standard library solutions.
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
  source_hash: 2dfbbd169402f28789a1fe88fb11db240e59ec83f8a570b0880a99b0abb1b0bb
  translated_at: '2026-06-13T11:54:42.854860+00:00'
  engine: anthropic
  token_count: 1889
---
# OnceCallback in Practice (Part 6): Testing and Performance Comparison

## Introduction

At this point, the four core features of OnceCallback—core skeleton, move semantics, cancellation tokens, and `Then` chaining—have all been implemented. In this article, we will do two things: first, systematically review the testing strategy to ensure the implementation is correct under various boundary conditions; second, analyze the performance differences between our implementation, the original Chromium version, and standard library approaches, to understand exactly what we traded away and what we gained.

> **Learning Objectives**
>
> - Master the method of organizing test cases by invariants.
> - Understand the design intent and key assertions of the six test categories.
> - Clarify the performance trade-offs between our OnceCallback and the Chromium original.

---

## Setting Up the Test Framework

We use Catch2 v3 as our testing framework, automatically fetching dependencies via CPM (CMake Package Manager).

```cmake
# tests/CMakeLists.txt
cpmaddpackage("gh:catchorg/Catch2@3.4.0")

# Enable Catch2's main function generator
catch_discover_tests(sources)
```

Catch2's `REQUIRE` macro is superior to `assert` because it reports the specific failed expression, file, and line number, and continues executing subsequent checks within the same section. `REQUIRE_THROWS_AS` is specifically used to verify exception types.

To run the tests: execute `ctest` in the `build` directory.

---

## Six Categories of Test Cases

We organize the tests into six categories, each focusing on a specific design invariant. Organizing tests by invariants rather than by features makes it less likely to miss boundary conditions.

### Category A: Basic Invocation and Return Values

```cpp
TEST_CASE("A: Basic invocation and return values", "[once_callback]") {
    SECTION("Non-void callback returns correct value") {
        OnceCallback<int()> cb = [] { return 42; };
        REQUIRE(cb() == 42);
    }

    SECTION("Void callback executes normally") {
        bool called = false;
        OnceCallback<void()> cb = [&called] { called = true; };
        cb();
        REQUIRE(called);
    }
}
```

This verifies the most basic construction and invocation behavior—non-void callbacks return the correct value, and void callbacks execute normally. The void return path takes a different branch in `operator()`.

### Category B: Move Semantics

```cpp
TEST_CASE("B: Move semantics", "[once_callback]") {
    SECTION("Move-only capture works") {
        MoveOnly mo(1);
        OnceCallback<void()> cb = [mo = std::move(mo)] { REQUIRE(mo.value == 1); };
        std::move(cb)();
    }

    SECTION("Move construction empties source") {
        OnceCallback<int()> src = [] { return 10; };
        OnceCallback<int()> dst = std::move(src);
        REQUIRE(src.IsEmpty()); // src is now empty
        REQUIRE(dst() == 10);
    }
}
```

The move-only capture test verifies that OnceCallback truly supports move-only callables—if the underlying implementation used `std::function` instead of a custom storage, this code would fail to compile. The move semantics test verifies that the source object becomes `kEmpty` after move construction.

There is a conceptual point that is easily confused—move operations transfer ownership, but do not trigger consumption. Only `operator()` consumes the callback. `std::move` merely transfers ownership; the callback remains active until it is actually invoked.

### Category C: Single-Invocation Constraint

This constraint is implemented via deducing this + `consteval`—attempting to call `operator()` on a const reference triggers a compile error; only calling on an rvalue reference passes. No runtime test is needed; the compilation success itself is the verification.

### Category D: Argument Binding

```cpp
TEST_CASE("D: Argument binding", "[once_callback]") {
    SECTION("Partial binding for lambdas") {
        auto add = [](int a, int b) { return a + b; };
        OnceCallback<int()> cb = OnceCallback<int(int, int)>(add).Bind(10, 20);
        REQUIRE(cb() == 30);
    }

    SECTION("Member function binding") {
        struct Widget {
            int Value() { return 99; }
        };
    Widget w;
    OnceCallback<int()> cb = OnceCallback<int()>::From<&Widget::Value>(&w);
    REQUIRE(cb() == 99);
    }
}
```

This covers partial argument binding for normal lambdas and member function binding. The lifetime trap with member function binding was discussed in previous articles—`this` is a raw pointer, so the caller is responsible for safety.

### Category E: Cancellation Mechanism

```cpp
TEST_CASE("E: Cancellation mechanism", "[once_callback]") {
    SECTION("Token valid: callback executes") {
        auto token = std::make_shared<CancellationToken>();
        OnceCallback<int()> cb = [token](int x) { return x * 2; };
        REQUIRE(cb(5) == 10);
    }

    SECTION("Token invalid: void callback skips execution") {
        auto token = std::make_shared<CancellationToken>();
        OnceCallback<void()> cb = [token] { FAIL("Should not be called"); };
        token->Invalidate();
        cb(); // Should skip without failing
    }

    SECTION("Token invalid: non-void callback throws CallbackCanceled") {
        auto token = std::make_shared<CancellationToken>();
        OnceCallback<int()> cb = [token](int x) { return x * 2; };
        token->Invalidate();
        REQUIRE_THROWS_AS(cb(5), CallbackCanceled);
    }
}
```

Three key behaviors: callback executes when the token is valid; void callback skips execution when the token is invalid; non-void callback throws `CallbackCanceled` when the token is invalid.

### Category F: Then Composition

```cpp
TEST_CASE("F: Then composition", "[once_callback]") {
    SECTION("Two-stage non-void pipeline") {
        OnceCallback<int()> first = [] { return 10; };
        OnceCallback<std::string()> second = first.Then([](int x) {
            return std::to_string(x * 2);
        });
        REQUIRE(second() == "20");
    }

    SECTION("Multi-stage pipeline crossing type boundaries") {
        OnceCallback<int()> a = [] { return 5; };
        OnceCallback<std::string()> b = a.Then([](int i) { return std::to_string(i); });
        OnceCallback<size_t()> c = b.Then([](std::string s) { return s.size(); });
        REQUIRE(c() == 1);
    }

    SECTION("Void prefix callback") {
        bool called = false;
        OnceCallback<void()> first = [&called] { called = true; };
        OnceCallback<int()> second = first.Then([] { return 42; });
        REQUIRE(!called); // Not called yet
        REQUIRE(second() == 42);
        REQUIRE(called);
    }
}
```

This covers three composition patterns: two-stage non-void pipelines, multi-stage pipelines (crossing type boundaries from int to string), and void prefix callbacks.

---

## Performance Comparison: vs. Chromium Original

### Object Size

```cpp
static_assert(sizeof(OnceCallback<void()>) <= 64, "OnceCallback is too large");
```

On GCC, typical values are `std::function` at about 32 bytes, `std::move_only_function` at about 32 bytes, and our `OnceCallback` at about 56-64 bytes. Chromium's is only 8 bytes.

The root of the difference lies in the storage strategy. Chromium places all state on the heap in a `ref_ptr`, and the callback object holds only a single pointer. We use SBO (Small Buffer Optimization) to store small objects inline, avoiding heap allocation but increasing the object size.

### Allocation Behavior

The SBO threshold for `std::function` is typically 2-3 pointer sizes (16-24 bytes). Lambdas capturing a small number of arguments usually fit into SBO and do not trigger heap allocation. Large lambdas trigger heap allocation upon construction.

Chromium always allocates on the heap (via `new`), but allocation only happens once. Subsequent move operations of OnceCallback simply copy a pointer (8 bytes), which is extremely cheap. Our approach allocates nothing for small objects (SBO), but move operations require copying 32+ bytes.

### Indirect Invocation Overhead

The invocation overhead is identical for both approaches—one indirect function call. Both our `manager` and Chromium's `Invoke` function dispatch via function pointers. Under `-O2` optimization, this indirect call cannot be inlined away.

### Trade-off Summary

| Metric | Our Approach | Chromium Approach |
|--------|--------------|-------------------|
| Callback object size | 56-64 bytes | 8 bytes |
| Small lambda heap allocation | No allocation (SBO) | Always allocates |
| Move cost | Copy 32+ bytes | Copy 1 pointer |
| Implementation code size | ~200 lines | ~2000+ lines |

We sacrificed object compactness and极致 performance of move operations for implementation simplicity—no need to manually write reference counting, function pointer tables, or `ref_ptr` annotations. Zero heap allocation for small lambdas can actually be an advantage in certain low-frequency scenarios. For educational purposes and most practical scenarios, this trade-off is worth it.

---

## Summary

In this article, we did two things. Regarding testing, we designed 12 Catch2 test cases around six invariants (basic invocation, move semantics, single invocation, argument binding, cancellation mechanism, and chaining), covering all core behaviors of OnceCallback. Regarding performance, we compared differences with Chromium's OnceCallback in object size, allocation behavior, and invocation overhead—our implementation traded compactness for simplicity.

With this, the design, implementation, and verification of the OnceCallback component are fully complete. The 13 articles cover the complete knowledge chain from C++11 move semantics to C++23 deducing this, starting from prerequisite knowledge to practical application. I hope this series helps you understand "how to design an industrial-grade component with modern C++"—not just writing code, but more importantly, understanding the reasons behind every design decision.

## References

- [Chromium base/functional/ source directory](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [Catch2 Documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
