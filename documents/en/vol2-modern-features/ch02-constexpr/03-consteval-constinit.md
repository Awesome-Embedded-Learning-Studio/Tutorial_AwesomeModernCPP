---
chapter: 2
cpp_standard:
- 20
- 23
description: 'C++20 Immediate Functions and Compile-Time Initialization: Precise Distinction
  and Selection Strategies for `constexpr`'
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 2: constexpr 基础'
reading_time_minutes: 15
related:
- constexpr 构造函数与字面类型
tags:
- host
- cpp-modern
- intermediate
- consteval
- constinit
- 编译期计算
title: 'consteval and constinit: New Tools for Compile-Time Guarantees'
translation:
  source: documents/vol2-modern-features/ch02-constexpr/03-consteval-constinit.md
  source_hash: 6fe9e473bc3963ab494aa986351bad7353725cf1be8c667a49837d50159a736c
  translated_at: '2026-06-16T03:56:47.880207+00:00'
  engine: anthropic
  token_count: 2855
---
# consteval and constinit: New Tools for Compile-Time Guarantees

## Introduction

In the previous two chapters, we discussed `constexpr`—the keyword that means "may be evaluated at compile time." The word "may" is both its strength and its weakness. When you declare a `constexpr` function, you express the intent that "this function *can* be evaluated at compile time," but the compiler does not guarantee that it *will* do so.

It is worth noting that modern compilers (with optimizations enabled) are quite intelligent—even if you assign the return value to a non-`constexpr` variable, as long as the arguments are constants and the function call is simple enough, the compiler may still evaluate it at compile time. However, in certain complex scenarios, or when compiler optimizations are disabled (such as `-O0`), `constexpr` functions can indeed degrade into runtime calls. This uncertainty is exactly what `consteval` aims to solve.

This "flexibility" is a good thing most of the time, but there are scenarios where you need a hard guarantee: this function *must*, *absolutely*, *positively* execute at compile time. Examples include compile-time hashing and compile-time configuration validation—if these degrade into runtime calculations, you might not notice the issue during code review, only discovering it during performance profiling or when a runtime error occurs. `consteval` exposes such issues at the compilation stage through mandatory compile-time checks.

C++20 introduced two new keywords to solve this problem: functions declared with `consteval` (called "immediate functions") must be evaluated at compile time, while `constinit` guarantees that static variables are initialized at compile time. They are not replacements for `constexpr`, but rather refined supplementary tools.

## Step 1 — consteval: Forcing Compile-Time Evaluation

### Core Differences Between consteval and constexpr

Functions declared with `consteval` are called "immediate functions." Their semantics are very direct: any call to such a function must produce a compile-time constant. If the compiler finds that a call context cannot be evaluated at compile time, it results in a direct error.

```cpp
// consteval version
consteval int sqr(int n) {
    return n * n;
}

constexpr int x = sqr(10); // OK: evaluated at compile time

// int y = 20;
// int z = sqr(y);        // ERROR: call to consteval function is not a constant expression
```

Compare this with the `constexpr` version:

```cpp
// constexpr version
constexpr int sqr(int n) {
    return n * n;
}

constexpr int x = sqr(10); // OK: evaluated at compile time

int y = 20;
int z = sqr(y);            // OK: degrades to runtime call
```

The difference is clear at a glance: a `constexpr` function will "compromise" when faced with runtime arguments, automatically degrading to runtime execution; a `consteval` function will "refuse" runtime arguments, directly causing a compilation failure. You can think of `consteval` as "`constexpr` with mandatory compile-time guarantees."

### Applicable Scenarios for consteval

`consteval` is best suited for calculations that "are meaningless or even risky to execute at runtime."

The first typical scenario is compile-time ID and hash generation. In protocol handling and command dispatching, we often need to map strings to integer IDs. If the string-to-ID hash calculation is performed at runtime, it wastes CPU cycles and loses the ability for compile-time conflict detection.

```cpp
// Compile-time hash (FNV-1a variant)
consteval uint32_t compile_time_hash(const char* str, uint32_t value = 0x811C9DC5) {
    return (*str == '\0') ? value : compile_time_hash(str + 1, ((value ^ *str) * 0x01000193));
}

// Usage: compile-time dispatch
constexpr uint32_t HASH_CMD_RESET = compile_time_hash("RESET");
// If "RESET" is misspelled or changed, the ID changes at compile time, ensuring consistency.
```

The second typical scenario is compile-time configuration validation and constraint checking. When you need to ensure a configuration value meets specific constraints, using `consteval` forces validation at compile time, eliminating the possibility of discovering configuration errors at runtime.

```cpp
consteval int validate_clock_divider(int div) {
    if (div <= 0 || div > 16) {
        throw "Invalid clock divider"; // Compile-time error
    }
    return div;
}

// Compiler error if value is invalid
constexpr int ValidDiv = validate_clock_divider(8);
// constexpr int InvalidDiv = validate_clock_divider(20); // Compile error!
```

The third scenario is compile-time type tags and metadata. When you need to embed compile-time information into the type system (such as peripheral descriptions or protocol field definitions), `consteval` ensures this metadata doesn't accidentally turn into a runtime object.

```cpp
struct PinInfo {
    const char* name;
    uint8_t port;
    uint8_t pin;
};

consteval PinInfo make_pin_info(const char* n) {
    return {n, 'A', 5};
}

constexpr PinInfo LED_PIN = make_pin_info("LED_STATUS");
```

### Propagation Rules of consteval

`consteval` has a propagation behavior that requires special attention: if a `consteval` function is called within another function, that outer function must also be `consteval` (or the call itself must be within a constant evaluation context).

```cpp
consteval int inner(int x) { return x + 1; }

// Error: outer must be consteval because it calls a consteval function
// int outer(int x) { return inner(x); }

// Correct: outer is also consteval
consteval int outer(int x) { return inner(x); }
```

C++23 (DR20, P2564R3) further adjusted propagation rules: if a `consteval` function is called within a `constexpr` function, no error is reported as long as the call to that `constexpr` function ultimately resides in a constant evaluation context. This makes the combination of `consteval` and `constexpr` more flexible.

### if consteval: Compile-Time/Runtime Dispatch

C++23 introduced `if consteval` (also known as `#!cpp if !consteval`), allowing functions to select different code paths based on whether they are currently in a constant evaluation context.

```cpp
constexpr int compute(int x) {
    if consteval {
        // Optimized path for compile-time
        return x * x;
    } else {
        // Fallback for runtime (if needed)
        return x * x;
    }
}
```

`if consteval` and `if constexpr` are different things. `if constexpr` selects branches at compile time based on template parameters, while `if consteval` selects based on whether the current context is a constant evaluation context. The latter is better suited for providing different implementation strategies for compile-time and runtime within the same function.

## Step 2 — constinit: Solving Static Initialization Problems

### The Static Initialization Order Fiasco

Before discussing `constinit`, we need to understand the problem it solves. In C++, the initialization of objects with static storage duration (global variables, `static` class member variables, etc.) happens in two stages:

The first stage is **static initialization**, which includes zero initialization and constant initialization. These occur during program loading, even before the `main` function starts, and their order is deterministic—zero initialization happens before constant initialization.

The second stage is **dynamic initialization**, which requires the participation of runtime code. The problem is that the order of dynamic initialization between different translation units is undefined. If you have two files, `a.cpp` and `b.cpp`, each with a global object, and the object in `b.cpp` depends on the value of the object in `a.cpp` during initialization, you might encounter the "Static Initialization Order Fiasco" (SIOF).

```cpp
// a.cpp
int config_value = 100; // Dynamic initialization

// b.cpp
extern int config_value;
int derived_value = config_value * 2; // Depends on config_value
// If config_value is not initialized when derived_value is initialized, derived_value is wrong.
```

The terrifying aspect of this bug is that it is "luck-dependent"—it works under certain linking orders but crashes under others, and only occurs during program startup, making debugging extremely difficult.

### Semantics of constinit

The semantics of `constinit` are concise and powerful: it applies to variable declarations with static or thread storage duration, asserting that the variable must undergo constant initialization. If the compiler discovers that this variable requires dynamic initialization, it results in a compilation error.

```cpp
// Guaranteed to be constant initialized
constinit int safe_config = 100;

// Error: initializer is not a constant expression
// constinit int unsafe_config = std::rand();
```

### constinit vs constexpr: Subtle but Critical Differences

Both `constexpr` and `constinit` involve compile time, but they focus on different dimensions. A `constexpr` variable requires the value to be determined at compile time and the object itself is immutable—you cannot modify it. A `constinit` variable also requires the initial value to be determined at compile time, but the object itself can be modified.

```cpp
// constexpr: Immutable, compile-time value
constexpr int ImmConfig = 100;
// ImmConfig = 200; // Error: cannot modify constexpr variable

// constinit: Mutable, compile-time initialization
constinit int MutConfig = 100;
MutConfig = 200;       // OK: can modify constinit variable
```

This difference seems small, but it is very useful in actual engineering. For example, a global configuration buffer: you want its initial value set at compile time (to avoid SIOF), but its content needs to be updated during program execution. `constinit` meets this need perfectly.

It is worth noting that `constinit` cannot be used simultaneously with `constexpr`—they are mutually exclusive. A `constexpr` variable implicitly guarantees constant initialization (and `const` semantics), so adding `constinit` is redundant.

### constinit with thread_local

`constinit` has a very practical side effect: when applied to `thread_local` variables, it can eliminate the overhead of runtime thread-safety checks.

```cpp
// Without constinit: Runtime check required on first access
thread_local int tls_counter = 0;

// With constinit: No runtime check needed
constinit thread_local int tls_counter_fast = 0;
```

Ordinary `thread_local` variables need to check if they have been initialized upon first access, which usually involves a hidden guard variable and possible atomic operations. With `constinit`, the compiler knows this variable has a definite initial value when the program loads, so it can theoretically optimize away runtime checks. However, actual performance gains depend on the specific compiler implementation—testing on GCC 15.2 (`-O3`), the optimization margin is limited (about 5%), but there might be more significant improvements in certain compilers or scenarios.

### constinit in extern Declarations

`constinit` can be used in non-initializing declarations (such as `extern` declarations) to tell the compiler "this variable has been declared with `constinit` elsewhere; it does not need runtime initialization checks."

```cpp
// config.h
extern constinit int global_config; // Declaration: tells the compiler it's constinit

// config.cpp
constinit int global_config = 500;  // Definition
```

This is particularly useful in large projects—the `constinit` declaration in the header file acts as "compile-time documentation," telling users that the initialization behavior of this global variable is deterministic.

## Step 3 — Comparison and Selection Strategy

Now that we understand the semantics of the three keywords, let's make a clear comparison.

| Feature | `constexpr` | `consteval` | `constinit` |
|---------|------------|-------------|-------------|
| Applicable Targets | Variables, functions | Functions, constructors | Static/thread-local variables |
| Compile-Time Guarantee | "Can" be evaluated at compile time | "Must" be evaluated at compile time | Initialization must be constant initialization |
| Runtime Behavior | Can degrade to runtime call | No runtime calls allowed | Variable can be modified at runtime |
| Mutability | Immutable (implicit `const`) | N/A | Mutable |
| Problem Solved | Flexibility of compile-time calculation | Forcing compile-time evaluation | Avoiding SIOF |

To summarize the selection strategy in one sentence: if the value never changes, use a `constexpr` variable; if the function must execute at compile time, use `consteval`; if a global variable needs compile-time initialization but is modifiable at runtime, use `constinit`. For functions, default to `constexpr` (it is the most flexible), and only upgrade to `consteval` when you truly need to force compile-time evaluation.

### Common Combination Patterns

In actual projects, these three keywords are often used in combination.

**Pattern 1: `consteval` function generating `constexpr` values.** The result of a `consteval` function call is naturally a constant expression, so it can be received by a `constexpr` variable.

```cpp
consteval int get_magic_number() { return 42; }
constexpr int Magic = get_magic_number();
```

**Pattern 2: `constexpr` function with `constinit` global state.** The function itself does not force compile-time evaluation, but when used to initialize a `constinit` variable, the compiler forces its execution at compile time.

```cpp
constexpr int calculate_config() { return 1024; }
constinit int SystemConfig = calculate_config(); // Forces compile-time execution
```

**Pattern 3: `consteval` for compile-time validation.** Use `consteval` on validation logic to ensure it executes at compile time,配合 `static_assert` to produce compilation errors.

```cpp
consteval bool check_alignment(size_t n) { return n % 4 == 0; }
static_assert(check_alignment(8), "Must be 4-byte aligned");
```

## Common Pitfalls

### Addresses of consteval Functions Cannot Be Used at Runtime

You cannot obtain a function pointer to a `consteval` function and call it at runtime. The address of a `consteval` function can be used at compile time (for example, passed in a `constexpr` context), but it cannot "escape" to runtime. Attempting to take the address of a `consteval` function in a non-constant evaluation context will result in a compilation error. This is because `consteval` functions have no runtime entity—they are completely expanded and inlined at compile time.

### constinit Does Not Mean const

This point is easy to confuse. `constinit` only says that the initialization is constant initialization; the object itself is not necessarily `const`. If you need a global variable that is initialized at compile time and is also immutable, you should use `constexpr` (not `constinit`, although the latter would also work).

### Interaction of consteval with Templates

`consteval` can be used in function templates, but be careful: if the template instantiation cannot satisfy `consteval` requirements (for example, if it internally calls a non-`consteval` function), the compiler will report an error. This differs from `constexpr` function templates—a `constexpr` template only needs at least one set of arguments to work at compile time, whereas `consteval` requires *all* calls to be completed at compile time.

## Run Online

Run the `consteval` and `constinit` examples online to observe C++20 compile-time guarantees:

<OnlineCompilerDemo
  title="consteval and constinit: C++20 Compile-Time Guarantees"
  source-path="code/examples/vol2/06_consteval_constinit.cpp"
  description="Run online to observe consteval forced compile-time hashing and constinit mutable global variables."
  allow-run
/>

## Summary

C++20's `consteval` and `constinit` are precise supplements to the `constexpr` system. `consteval` fills the gap for "I want to force compile-time evaluation," while `constinit` solves C++'s long-standing static initialization order problem. The three have their own division of labor: `constexpr` provides flexibility, `consteval` provides enforcement, and `constinit` provides initialization safety. Understanding their precise differences and making reasonable choices is the key to writing high-quality compile-time calculation code.

In the next chapter, we will enter practical application, comprehensively using this knowledge to implement compile-time table lookups, string processing, and state machine design.

## References

- [cppreference: consteval specifier (C++20)](https://en.cppreference.com/w/cpp/language/consteval)
- [cppreference: constinit specifier (C++20)](https://en.cppreference.com/w/cpp/language/constinit)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [C++ Stories: const vs constexpr vs consteval vs constinit in C++20](https://www.cppstories.com/2022/const-options-cpp20/)
