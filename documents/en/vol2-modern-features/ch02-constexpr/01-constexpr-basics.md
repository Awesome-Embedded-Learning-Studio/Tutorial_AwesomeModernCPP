---
chapter: 2
cpp_standard:
- 11
- 14
- 17
description: Master the core mechanisms of compile-time computation and the evolution
  of the standard, from `constexpr` variables to `constexpr` functions.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 0: 移动构造与移动赋值'
reading_time_minutes: 17
related:
- constexpr 构造函数与字面类型
- 编译期计算实战
tags:
- host
- cpp-modern
- intermediate
- constexpr
- 编译期计算
title: 'constexpr Fundamentals: The Art of Compile-Time Evaluation'
translation:
  source: documents/vol2-modern-features/ch02-constexpr/01-constexpr-basics.md
  source_hash: 008babb96171ec695231edcf2d4465b79d423d80955d701cb46db55d7ad1f6a4
  translated_at: '2026-06-16T03:56:45.574624+00:00'
  engine: anthropic
  token_count: 3130
---
# constexpr Basics: The Art of Compile-Time Evaluation

## Introduction

Let's keep it simple! The core problem `constexpr` solves isn't "is it fast?", but "do we even need to calculate it?". When you write `constexpr` in your code, you are telling the compiler: this value is determined at compile time, so just write it directly into the binary file. It doesn't cost a single instruction at runtime. This is more thorough than any runtime optimization.

To verify this, let's look at the assembly output of a test code snippet (GCC 15.2.1, -O2 optimization):

```cpp
int get_value() {
    constexpr int x = 16;
    return x * x;
}
```

Compiled assembly code (verified):

```asm
get_value():
        mov     eax, 256
        ret
```

We can see that the function directly returns the immediate value 256, without any memory access or calculation. This is direct evidence that "the compiler calculates it for you and writes an immediate value."

In this chapter, we start from scratch to understand the ins and outs of `constexpr`: what it is, what it isn't, what restrictions have been relaxed in various C++ standard versions, and how to use it to write safer and faster code.

## Step 1 — Understanding `constexpr` Variables

### Compile-Time Constants vs `const`

Many people confuse `constexpr` and `const`. This is a misconception that needs to be corrected early. The semantics of `const` are "this variable cannot be modified after initialization," but its initial value can be calculated entirely at runtime. The semantics of `constexpr` are stronger: it requires that the variable's initial value must be determinable at compile time.

```cpp
void runtime_example() {
    int user_input;
    std::cin >> user_input;
    const int c = user_input;  // OK: Read-only, value determined at runtime
    // constexpr int ce = user_input; // ERROR: Value not known at compile time
}
```

`c` is a `const` variable. The compiler won't let you modify it, but its value is determined at runtime. This means you cannot use it to declare array sizes (C-style arrays in C++ require compile-time constants as lengths), nor can you use it as a non-type template parameter. `constexpr` variables don't have these restrictions—because they have a determined value at compile time.

Here is a common pitfall: The C++ standard specifies that if a `const` integral variable is initialized with a constant expression, it is itself a constant expression. This means that at global/namespace scope, a declaration like `const int max_size = 100;` is actually usable for array sizes and non-type template parameters. This contradicts the intuition many people have that "`const` cannot be used in compile-time contexts." However, the advantage of `constexpr` is that it clearly expresses your intent, applies to all literal types (not just integral types), and strictly requires the initializer to be a constant expression.

Here is another pitfall: At global or namespace scope, `const` integral variables in C++ have internal linkage by default (just like `static`), and `constexpr` variables also have internal linkage. However, if your `const` variable happens to be initialized with a value that can be calculated at compile time, the compiler might treat it as a constant expression—this is a compiler extension behavior, not guaranteed by the standard. So if you need a compile-time constant, explicitly write `constexpr`; don't rely on the compiler to decide for you.

### Requirements for `constexpr` Variables

To declare a variable as `constexpr`, the following conditions must be met: it must be a literal type, it must be initialized immediately, and the initializing expression must be a constant expression. We will expand on the concept of literal types in the next chapter; for now, just know that scalar types (`int`, `float`, pointers, etc.), reference types, and class types with `constexpr` constructors all count as literal types.

## Step 2 — `constexpr` Functions: The Double Agent

`constexpr` functions are the most interesting part of `constexpr`. We call them "double agents" because they can work in two scenarios: when their arguments are all compile-time constants and the context requires compile-time evaluation, they execute at compile time; otherwise, they execute at runtime just like ordinary functions.

### Basic Form

```cpp
constexpr int square(int x) {
    return x * x;
}

int main() {
    constexpr int compile_time_result = square(10); // Evaluated at compile time
    int y = 20;
    int runtime_result = square(y);                 // Evaluated at runtime
}
```

You see, the same function, two different fates. This is actually the essence of `constexpr` function design: you write one piece of code, and the compiler decides when to execute it based on context. This "context-adaptive" characteristic makes `constexpr` functions much more flexible than pure compile-time tools (like template metaprogramming).

### The Golden Duo: `static_assert` and `constexpr`

`static_assert` is a compile-time assertion, and its first parameter must be a constant expression. This naturally pairs with `constexpr` functions—you can use `static_assert` to verify the behavior of `constexpr` functions at compile time.

```cpp
constexpr int factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);
}

static_assert(factorial(5) == 120, "Factorial of 5 should be 120");
```

If you write a bug in the implementation of `factorial` (e.g., mistakenly writing `n + 1` instead of `n - 1`), `static_assert` will crash immediately at compile time, telling you exactly where the problem is. This ability to "catch errors at compile time" is extremely valuable in large projects. Moreover, this kind of testing is zero-cost—they don't generate any runtime code.

## Step 3 — Evolution of the Standard: From Constraints to Freedom

The capabilities of `constexpr` vary significantly across different C++ standards. Understanding these differences is crucial for writing portable and correct `constexpr` code.

### C++11: Extremely Strict Restrictions

C++11 introduced `constexpr`, but with extremely strict limitations. The body of a `constexpr` function could contain only a single `return` statement (plus `using`, `typedef` declarations, etc., that don't generate code). This meant you couldn't write loops, declare local variables, or write `if` statements—all logic had to be compressed into a single ternary operator expression or recursive calls.

```cpp
// C++11 style: Recursive implementation
constexpr int factorial_cxx11(int n) {
    return n <= 1 ? 1 : n * factorial_cxx11(n - 1);
}
```

This code looks concise, but there is a hidden problem: recursion depth. Compilers have a default limit on the recursion depth of `constexpr` evaluation, the specific value depends on the compiler implementation. Based on testing, GCC 15.2.1 has a recursion depth limit of about 520-600 layers; exceeding this limit triggers a compilation error. If you calculate a value of the scale `factorial(50)`, although the recursively expanded call tree is large, the call depth is relatively shallow (only 50 layers), so it usually won't trigger the limit. But if you write a linear recursion by hand (e.g., subtracting 1 and recursing to 0), when the parameter is large, it will exceed the limit.

To verify this, we wrote a test program (see `ch05/factorial_limit.cpp`), with actual results as follows:

```text
[...]
```

This shows that the 512/1024 mentioned in articles is a conservative estimate; the actual situation varies by compiler and version. If you need to handle deeper recursion, consider switching to an iterative version (supported starting from C++14), or use compiler options to adjust the limit (like GCC's `-fconstexpr-depth`).

### C++14: Significantly Relaxed

C++14 was the turning point where `constexpr` became truly practical. Local variables, `if` statements, and `for`/`while` loops can now be used in the function body. The only things still not allowed are `goto`, assembly statements, and local variables of non-literal types.

```cpp
// C++14 style: Iterative implementation
constexpr int factorial_cxx14(int n) {
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}
```

Finally, we don't have to cram all logic into recursion. For embedded developers, this means you can implement logic like CRC calculations and lookup table generation in a more natural way, instead of racking your brains to use template metaprogramming or recursion to bypass restrictions.

Another important change is that `constexpr` member functions are no longer implicitly `const`. In C++11, `constexpr` member functions were implicitly marked with the `const` qualifier, meaning they could not modify any member variables. C++14 removed this restriction, allowing `constexpr` member functions to modify members (in a compile-time context), making the behavior of compile-time objects more flexible.

### C++17: More Practical Features

C++17 further expanded the capabilities of `constexpr`. `constexpr` lambda expressions are officially supported (GCC/Clang had extension support before), and `if constexpr` became standard. Furthermore, more and more functions in the standard library are marked as `constexpr`: `std::pair`, `std::array` operations, `std::chrono` utilities, etc.

```cpp
// C++17: constexpr lambda and if constexpr
constexpr auto get_square_lambda() {
    return [](int n) { return n * n; };
}

constexpr int check_value(int n) {
    if constexpr (sizeof(int) == 4) {
        return n * 2;
    } else {
        return n;
    }
}
```

Let's summarize the key differences of the three standards with a table:

| Capability | C++11 | C++14 | C++17 |
|------------|-------|-------|-------|
| Local Variables | `static` only | Allowed | Allowed |
| Loops (`for`/`while`) | Forbidden | Allowed | Allowed |
| `if` Statement | Forbidden (ternary only) | Allowed | Allowed |
| Member Func Modify Members | Forbidden (implicit `const`) | Allowed | Allowed |
| Lambda | Not Supported | Partial Support | Official Support |
| Standard Library `constexpr` | Very Few | Increased | Significantly Increased |

## Step 4 — `constexpr` vs Templates: When to Use Which

Both `constexpr` and template metaprogramming can achieve compile-time calculation, but their positioning is vastly different. Template metaprogramming is Turing complete and can theoretically perform any calculation at compile time; but it is painful to write, even more painful to read, and the compilation error messages are like gibberish. `constexpr` is a "good enough" solution—it covers the vast majority of compile-time calculation needs and reads almost exactly like a normal function.

```cpp
// Template metaprogramming version (C++11)
template<int N>
struct Factorial {
    static const int value = N * Factorial<N - 1>::value;
};

template<>
struct Factorial<0> {
    static const int value = 1;
};

// constexpr version (C++14)
constexpr int factorial(int n) { /* ... */ }
```

From the author's experience, the principle is simple: if you can solve it with a `constexpr` function, don't resort to template metaprogramming. Template metaprogramming is suitable for scenarios that require calculation at the type level (e.g., selecting different implementation strategies based on type), while `constexpr` is suitable for compile-time calculation at the value level. The two are often used together—templates handle type-level dispatch, and `constexpr` functions handle specific value calculations.

## Step 5 — Practical Examples

### Compile-Time Fibonacci and Factorial

We have already shown these two classic examples earlier. Now let's do something more practical—use a `constexpr` function to generate a compile-time lookup table.

### Compile-Time CRC-32 Lookup Table

CRC checksums are ubiquitous in communication protocols and storage systems. The traditional approach is to generate a CRC lookup table at runtime with a loop, or use a tool like Python to generate the table and `#include` it. With `constexpr`, we can let the compiler generate this table for us.

```cpp
constexpr uint32_t crc32_table(uint8_t idx) {
    uint32_t crc = idx;
    for (int i = 0; i < 8; ++i) {
        if (crc & 1)
            crc = (crc >> 1) ^ 0xEDB88320;
        else
            crc >>= 1;
    }
    return crc;
}

// Generate the full table at compile time
constexpr std::array<uint32_t, 256> crc32_lut = [] {
    std::array<uint32_t, 256> table{};
    for (int i = 0; i < 256; ++i) {
        table[i] = crc32_table(i);
    }
    return table;
}();
```

`crc32_lut` is fully generated at compile time and is written directly into the read-only data section (`.rodata`) of the target file. No initialization code is needed at runtime; it can be used directly. The elegance of this pattern lies in: the table generation logic and table usage logic are in the same source file, requiring no extra code generation tools or build steps.

### Compile-Time vs Runtime Performance Comparison

To intuitively feel the power of `constexpr`, let's look at a simple comparison experiment.

```cpp
// Runtime version
uint32_t runtime_crc32(uint32_t crc, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        crc = (crc >> 8) ^ crc32_lut_runtime[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}

// Compile-time version (uses constexpr table)
constexpr uint32_t compiletime_crc32(uint32_t crc, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        crc = (crc >> 8) ^ crc32_lut[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}
```

Results are roughly as follows (specific values depend on hardware and compiler optimization):

```text
Runtime CRC32:  2.85 us
Compile-time CRC32: 0.35 us
```

**Note**: This benchmark has certain limitations. Modern compilers are very smart; even if you declare a runtime version, if the compiler finds that the function's input is a constant and has no side effects, it might automatically promote it to compile-time calculation during the optimization phase (this optimization is called "constant propagation"). Therefore, to accurately measure the advantage of `constexpr`, you need to ensure the compiler doesn't perform this optimization on the runtime version. In actual projects, the true value of `constexpr` isn't in saving these 2.5 microseconds, but in:

1. Forcing compile-time calculation, not relying on the compiler's "mood".
2. Being usable in contexts requiring constant expressions (like array sizes, template parameters).
3. Discovering logic errors at compile time (via `static_assert`).

However, for embedded systems, faster startup time is indeed a practical advantage—the `constexpr` version of the table is stored directly in the read-only data section, requiring no initialization code.

### Compile-Time Math Lookup Tables

Another common scenario is trigonometric function lookup tables. In signal processing and motor control, we often need to quickly get `sin`/`cos` values. Directly calling `std::sin` on embedded systems might be too slow (especially on MCUs without an FPU), and lookup tables are a classic optimization method.

```cpp
constexpr float deg_to_rad(float deg) {
    return deg * 3.14159265f / 180.0f;
}

// Taylor series approximation for sin
constexpr float sin_approx(float x) {
    // ... implementation details ...
    return result;
}

constexpr std::array<float, 360> sin_lut = [] {
    std::array<float, 360> table{};
    for (int i = 0; i < 360; ++i) {
        table[i] = sin_approx(deg_to_rad(i));
    }
    return table;
}();
```

Here is a detail worth noting: The C++ standard does not guarantee `std::sin` is a `constexpr` function. It wasn't until C++26 that a proposal was made to make it officially `constexpr`. So in C++17 and earlier, you need to implement compile-time trigonometric calculations yourself using Taylor expansion or other approximation methods. However, this doesn't affect the final result—the compiled lookup data is precise.

## Common Pitfalls and Gotchas

### `constexpr` is Not "Force Compile-Time Evaluation"

This is the easiest mistake to make. A `constexpr` function is only "allowed" to be evaluated at compile time, not "required" to. If you assign the return value of a `constexpr` function to a normal variable (not a `constexpr` variable), the compiler is perfectly free to call it at runtime. If you really need to force compile-time evaluation, use a `constexpr` variable to receive the return value, or use `std::is_constant_evaluated` in C++20 (we will cover this in detail in later chapters).

### Compiler Recursion Depth Limits

Even with the iterative version of C++14, `constexpr` functions can still trigger the compiler's evaluation step limit. Different compilers have different default limits: GCC 15.2.1 has a default recursion depth limit of about 520-600 layers (tested), Clang defaults to 512 layers (documented value), and MSVC has similar limits. Besides recursion depth, compilers also have a total step limit (GCC defaults to about 33M steps). If you do a lot of calculation at compile time (e.g., generating a very large lookup table), you might trigger the compiler's internal limits, manifesting as compilation failure.

When encountering this, you can increase the limit via compiler options (like GCC's `-fconstexpr-depth` and `-fconstexpr-loop-limit`), or consider splitting the generation of large tables into smaller segments. However, in actual projects, if your `constexpr` calculation is complex enough to trigger these limits, you should usually reconsider the design—although compile-time calculation is zero-cost, it significantly increases compilation time.

### Undefined Behavior in `constexpr` Functions

When a `constexpr` function is evaluated at compile time, if it triggers undefined behavior (UB), the compiler will report an error directly—this is actually a good thing. Things like array out-of-bounds, signed integer overflow, or division by zero might quietly produce wrong results at runtime, but they will be intercepted by the compiler during `constexpr` evaluation.

```cpp
constexpr int bad_func(int n) {
    int arr[10] = {};
    return arr[n]; // If n >= 10, compilation error
}

constexpr int test = bad_func(20); // Compile error: array index out of bounds
```

This feature makes `constexpr` a kind of "safety net"—for things you can calculate at compile time, the compiler helps you check their legality.

## Run Online

Run the `constexpr` basic examples online to observe the difference between compile-time evaluation and runtime evaluation:

<OnlineCompilerDemo
  title="constexpr Basics: Compile-Time Factorial and CRC-32 Lookup Table"
  source-path="code/examples/vol2/05_constexpr_basics.cpp"
  description="Run online and observe the compile-time and runtime behavior of constexpr functions, as well as static_assert validation."
  allow-run
  allow-x86-asm
/>

## Summary

By now, we have sorted out the basic mechanism of `constexpr`. Let's summarize a few key points:

`constexpr` variables are true compile-time constants, while `const` only guarantees "read-only". `constexpr` functions are a dual-mode function where the compiler decides whether to execute them at compile time or runtime based on context. From C++11 to C++17, the restrictions on `constexpr` have been gradually relaxed, from only allowing a single `return` statement to supporting loops, local variables, and lambdas. `static_assert` is the natural partner of `constexpr`, making compile-time testing possible. Don't use template metaprogramming if a `constexpr` function can solve the problem—the code is clearer and error messages are friendlier.

In the next chapter, we will dive into `constexpr` constructors and literal types to see how to make custom types participate in compile-time calculation.

## Reference Resources

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [C++ Feature-test macro `__cpp_constexpr`](https://en.cppreference.com/w/cpp/feature_test)
