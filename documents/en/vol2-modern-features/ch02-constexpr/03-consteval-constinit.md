---
title: 'consteval and constinit: New Tools for Compile-Time Guarantees'
description: C++20 immediate functions and compile-time initialization, precise distinctions
  from `constexpr`, and selection strategies
chapter: 2
order: 3
tags:
- host
- cpp-modern
- intermediate
- consteval
- constinit
- 编译期计算
difficulty: intermediate
platform: host
cpp_standard:
- 20
- 23
reading_time_minutes: 18
prerequisites:
- 'Chapter 2: constexpr 基础'
related:
- constexpr 构造函数与字面类型
translation:
  source: documents/vol2-modern-features/ch02-constexpr/03-consteval-constinit.md
  source_hash: 401080169dadf4b663ae4cb31f44826c2c222ed3397aa74b3f1cee528dff5c0e
  translated_at: '2026-05-26T11:24:41.831090+00:00'
  engine: anthropic
  token_count: 2861
---
# consteval and constinit: New Tools for Compile-Time Guarantees

## Introduction

In the previous two chapters, we discussed `constexpr`—the keyword that means a function *might* be evaluated at compile time. That "might" is both its strength and its weakness. When you declare a `constexpr` function, you express the intent that "this function can be evaluated at compile time," but the compiler does not guarantee it will actually do so.

It is worth noting that modern compilers (with optimizations enabled) are quite smart—even if you assign the return value to a non-`constexpr` variable, as long as the arguments are constants and the function call is simple enough, the compiler may still evaluate it at compile time. However, in certain complex scenarios, or when compiler optimizations are disabled (such as `-O0`), a `constexpr` function can indeed degrade into a runtime call. This uncertainty is exactly the problem `consteval` aims to solve.

This flexibility is a good thing most of the time, but in some scenarios you really need a hard guarantee: this function must, absolutely, unequivocally execute at compile time. For example, compile-time hashing, compile-time configuration validation—if these things degrade into runtime computations, you might not notice the issue during code review, only discovering it during profiling or when a runtime error occurs. `consteval` exposes such problems at the compilation stage through mandatory compile-time checks.

C++20 introduced two new keywords to solve this problem: functions declared with `consteval` (called "immediate functions") must be evaluated at compile time, while `constinit` guarantees that static variables complete their initialization at compile time. They are not replacements for `constexpr`, but rather fine-grained complementary tools.

## Step 1 — consteval: Forcing Compile-Time Evaluation

### Core Differences Between consteval and constexpr

Functions declared with `consteval` are called "immediate functions." Their semantics are very straightforward: any call to such a function must produce a compile-time constant. If the compiler finds that a call context cannot be evaluated at compile time, it directly reports an error.

```cpp
consteval int square(int x)
{
    return x * x;
}

// OK：参数是常量，上下文是 constexpr 变量初始化
constexpr int kResult = square(8);  // 编译通过，kResult == 64

// OK：参数是常量字面量
int arr[square(5)];  // OK，square(5) == 25，数组大小

// 错误！参数来自运行时
int runtime_val = 42;
// int bad = square(runtime_val);  // 编译错误：不是常量表达式
```

Compare this with the `constexpr` version:

```cpp
constexpr int square_maybe(int x)
{
    return x * x;
}

int runtime_val = 42;
int ok = square_maybe(runtime_val);  // OK！退化为运行时调用
```

The difference is clear at a glance: a `constexpr` function "compromises" when facing runtime arguments, automatically degrading into runtime execution; a `consteval` function "rejects" runtime arguments, directly causing a compilation failure. You can think of `consteval` as "`constexpr` with a mandatory compile-time guarantee."

### Applicable Scenarios for consteval

`consteval` is best suited for computations where "executing at runtime makes no sense or even introduces risk."

The first typical scenario is compile-time ID and hash generation. In protocol processing and command dispatch, we often need to map strings to integer IDs. If the string-to-ID hash calculation executes at runtime, it both wastes CPU cycles and loses the ability to detect collisions at compile time.

```cpp
#include <cstdint>
#include <cstddef>

consteval std::uint32_t fnv1a32(const char* str, std::size_t len)
{
    std::uint32_t hash = 0x811c9dc5u;
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= static_cast<std::uint8_t>(str[i]);
        hash *= 0x01000193u;
    }
    return hash;
}

template <std::size_t N>
consteval std::uint32_t command_id(const char (&s)[N])
{
    return fnv1a32(s, N - 1);
}

// 所有 ID 都在编译期生成，没有任何运行时开销
constexpr auto kIdStart = command_id("START");
constexpr auto kIdStop  = command_id("STOP");
constexpr auto kIdReset = command_id("RESET");

// 编译期验证：确保没有哈希冲突
static_assert(kIdStart != kIdStop);
static_assert(kIdStart != kIdReset);
static_assert(kIdStop != kIdReset);
```

The second typical scenario is compile-time configuration validation and constraint checking. When you need to ensure a configuration value meets specific constraints, using `consteval` forces the validation to complete at compile time, eliminating the possibility of discovering configuration errors only at runtime.

```cpp
consteval int validate_buffer_size(int size)
{
    // 如果约束不满足，直接编译错误
    return size > 0 && size <= 4096 && (size & (size - 1)) == 0
        ? size
        : throw "Buffer size must be a power of 2 between 1 and 4096";
    // 在 consteval 上下文中，throw 会导致编译错误
}

constexpr int kBufferSize = validate_buffer_size(1024);  // OK
// constexpr int kBadSize = validate_buffer_size(1000);  // 编译错误！不是 2 的幂
```

The third scenario is compile-time type tags and metadata. When you need to embed compile-time information in the type system (such as peripheral descriptions, protocol field definitions), `consteval` ensures these metadata objects do not accidentally become runtime objects.

```cpp
struct PeripheralTag {
    const char* name;
    std::uint32_t base_address;
    std::uint32_t clock_mask;

    consteval PeripheralTag(const char* n, std::uint32_t addr, std::uint32_t clk)
        : name(n), base_address(addr), clock_mask(clk) {}
};

consteval PeripheralTag make_usart1_tag()
{
    return PeripheralTag{"USART1", 0x40013800, 0x00004000};
}

constexpr auto kUsart1Tag = make_usart1_tag();
static_assert(kUsart1Tag.base_address == 0x40013800);
```

### Propagation Rules of consteval

`consteval` has a propagation behavior that requires special attention: if a `consteval` function is called within another function, that outer function must also be `consteval` (or the call itself must be in a constant evaluation context).

```cpp
consteval int forced_compile_time(int x) { return x * x; }

// 错误！constexpr 函数中调用 consteval 函数，
// 但该调用的结果不是常量表达式
constexpr int wrapper(int x)
{
    // return forced_compile_time(x);  // 编译错误
    return x * x;  // 需要自己实现逻辑
}

// OK：consteval 函数中可以调用 consteval 函数
consteval int double_square(int x)
{
    return forced_compile_time(x) * 2;
}

constexpr auto kVal = double_square(3);  // OK，kVal == 18
```

C++23 (DR20, P2564R3) further adjusted the propagation rules: if a `consteval` function is called within a `constexpr` function, as long as the call to that `constexpr` function ultimately occurs in a constant evaluation context, it no longer triggers an error. This makes the combination of `consteval` and `constexpr` more flexible.

### if consteval: Compile-Time/Runtime Dispatch

C++23 introduced `if consteval` (also known as `if !consteval`), which allows a function to choose different code paths based on whether it is currently in a constant evaluation context.

```cpp
#include <cstdio>
#include <cstddef>

constexpr std::size_t compute_hash(const char* str, std::size_t len)
{
    if consteval {
        // 编译期路径：使用纯 constexpr 的算法
        std::size_t hash = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < len; ++i) {
            hash ^= static_cast<std::size_t>(str[i]);
            hash *= 0x100000001b3ull;
        }
        return hash;
    } else {
        // 运行时路径：可以使用其他实现策略
        std::size_t hash = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < len; ++i) {
            hash ^= static_cast<std::size_t>(str[i]);
            hash *= 0x100000001b3ull;
        }
        // 运行时路径中，如果编译器支持内联 SIMD 指令，
        // 可能会自动向量化这段循环；也可以显式调用 SIMD 库
        return hash;
    }
}

constexpr auto kCompileTimeHash = compute_hash("test", 4);  // 走编译期路径
```

`if consteval` and `if constexpr` are different things. `if constexpr` selects a branch at compile time based on template parameters, while `if consteval` selects based on whether the current context is a constant evaluation context. The latter is better suited for providing different implementation strategies for compile-time and runtime within the same function.

## Step 2 — constinit: Solving the Static Initialization Problem

### The Static Initialization Order Fiasco

Before discussing `constinit`, we need to understand the problem it aims to solve. In C++, the initialization of objects with static storage duration (global variables, `static` class member variables, etc.) is divided into two phases:

The first phase is static initialization, including zero initialization and constant initialization. These occur during the program loading phase, even before the `main` function begins, and their order is well-defined—zero initialization happens before constant initialization.

The second phase is dynamic initialization, which requires the involvement of runtime code. The problem is that the order of dynamic initialization across different translation units is undefined. If you have two files, `a.cpp` and `b.cpp`, each with a global object, and the initialization of the object in `a.cpp` depends on the value of the object in `b.cpp`, you might encounter the "Static Initialization Order Fiasco" (SIOF).

```cpp
// a.cpp
#include <vector>
std::vector<int> g_data{1, 2, 3};  // 动态初始化：调用 vector 的构造函数

// b.cpp
extern std::vector<int> g_data;
int g_first_element = g_data[0];  // 可能读到未初始化的 g_data！
```

What makes this bug so terrifying is that it is "luck-dependent"—it works fine under certain link orders but crashes under others, and it only occurs during program startup, making it extremely difficult to debug.

### Semantics of constinit

The semantics of `constinit` are concise and powerful: it applies to variable declarations with static or thread storage duration, asserting that the variable must undergo constant initialization. If the compiler finds that this variable requires dynamic initialization, it directly reports a compilation error.

```cpp
#include <array>

// OK：std::array 的聚合初始化是常量初始化
constinit std::array<int, 4> g_table = {1, 2, 3, 4};

// OK：用 constexpr 函数的返回值初始化
constexpr int compute_value() { return 42; }
constinit int g_value = compute_value();

// 错误！get_runtime_value 不是常量表达式，需要动态初始化
// int get_runtime_value();
// constinit int g_bad = get_runtime_value();  // 编译错误
```

### constinit vs constexpr: Subtle but Critical Differences

Both `constinit` and `constexpr` involve compile-time, but they focus on different dimensions. A `constexpr` variable requires its value to be determined at compile time and the object itself to be `const`—you cannot modify it. A `constinit` variable also requires its initial value to be determined at compile time, but the object itself can be modified.

```cpp
constexpr int kConstVal = 42;        // 编译期值 + 不可修改
// kConstVal = 100;                  // 错误！constexpr 变量是 const 的

constinit int gMutableVal = 42;      // 编译期初始化 + 可修改
gMutableVal = 100;                   // OK！运行时可以改值
```

This difference may seem small, but it is very useful in practical engineering. For example, a global configuration buffer where you want the initial value to be set at compile time (to avoid SIOF), but its contents need to be updated during program execution. `constinit` perfectly meets this need.

It is worth noting that `constinit` cannot be used together with `constexpr`—they are mutually exclusive. A `constexpr` variable implicitly guarantees constant initialization (and `const` semantics), so adding `constinit` is redundant.

### constinit and thread_local

`constinit` has a very practical side effect: when applied to a `thread_local` variable, it can eliminate the overhead of runtime thread-safety checks.

```cpp
// 没有 constinit：每次访问都需要检查线程局部存储是否已初始化
thread_local int tl_counter = 42;

// 有 constinit：编译器知道初始化在加载时就完成了，
// 不需要运行时守卫变量（guard variable）
constinit thread_local int tl_fast_counter = 42;
```

An ordinary `thread_local` variable needs to check whether it has already been initialized on first access, which typically involves a hidden guard variable and possible atomic operations. With `constinit`, the compiler knows this variable already has a determined initial value at program load time, so it can theoretically optimize away the runtime checks. However, the actual performance improvement depends on the specific compiler implementation—in testing on GCC 15.2 (`-O2`), the optimization margin is limited (about 5%), but it may show more significant improvements with certain compilers or in certain scenarios.

### constinit in extern Declarations

`constinit` can be used in non-initializing declarations (such as `extern` declarations) to tell the compiler "this variable has already been declared with `constinit` elsewhere, and it does not need runtime initialization checks."

```cpp
// header.h
extern constinit int g_shared_value;  // 告诉使用者：这是常量初始化的

// source.cpp
#include "header.h"
constinit int g_shared_value = 100;   // 实际定义
```

This is particularly useful in large projects—an `extern constinit` declaration in a header file serves as "compile-time documentation," telling users that the initialization behavior of this global variable is deterministic.

## Step 3 — Comparing the Three Keywords and Selection Strategies

Now that we understand the semantics of the three keywords, let's make a clear comparison.

| Feature | `constexpr` | `consteval` | `constinit` |
|---------|-------------|-------------|-------------|
| Applicable targets | Variables, functions | Functions, constructors | Static/thread storage duration variables |
| Compile-time guarantee | "Can" be evaluated at compile time | "Must" be evaluated at compile time | Initialization must be constant initialization |
| Runtime behavior | Can degrade to a runtime call | Runtime calls not allowed | Variable can be modified at runtime |
| Mutability | Immutable (implicit `const`) | N/A | Mutable |
| Problem solved | Flexibility of compile-time computation | Forcing compile-time evaluation | Avoiding SIOF |

To summarize the selection strategy in one sentence: if the value never changes, use a `constexpr` variable; if a function must execute at compile time, use `consteval`; if a global variable needs to be initialized at compile time but modified at runtime, use `constinit`. For functions, default to `constexpr` (it is the most flexible), and only upgrade to `consteval` when you truly need to force compile-time evaluation.

### Common Combination Patterns

In real-world projects, these three keywords are often used in combination.

Pattern one is using a `consteval` function to generate a `constexpr` value. The call result of a `consteval` function is naturally a constant expression, so it can be received by a `constexpr` variable.

```cpp
consteval std::uint32_t hash_string(const char* s)
{
    std::uint32_t h = 0x811c9dc5u;
    while (*s) {
        h ^= static_cast<std::uint8_t>(*s++);
        h *= 0x01000193u;
    }
    return h;
}

constexpr auto kHashStart = hash_string("START");  // 编译期强制求值
constexpr auto kHashStop  = hash_string("STOP");
```

Pattern two is a `constexpr` function paired with `constinit` global state. The function itself does not force compile-time evaluation, but when it is used to initialize a `constinit` variable, the compiler forces it to execute at compile time.

```cpp
constexpr int lookup_value(int index)
{
    constexpr int kTable[] = {10, 20, 30, 40, 50};
    return index >= 0 && index < 5 ? kTable[index] : 0;
}

constinit int g_first = lookup_value(0);   // 编译期求值
constinit int g_third = lookup_value(2);   // 编译期求值
```

Pattern three is using `consteval` for compile-time validation. Using `consteval` on the validation logic ensures it executes at compile time, paired with `throw` to produce a compilation error.

```cpp
consteval bool check_config(int baud_rate, int data_bits)
{
    if (baud_rate <= 0 || baud_rate > 4000000) return false;
    if (data_bits < 5 || data_bits > 9) return false;
    return true;
}

// 用 static_assert + consteval 函数做编译期配置校验
static_assert(check_config(115200, 8), "Invalid UART config");
// static_assert(check_config(0, 8));  // 编译错误：校验不通过
```

## Common Pitfalls

### Function Pointers to consteval Functions Cannot Be Used at Runtime

You cannot obtain a function pointer to a `consteval` function at runtime and call it. The address of a `consteval` function can be used at compile time (such as passing it in a `consteval` context), but it cannot "escape" to runtime. If you try to obtain the address of a `consteval` function in a non-constant evaluation context, it will cause a compilation error. This is because `consteval` functions have no runtime entity—they are completely expanded and inlined at compile time.

### constinit Does Not Mean const

This point is easy to confuse. `constinit` only means that the initialization is constant initialization; the object itself is not necessarily `const`. If you need a global variable that is both initialized at compile time and immutable, you should use `constexpr` (rather than `constinit const`, although the latter would also work).

### Interaction Between consteval and Templates

`consteval` can be used with function templates, but note that if a template instantiation cannot satisfy the requirements of `consteval` (for example, if it internally calls a non-`constexpr` function), the compiler will report an error. This is different from a `constexpr` function template—a `constexpr` template only needs at least one set of arguments that can work at compile time, whereas `consteval` requires all calls to complete at compile time.

## Run Online

Run the consteval and constinit examples online to observe C++20 compile-time guarantees:

<OnlineCompilerDemo
  title="consteval and constinit: C++20 Compile-Time Guarantees"
  source-path="code/examples/vol2/06_consteval_constinit.cpp"
  description="Run online and observe consteval forced compile-time hashing and constinit mutable global variables."
  allow-run
/>

## Summary

C++20's `consteval` and `constinit` are precise supplements to the `constexpr` system. `consteval` fills the gap of "I want to force compile-time evaluation," while `constinit` solves C++'s long-standing static initialization order problem. The three each have their own roles: `constexpr` provides flexibility, `consteval` provides enforcement, and `constinit` provides initialization safety. Understanding their precise differences and making reasonable choices is key to writing high-quality compile-time computation code.

In the next chapter, we will move into practice, comprehensively applying this knowledge to implement compile-time lookup tables, string processing, and state machine design.

## References

- [cppreference: consteval specifier (C++20)](https://en.cppreference.com/w/cpp/language/consteval)
- [cppreference: constinit specifier (C++20)](https://en.cppreference.com/w/cpp/language/constinit)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [C++ Stories: const vs constexpr vs consteval vs constinit in C++20](https://www.cppstories.com/2022/const-options-cpp20/)
