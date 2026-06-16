---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: Semantics and pitfalls of value capture, reference capture, and init
  capture
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 3: Lambda 基础'
reading_time_minutes: 15
related:
- 泛型 Lambda 与模板 Lambda
tags:
- host
- cpp-modern
- intermediate
- lambda
title: Deep Dive into Lambda Capture Mechanisms
translation:
  source: documents/vol2-modern-features/ch03-lambda/02-lambda-capture.md
  source_hash: e74d69f3bc25b0df78416302d03ba8f74e7c659f9ef82f5278adeea7fd177023
  translated_at: '2026-06-16T03:57:06.607345+00:00'
  engine: anthropic
  token_count: 3089
---
# Deep Dive into Lambda Capture Mechanisms

## Introduction

In the previous chapter, we quickly reviewed the basic syntax of lambdas and briefly mentioned the existence of the capture list. However, you might still have a few questions in mind: What exactly does a value capture copy? Is a reference capture just a pointer under the hood? What are the pitfalls with default captures like `[=]` and `[&]`? What makes C++14 init capture so useful? In this chapter, we will dissect the capture mechanism from start to finish. We won't just cover "how to use it," but clearly explain "what the compiler does behind the scenes" and "which usages might explode at runtime."

> **Learning Objectives**
>
> - Understand the underlying semantics of value and reference capture—what exactly the closure type stores.
> - Master the usage and motivation behind C++14 init capture and C++17 `*this` capture.
> - Identify and avoid common capture-related pitfalls (dangling references, lifetime issues).
> - Understand the size and performance impact of lambda objects.

---

## Value Capture — Copying into the Closure Object

The semantics of value capture are very straightforward: at the moment the lambda is created, the captured variable is copied and stored as a member variable of the closure type. Subsequent modifications to the external variable will not affect the copy inside the lambda.

```cpp
int x = 10;
auto f = [x]() { return x + 1; };
x = 20;
assert(f() == 11); // The internal copy of x is still 10
```

From the compiler's perspective, the lambda above is roughly translated into a closure type like this:

```cpp
class ClosureType {
    int x; // Value capture stores a copy

public:
    ClosureType(int _x) : x(_x) {}

    int operator()() const {
        return x + 1;
    }
};
```

Note that `const`—members captured by value are `const` inside the `operator()` by default, so you cannot modify them. If you genuinely need to modify the captured copy inside the lambda, you need to add the `mutable` keyword:

```cpp
int x = 0;
auto f = [x]() mutable {
    x += 1; // OK: x is mutable inside the lambda
    return x;
};
```

`mutable` tells the compiler: this lambda's `operator()` is not `const`. Each invocation might modify the internal state of the closure object. This is why calling `f()` repeatedly increments the value—the closure object maintains its own independent state.

---

## Reference Capture — Storing the Address of the Original Variable

The semantics of reference capture are not mysterious either: the compiler stores a pointer to the captured variable (or a reference, which is basically equivalent in underlying implementation) within the closure type. We can verify this via `sizeof`: the size of a reference-capturing closure object equals the size of a pointer (8 bytes on a 64-bit system). Reads and writes to the captured variable inside the lambda are actually operations on the original variable.

```cpp
int x = 10;
auto f = [&x] { x += 1; }; // Reference capture
f();
assert(x == 11);
```

The corresponding closure type looks roughly like this:

```cpp
class ClosureType {
    int& ref; // Reference capture stores a reference (pointer)

public:
    ClosureType(int& _ref) : ref(_ref) {}

    void operator()() const {
        ref += 1; // Modifying the original object
    }
};
```

Here is an interesting detail: `operator()` is `const`, yet we modified an external variable through `ref`. This is because the reference itself (the stored address) is `const`—you cannot make the reference point to a different object—but the value of the object bound to the reference can be modified. This is analogous to a `const` pointer: you can't change the pointer, but you can change the data it points to.

> **Verification**: You can run `godbolt` to verify the underlying implementation details of reference capture and `const` semantics.

The biggest advantage of reference capture is zero copy—for large objects (like `std::vector`, `std::string`), reference capture avoids unnecessary copying. But the greatest risk lies here as well: **the referenced variable must outlive the lambda.**

---

## Default Capture — The Pitfalls of `[=]` and `[&]`

When there are many variables to capture, listing them one by one can be tedious. C++ provides two default capture modes: `[=]` means all used external variables are captured by value, and `[&]` means all are captured by reference.

```cpp
int x, y;
auto f1 = [=] { return x + y; }; // Capture x and y by value
auto f2 = [&] { return x + y; }; // Capture x and y by reference
```

You can also specify different modes for individual variables on top of the default capture—mixed capture:

```cpp
auto f = [=, &y] { return x + y; }; // x by value, y by reference
```

This sounds convenient, but `[=]` and `[&]` have a few inconspicuous traps. Before C++20, `[=]` could implicitly capture `this` pointer. This led to a classic problem: you think you are capturing the value of a member variable, but you are actually capturing the `this` pointer, and accessing the member inside the lambda still goes to the original object. C++20 fixed this behavior; `[=]` no longer implicitly captures `this`, requiring you to explicitly write `*this` or `this`.

> **Verification**: You can run `godbolt` to observe the behavioral difference between C++17 and C++20 regarding default capture of `this` (C++20 will issue a warning).

The author's advice is: **try to explicitly list the variable names you want to capture in production code**, and use `[=]` and `[&]` sparingly. The benefit of being explicit is that during code review, you can immediately see which external states the lambda depends on, and it avoids accidentally capturing things that shouldn't be captured. (Capture all, unless your code is trivial enough, otherwise you might not know what you're getting and problems may arise.)

---

## C++14 Init Capture — Lambda Owns Its State

C++14 introduced init capture, sometimes called generalized lambda capture. The syntax is `var = expression` in the capture list, where `var` is a new variable name and `expression` is the initialization expression. This variable belongs entirely to the closure object and has no relation to the outside:

```cpp
auto f = [v = 10]() { return v + 1; }; // v is a member of the closure
```

The most useful scenario for init capture is **move capture**—moving move-only types (`std::unique_ptr`, `std::ofstream`, etc.) into the closure object:

```cpp
auto ptr = std::make_unique<int>(42);
auto f = [p = std::move(ptr)] { return *p; }; // Move unique_ptr into lambda
```

In C++11, to achieve the same effect, you had to manually write a functor class and make `std::unique_ptr` a member variable. C++14 init capture makes this very natural.

Another common usage is using init capture to replace `static` counters, with clearer semantics:

```cpp
// C++11 style
auto f = [&]() {
    static int counter = 0;
    return ++counter;
};

// C++14 style
auto f = [counter = 0]() mutable {
    return ++counter;
};
```

The benefit of the second version is that `counter` is entirely the lambda's own state, with no relation to the external variable `counter`—the name itself makes it clear that this is an independent counter.

---

## C++17 `*this` Capture — Capturing the Whole Object by Value

When writing a lambda inside a member function, if you want to capture the current object, the traditional way is `this`. But `this` captures a pointer. If the lambda's lifetime exceeds the object itself, you end up with a dangling `this` pointer. C++17 introduced `*this`, which captures the entire object by value—storing a copy of the object in the closure type:

```cpp
class Widget {
    std::string name;
public:
    void func() {
        auto f = [*this] { return name; }; // Captures a copy of *this
    }
};
```

The cost of `*this` is copying the entire object. If the object is large (contains `std::vector`, large arrays, etc.), this copy overhead might be significant. But for small configuration objects or value types, the safety gained by this copy is well worth it.

⚠️ **Note**: `*this` requires that the current lambda context is a member function where `this` can be dereferenced. It cannot be used in static member functions or non-member functions.

---

## Capture Pitfalls — Dangling References and Lifetimes

The most common and headache-inducing source of bugs in capture mechanisms is lifetime issues. Let's look at a few classic trap scenarios.

### Returning a Reference-Captured Lambda

```cpp
auto make_counter(int& count) {
    return [&count] { return ++count; }; // DANGER! count is destroyed
}
```

The fix is simple—use value capture or init capture instead of reference capture:

```cpp
auto make_counter(int& count) {
    return [count]() mutable { return ++count; }; // Safe: owns its own copy
}
```

### Reference Capture in Loops

This trap is particularly common in asynchronous programming and event systems:

```cpp
std::vector<std::function<void()>> tasks;
for (int i = 0; i < 3; ++i) {
    tasks.push_back([&i] { std::cout << i << std::endl; });
}
// All lambdas refer to the same i, which is now 3!
```

### Risks of Capturing `this`

```cpp
class Button {
    void onClick() {
        // If this lambda is stored and called later, 'this' might be invalid
        callbacks.push_back([this] { handle(); });
    }
};
```

---

## Lambda Object Size Analysis

Once you understand the underlying storage mechanism of captures, the size of a lambda object is easy to understand—it is the sum of the sizes of all captured variables (plus some alignment padding). A standard lambda has no virtual table pointer; the closure type is a normal class type. We can verify this with `sizeof`:

```cpp
int x = 0;
int y = 0;
auto empty = [] {};
auto cap_val = [x] {};
auto cap_ref = [&x] {};
auto cap_both = [x, &y] {};

std::cout << sizeof(empty)   << "\n"; // 1
std::cout << sizeof(cap_val) << "\n"; // 4
std::cout << sizeof(cap_ref) << "\n"; // 8 (pointer size)
std::cout << sizeof(cap_both)<< "\n"; // 12 (4 + 8 + padding)
```

Typical output (64-bit system, GCC):

```text
1
4
8
16
```

One noteworthy point: the size of a capture-less lambda is usually 1 byte, not 0 bytes—C++ does not allow objects of size 0 (otherwise element addresses in an array would be indistinguishable). Reference capture stores a pointer, which takes 8 bytes on a 64-bit system.

> **Verification**: You can run `godbolt` to view the actual size of closure objects under various capture modes.

When you store a lambda in `std::function`, the storage space is more than this—`std::function` usually has its own SBO buffer (32-64 bytes), plus type erasure management overhead. This is why we said in the previous chapter "prefer `std::function` to store lambdas" (Wait, actually prefer auto or templates, `std::function` has overhead). *Correction: Prefer `auto` or templates for storing lambdas.*

---

## Performance Considerations — When to Inline, When Not To

The performance characteristics of a lambda are closely related to its capture method and storage method.

When a lambda is called with a type known at compile time (`auto` or template parameter), the compiler can see the complete closure type and `operator()` implementation, allowing for perfect inlining. In this case, the difference between value and reference capture is basically zero—even if value capture involves a copy, the compiler can usually eliminate this copy cost after optimization.

However, if the lambda is stored in `std::function`, the situation is different. The type erasure of `std::function` introduces a layer of indirection. The compiler cannot inline across this indirection. Moreover, if the captured content exceeds the SBO buffer size of `std::function`, it triggers heap allocation.

```cpp
// Fast: compile-time type, easy to inline
template<typename F>
void run_fast(F&& f) {
    f();
}

// Slow: type erasure, indirect call
void run_slow(std::function<void()> f) {
    f();
}
```

With optimizations enabled (-O2/-O3), the `run_fast` version is typically about 2-3x faster than the `run_slow` version (specific numbers depend on the compiler, optimization level, and lambda complexity). Benchmarks (GCC 13.2.0, -O3) show that when processing 10 million elements, the `run_fast` version takes about 6-7 ms, while the `run_slow` version takes about 14-15 ms. The trend is clear: **when you don't need runtime polymorphism, using templates or `auto` to pass lambdas is the optimal choice.**

> **Verification**: You can run `quick_bench` to reproduce this performance test (requires -O3 optimization).

---

## Choosing a Capture Method — A Decision Guide

Let's summarize the choice of capture methods into a few simple rules:

For small, immutable data (`int`, `float`, simple structs), value capture is the safest default. It ensures the lambda doesn't depend on external state, is thread-safe, and avoids lifetime issues. For large objects (`std::vector`, `std::string`), if the lambda needs to read but not modify, reference capture plus `const` is a zero-copy solution; if the lambda needs to own the object independently, use init capture `var = std::move(obj)` to move it into the closure. For external variables that need to be modified inside the lambda (accumulators, state updates), reference capture is the most natural choice, but ensure the variable's lifetime is sufficient.

In member functions, if the lambda does not escape the object's lifetime, `this` is convenient; if the lambda might outlive the object, use `*this` (C++17) or init capture for the specific member variables needed. In production code, the author strongly recommends explicitly listing the names of captured variables and avoiding `[=]` and `[&]`—explicit code makes code review easier and reduces accidental captures.

---

## Try It Online

Run the Lambda capture mechanism examples online and compare the effects of different capture methods:

<OnlineCompilerDemo
  title="Lambda Capture Mechanisms: Value, Reference, and Closure Size"
  source-path="code/examples/vol2/09_lambda_capture.cpp"
  description="Run online to compare the behavioral differences between value capture, reference capture, mutable, and init capture."
  allow-run
/>

## Summary

The lambda capture mechanism is key to understanding lambda performance and safety. Core takeaways:

- Value capture copies variables into the closure object, default `const`, `mutable` allows modification of the internal copy.
- Reference capture stores the variable's address/reference, zero-copy but requires guaranteeing lifetime.
- C++14 init capture allows lambdas to have independent state and supports move capture.
- C++17 `*this` capture copies the entire object by value, solving the dangling pointer problem of `this`.
- The size of a lambda object equals the sum of the sizes of all captured variables.
- When runtime polymorphism is not needed, passing lambdas via `auto` or template parameters yields the best performance.

## References

- [Lambda capture - cppreference](https://en.cppreference.com/w/cpp/language/lambda#Lambda_capture)
- [C++14 generalized lambda capture](https://en.cppreference.com/w/cpp/language/lambda#Captures)
- [C++17 capture *this](https://en.cppreference.com/w/cpp/language/lambda#Lambda_capture)
