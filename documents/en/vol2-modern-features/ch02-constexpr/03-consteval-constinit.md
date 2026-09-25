---
chapter: 2
cpp_standard:
- 20
- 23
description: C++20 immediate functions and compile-time initialization, and how to
  precisely distinguish them from constexpr and choose between them
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 2: constexpr Basics: The Art of Compile-Time Evaluation'
reading_time_minutes: 15
related:
- constexpr Constructors and Literal Types
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
  source_hash: 77655f2aeb9cf2849823f20e8c904e91538a43d38eee6910dbe12cb417682ddd
  translated_at: '2026-09-25T14:58:57+00:00'
  engine: anthropic
  token_count: 3300
---
# consteval and constinit: New Tools for Compile-Time Guarantees

Over the previous two chapters we kept discussing `constexpr`—the keyword that "may" be evaluated at compile time. That word "may" is both its strength and its weakness. When you declare a `constexpr` function, you express the intent that "this function can be evaluated at compile time," but the compiler does not guarantee that it actually will be.

Note that modern compilers (with optimizations enabled) are quite smart—even if you assign the return value to a non-`constexpr` variable, as long as the arguments are constants and the function call is simple enough, the compiler may still evaluate it at compile time. In certain complex scenarios, however, or when compiler optimizations are disabled (such as with `-O0`), a `constexpr` function can indeed degrade into a runtime call. This uncertainty is exactly the problem `consteval` set out to solve.

This "flexibility" is a good thing most of the time, but there are scenarios where you genuinely need a hard guarantee: this function must, necessarily, absolutely finish executing at compile time. Think compile-time hashing or compile-time configuration validation—if these degrade into runtime computation, you might not notice the problem during code review and only discover it during performance profiling or from a runtime error. Through mandatory compile-time checking, `consteval` exposes this class of problems at the compilation stage.

C++20 introduced two new keywords to solve this problem: functions declared `consteval` (called "immediate functions") must be evaluated at compile time, while `constinit` guarantees that static variables complete initialization at compile time. They are not replacements for `constexpr`, but fine-grained complementary tools.

## Step 1 — consteval: Enforcing Compile-Time Evaluation

### The Core Difference Between consteval and constexpr

A function declared `consteval` is called an "immediate function". Its semantics are strikingly direct: every call to such a function must produce a compile-time constant. If the compiler finds that a calling context cannot complete the evaluation at compile time, it simply reports an error.

```cpp
consteval int square(int x)
{
    return x * x;
}

// OK: the argument is a constant and the context is a constexpr variable initialization
constexpr int kResult = square(8);  // compiles, kResult == 64

// OK: the argument is a constant literal
int arr[square(5)];  // OK, square(5) == 25, array size

// Error! the argument comes from the runtime
int runtime_val = 42;
// int bad = square(runtime_val);  // compile error: not a constant expression
```

Compare with the `constexpr` version:

```cpp
constexpr int square_maybe(int x)
{
    return x * x;
}

int runtime_val = 42;
int ok = square_maybe(runtime_val);  // OK! degrades to a runtime call
```

The difference is plain at a glance: faced with runtime arguments, a `constexpr` function "compromises" and automatically degrades to runtime execution; a `consteval` function "refuses" and fails the compile outright. You can think of `consteval` as "`constexpr` with an enforced compile-time guarantee."

### Where consteval Fits Best

The scenarios where `consteval` fits best are those computations where "executing at runtime makes no sense, or even introduces risk."

The first classic scenario is compile-time ID and hash generation. Protocol handling and command dispatch often need to map strings to integer IDs. If the hash computation from string to ID runs at runtime, you both waste CPU and lose the ability to detect hash collisions at compile time.

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

// All IDs are generated at compile time, with zero runtime overhead
constexpr auto kIdStart = command_id("START");
constexpr auto kIdStop  = command_id("STOP");
constexpr auto kIdReset = command_id("RESET");

// Compile-time verification: make sure there are no hash collisions
static_assert(kIdStart != kIdStop);
static_assert(kIdStart != kIdReset);
static_assert(kIdStop != kIdReset);
```

The second classic scenario is compile-time configuration validation and constraint checking. When you need to ensure a configuration value satisfies specific constraints, `consteval` forces the validation to complete at compile time, ruling out any chance of discovering a bad configuration only at runtime.

```cpp
consteval int validate_buffer_size(int size)
{
    // If the constraint is not satisfied, it is a compile error right away
    return size > 0 && size <= 4096 && (size & (size - 1)) == 0
        ? size
        : throw "Buffer size must be a power of 2 between 1 and 4096";
    // In a consteval context, throw causes a compile error
}

constexpr int kBufferSize = validate_buffer_size(1024);  // OK
// constexpr int kBadSize = validate_buffer_size(1000);  // compile error! not a power of 2
```

The third scenario is compile-time type tags and metadata. When you need to embed compile-time information into the type system (peripheral descriptions or protocol field definitions, for example), `consteval` ensures this metadata can never accidentally become a runtime object.

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

### consteval Propagation Rules

`consteval` has a propagation behavior that deserves special attention: if a `consteval` function is called inside another function, that outer function must itself be `consteval` (or the call itself must sit inside a constant evaluation context).

```cpp
consteval int forced_compile_time(int x) { return x * x; }

// Error! Calling a consteval function inside a constexpr function,
// but the result of that call is not a constant expression
constexpr int wrapper(int x)
{
    // return forced_compile_time(x);  // compile error
    return x * x;  // you have to reimplement the logic yourself
}

// OK: a consteval function may call another consteval function
consteval int double_square(int x)
{
    return forced_compile_time(x) * 2;
}

constexpr auto kVal = double_square(3);  // OK, kVal == 18
```

C++23 (as a defect report against C++20, P2564R3) further adjusted the propagation rules: if a `consteval` function is called inside a `constexpr` function, it is no longer an error as long as the call of that `constexpr` function ultimately ends up in a constant evaluation context. This makes combining `consteval` with `constexpr` much more flexible.

### if consteval: Compile-Time/Runtime Dispatch

C++23 introduced `if consteval` (also known as `if !consteval`), allowing a function to choose different code paths depending on whether it is currently in a constant evaluation context.

```cpp
#include <cstdio>
#include <cstddef>

constexpr std::size_t compute_hash(const char* str, std::size_t len)
{
    if consteval {
        // Compile-time path: use a pure constexpr algorithm
        std::size_t hash = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < len; ++i) {
            hash ^= static_cast<std::size_t>(str[i]);
            hash *= 0x100000001b3ull;
        }
        return hash;
    } else {
        // Runtime path: other implementation strategies are possible
        std::size_t hash = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < len; ++i) {
            hash ^= static_cast<std::size_t>(str[i]);
            hash *= 0x100000001b3ull;
        }
        // On the runtime path, if the compiler supports inline SIMD instructions,
        // it may auto-vectorize this loop; you could also call a SIMD library explicitly
        return hash;
    }
}

constexpr auto kCompileTimeHash = compute_hash("test", 4);  // takes the compile-time path
```

`if consteval` and `if constexpr` are different things. `if constexpr` selects a branch at compile time based on template arguments, while `if consteval` selects based on whether evaluation is currently happening in a constant evaluation context. The latter is better suited to providing different implementation strategies for compile time and runtime within the same function.

## Step 2 — constinit: Solving the Static Initialization Problem

### The Static Initialization Order Fiasco

Before discussing `constinit`, we need to understand the problem it solves. In C++, objects with static storage duration (global variables, `static` class member variables, and so on) are initialized in two phases:

The first phase is static initialization, which covers zero-initialization and constant initialization. These happen during program loading, even before the `main` function starts, and their order is well-defined—zero-initialization comes before constant initialization.

The second phase is dynamic initialization, which requires running code to participate. The problem is that the order of dynamic initialization across translation units is undefined. If you have two files, `a.cpp` and `b.cpp`, each with a global object, and the initialization of the object in `a.cpp` depends on the value of the object in `b.cpp`, you may run into the "Static Initialization Order Fiasco" (SIOF for short).

```cpp
// a.cpp
#include <vector>
std::vector<int> g_data{1, 2, 3};  // dynamic initialization: calls vector's constructor

// b.cpp
extern std::vector<int> g_data;
int g_first_element = g_data[0];  // may read an uninitialized g_data!
```

What makes this bug nasty is that it "depends on luck"—it works under some link orders and blows up under others, and it only strikes at program startup, which makes it extremely hard to debug.

### The Semantics of constinit

The semantics of `constinit` are simple and forceful: applied to a variable declaration with static or thread storage duration, it asserts that the variable must receive constant initialization. If the compiler finds that this variable needs dynamic initialization, it fails the compile outright.

```cpp
#include <array>

// OK: aggregate initialization of std::array is constant initialization
constinit std::array<int, 4> g_table = {1, 2, 3, 4};

// OK: initialized with the return value of a constexpr function
constexpr int compute_value() { return 42; }
constinit int g_value = compute_value();

// Error! get_runtime_value is not a constant expression and needs dynamic initialization
// int get_runtime_value();
// constinit int g_bad = get_runtime_value();  // compile error
```

### constinit vs constexpr: A Subtle but Critical Difference

`constinit` and `constexpr` both involve the compile time, but along different dimensions. A `constexpr` variable requires the value to be determined at compile time and the object itself to be `const`—you cannot modify it. A `constinit` variable also requires the initial value to be determined at compile time, but the object itself can be modified.

```cpp
constexpr int kConstVal = 42;        // compile-time value + not modifiable
// kConstVal = 100;                  // error! a constexpr variable is const

constinit int gMutableVal = 42;      // compile-time initialization + modifiable
gMutableVal = 100;                   // OK! the value can be changed at runtime
```

This difference looks small, but it is remarkably useful in real projects. Take a global configuration buffer: you want its initial contents settled at compile time (avoiding SIOF), yet the program needs to update its contents while running. `constinit` fits this need exactly.

Worth noting: `constinit` cannot be used together with `constexpr`—they are mutually exclusive. A `constexpr` variable already implies the constant-initialization guarantee (along with `const` semantics), so adding `constinit` on top would be redundant.

### constinit and thread_local

`constinit` has a very practical side effect: applied to a `thread_local` variable, it can eliminate the runtime overhead of thread-safety checks.

```cpp
// Without constinit: every access has to check whether the thread-local storage is initialized
thread_local int tl_counter = 42;

// With constinit: the compiler knows initialization completed at load time,
// so no runtime guard variable is needed
constinit thread_local int tl_fast_counter = 42;
```

An ordinary `thread_local` variable has to check on first access whether it has been initialized, which typically involves a hidden guard variable and possibly atomic operations. With `constinit`, the compiler knows the variable already holds a definite initial value when the program loads, so in principle it can optimize the runtime check away. The actual performance gain depends on the specific compiler implementation—in a test on GCC 15.2 (`-O2`), the improvement was limited (about 5%), but other compilers or scenarios may see a more noticeable benefit.

### constinit in extern Declarations

`constinit` can be used in non-initializing declarations (an `extern` declaration, for example) to tell the compiler "this variable is already declared `constinit` elsewhere, so it needs no runtime initialization check."

```cpp
// header.h
extern constinit int g_shared_value;  // tells users: this is constant-initialized

// source.cpp
#include "header.h"
constinit int g_shared_value = 100;   // the actual definition
```

This is especially useful in large projects—an `extern constinit` declaration in a header file is a form of "compile-time documentation", telling users that this global variable's initialization behavior is deterministic.

## Step 3 — Comparing the Three Keywords and Choosing Between Them

With the semantics of the three keywords understood, let's now lay out a clear comparison.

This comparison has been turned into an animation—you can play it, pause it, or use the step buttons to single-step through and see clearly what each of the three keywords is responsible for:

<Anim id="consteval-constinit" />

| Feature | `constexpr` | `consteval` | `constinit` |
|------|-------------|-------------|-------------|
| Applies to | variables, functions | functions, constructors | static/thread storage duration variables |
| Compile-time guarantee | "can" be evaluated at compile time | "must" be evaluated at compile time | initialization must be constant initialization |
| Runtime behavior | may degrade to a runtime call | runtime calls not allowed | variable can be modified at runtime |
| Mutability | not modifiable (implicitly `const`) | N/A | modifiable |
| Problem solved | flexibility of compile-time computation | enforced compile-time evaluation | avoiding SIOF |

The selection strategy in one sentence: if the value never changes, use a `constexpr` variable; if the function must execute at compile time, use `consteval`; if a global variable needs compile-time initialization but runtime modifiability, use `constinit`. For functions, default to `constexpr` (it is the most flexible), and only escalate to `consteval` when you truly need to force compile-time evaluation.

### Common Combination Patterns

In real projects, these three keywords are frequently combined.

Pattern one is a `consteval` function producing `constexpr` values. The result of calling a `consteval` function is a constant expression by nature, so a `constexpr` variable can receive it directly.

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

constexpr auto kHashStart = hash_string("START");  // compile-time enforced evaluation
constexpr auto kHashStop  = hash_string("STOP");
```

Pattern two is a `constexpr` function paired with `constinit` global state. The function itself does not force compile-time evaluation, but when it is used to initialize a `constinit` variable, the compiler forces it to execute at compile time.

```cpp
constexpr int lookup_value(int index)
{
    constexpr int kTable[] = {10, 20, 30, 40, 50};
    return index >= 0 && index < 5 ? kTable[index] : 0;
}

constinit int g_first = lookup_value(0);   // evaluated at compile time
constinit int g_third = lookup_value(2);   // evaluated at compile time
```

Pattern three is `consteval` for compile-time validation. Marking the validation logic `consteval` ensures it executes at compile time, and pairing it with `throw` produces the compile error.

```cpp
consteval bool check_config(int baud_rate, int data_bits)
{
    if (baud_rate <= 0 || baud_rate > 4000000) return false;
    if (data_bits < 5 || data_bits > 9) return false;
    return true;
}

// Compile-time configuration validation with static_assert + a consteval function
static_assert(check_config(115200, 8), "Invalid UART config");
// static_assert(check_config(0, 8));  // compile error: validation fails
```

## Common Pitfalls

### The Address of a consteval Function Cannot Be Used at Runtime

You cannot take a function pointer to a `consteval` function at runtime and call through it. The address of a `consteval` function can be used at compile time (passed around within a `consteval` context, for example), but it cannot "escape" to runtime. Attempting to take the address of a `consteval` function in a non-constant-evaluation context causes a compile error. This is because `consteval` functions have no runtime entity—they are fully expanded and inlined at compile time.

### constinit Does Not Mean const

This one is easy to mix up. `constinit` only says that the initialization is constant initialization; the object itself is not necessarily `const`. If you need a global variable that is both initialized at compile time and unmodifiable, you should use `constexpr` (rather than `constinit const`, even though the latter also works).

### How consteval Interacts with Templates

`consteval` can be used with function templates, but note: if an instantiation of the template cannot satisfy `consteval`'s requirements (for example, it internally calls a non-`constexpr` function), the compiler reports an error. This differs from `constexpr` function templates—a `constexpr` template only needs at least one set of arguments to work at compile time, whereas `consteval` requires all calls to complete at compile time.

## Run Online

Run the consteval and constinit examples online and observe the compile-time guarantees of C++20:

<OnlineCompilerDemo
  title="consteval and constinit: C++20 Compile-Time Guarantees"
  source-path="code/examples/vol2/06_consteval_constinit.cpp"
  description="Run online and observe consteval enforcing compile-time hashing and constinit mutable global variables."
  allow-run
/>

## References

- [cppreference: consteval specifier (C++20)](https://en.cppreference.com/w/cpp/language/consteval)
- [cppreference: constinit specifier (C++20)](https://en.cppreference.com/w/cpp/language/constinit)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [C++ Stories: const vs constexpr vs consteval vs constinit in C++20](https://www.cppstories.com/2022/const-options-cpp20/)
