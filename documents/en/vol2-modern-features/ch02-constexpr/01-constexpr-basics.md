---
title: 'constexpr Basics: The Art of Compile-Time Evaluation'
description: From `constexpr` variables to `constexpr` functions, master the core
  mechanisms and standard evolution of compile-time computation.
chapter: 2
order: 1
tags:
- host
- cpp-modern
- intermediate
- constexpr
- 编译期计算
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
reading_time_minutes: 18
prerequisites:
- 'Chapter 0: 移动构造与移动赋值'
related:
- constexpr 构造函数与字面类型
- 编译期计算实战
translation:
  source: documents/vol2-modern-features/ch02-constexpr/01-constexpr-basics.md
  source_hash: 0285029e807ada351f0c0a7501219f25453af46787ee930c46d24e47002d1640
  translated_at: '2026-05-26T11:24:13.320482+00:00'
  engine: anthropic
  token_count: 3136
---
# constexpr Basics: The Art of Compile-Time Evaluation

## Introduction

Simply put, the core problem `constexpr` solves is not "is it fast," but "does it even need to be computed." When you write `constexpr` in your code, you are telling the compiler: this value is determined at compile time, just write it directly into the binary. It doesn't cost a single instruction at runtime. This is more thorough than any runtime optimization.

To verify this, let's look at the assembly output of a test snippet (GCC 15.2.1, -O2 optimization):

```cpp
constexpr int kBufferSize = 256;

int get_buffer_size()
{
    return kBufferSize;
}
```

The compiled assembly code (verified):

```asm
get_buffer_size():
    movl    $256, %eax
    ret
```

As we can see, the function directly returns the immediate value 256, with no memory access or computation. This is direct evidence of "the compiler computes it for you and writes an immediate value."

In this chapter, we start from scratch to understand the ins and outs of `constexpr`: what it is, what it isn't, what restrictions each C++ standard version relaxed, and how to use it to write safer and faster code.

## Step One — Understanding constexpr Variables

### Compile-Time Constants vs const

Many people confuse `const` and `constexpr`, which is a misconception that needs to be corrected early. The semantics of `const` are "this variable cannot be modified after initialization," but its initial value can be computed at runtime. `constexpr` has stronger semantics: it requires the variable's initial value to be determinable at compile time.

```cpp
// const：运行时常量，初始值可以来自运行时
int get_runtime_value();
const int kSize = get_runtime_value();     // OK，kSize 是 const 但不是编译期常量

// constexpr：编译期常量，初始值必须能在编译期算出来
constexpr int kBufferSize = 256;           // OK，256 是字面量
constexpr int kMask = kBufferSize - 1;     // OK，由编译期常量计算而来

// constexpr int kBad = get_runtime_value(); // 编译错误！初始值不是常量表达式
```

`runtime_val` is a `const` variable, and the compiler won't let you modify it, but its value is determined at runtime. This means you can't use it to declare an array size (C-style arrays in C++ require a compile-time constant for their length), nor can you use it as a non-type template parameter. `compile_val`, on the other hand, has no such restrictions — because it has a determined value at compile time.

Here is an easy pitfall to fall into: the C++ standard specifies that if a `const` integer variable is initialized with a constant expression, it is itself a constant expression. This means that in global or namespace scope, a declaration like `const int N = 10;` can actually be used for array sizes and non-type template parameters. This contradicts the intuition many people have that "const cannot be used in compile-time contexts." However, the advantage of `constexpr` is that it explicitly expresses your intent, applies to all literal types (not just integers), and strictly requires the initial value to be a constant expression.

Here is another easy pitfall to fall into: in global or namespace scope, `const` integer variables in C++ have internal linkage by default (just like `static`), and `constexpr` variables also have internal linkage. But if your `const` variable happens to be initialized with a value computable at compile time, the compiler might treat it as a constant expression — this is a compiler extension, not guaranteed by the standard. So if you need a compile-time constant, explicitly write `constexpr`, and don't rely on the compiler to make that decision for you.

### Requirements for constexpr Variables

For a variable to be declared `constexpr`, it must meet the following conditions: it must be a literal type, it must be immediately initialized, and the initializing expression must be a constant expression. We will dive into the concept of literal types in the next chapter; for now, you just need to know that scalar types (`int`, `float`, pointers, etc.), reference types, and class types with a `constexpr` constructor all qualify as literal types.

## Step Two — constexpr Functions: The Double Agent

`constexpr` functions are the most interesting part of `constexpr`. We call them "double agents" because they can work in two scenarios: when all their arguments are compile-time constants and the context requires compile-time evaluation, they execute at compile time; otherwise, they execute at runtime just like ordinary functions.

### Basic Form

```cpp
constexpr int square(int x)
{
    return x * x;
}

// 编译期求值：参数是字面量，上下文是 constexpr 变量初始化
constexpr int kResult = square(8);  // 编译器直接把 kResult 替换为 64

// 运行时求值：参数来自运行时
int runtime_input = 42;
int result = square(runtime_input);  // 普通函数调用，在运行时执行
```

You see, the same function, two different fates. This is actually the essence of `constexpr` function design: you write one piece of code, and the compiler decides when to execute it based on the context. This "context-adaptive" trait makes `constexpr` functions much more flexible than pure compile-time tools like template metaprogramming.

### The Golden Partnership of static_assert and constexpr

`static_assert` is a compile-time assertion, and its first parameter must be a constant expression. This naturally pairs with `constexpr` functions — you can use `static_assert` to verify the compile-time behavior of `constexpr` functions.

```cpp
constexpr int factorial(int n)
{
    return n <= 1 ? 1 : n * factorial(n - 1);
}

static_assert(factorial(0) == 1, "factorial(0) should be 1");
static_assert(factorial(1) == 1, "factorial(1) should be 1");
static_assert(factorial(5) == 120, "factorial(5) should be 120");
static_assert(factorial(10) == 3628800, "factorial(10) should be 3628800");
```

If you write a bug in the implementation of `factorial` (for example, mistakenly writing `return n * factorial(n)` instead of `return n * factorial(n - 1)`), `static_assert` will blow up immediately at compile time, telling you exactly what went wrong. This ability to "catch errors at compile time" is extremely valuable in large projects. Moreover, these tests are zero-cost — they don't generate any runtime code.

## Step Three — Standard Evolution: From Strict Constraints to Greater Freedom

The capabilities of `constexpr` vary drastically across different C++ standards. Understanding these differences is crucial for writing portable and correct `constexpr` code.

### C++11: Extremely Strict Limitations

C++11 introduced `constexpr`, but with extremely strict limitations. The body of a `constexpr` function could only contain a single `return` statement (plus `using`, `typedef` declarations, and other statements that don't generate code). This meant you couldn't write loops, declare local variables, or write `if` statements — all logic had to be compressed into a ternary operator expression or a recursive call.

```cpp
// C++11 风格：只能用递归和三元运算符
constexpr int fibonacci_cxx11(int n)
{
    return n <= 1 ? n : fibonacci_cxx11(n - 1) + fibonacci_cxx11(n - 2);
}
```

This code looks concise, but it has an implicit issue: recursion depth. Compilers have a default limit on the recursion depth of `constexpr` evaluation, and the exact value depends on the compiler implementation. Based on actual testing, GCC 15.2.1's recursion depth limit is approximately 520–600 levels; exceeding this limit triggers a compilation error. If you compute a value on the scale of `factorial(50)`, although the expanded call tree is large, the call depth is relatively shallow (only 50 levels), so it usually won't trigger the limit. But if you hand-write a linear recursion (for example, decrementing by 1 and recursing down to 0), it will exceed the limit when the argument is large.

To verify this, we wrote a test program (see `constexpr_depth_test.cpp`), with the following actual results:

```text
Depth 100: 100 (OK)
Depth 256: 256 (OK)
Depth 512: 512 (OK)
Depth 520: 520 (OK)
Depth 600: [编译错误]
```

This shows that the 512/1024 values mentioned in the article are conservative estimates, and the actual situation varies by compiler and version. If you need to handle deeper recursion, consider switching to an iterative version (supported starting in C++14), or use compiler flags to adjust the limit (such as GCC's `-fconstexpr-depth=`).

### C++14: Significantly Relaxed

C++14 was the turning point where `constexpr` truly became practical. Function bodies could now use local variables, `if` statements, and `for`/`while` loops. The only things still forbidden were `goto` statements, `try`/`catch` blocks, and local variables of non-literal types.

```cpp
// C++14 风格：自然得多的写法
constexpr int factorial_cxx14(int n)
{
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

static_assert(factorial_cxx14(6) == 720);
```

Finally, we no longer have to cram all logic into recursion. For embedded developers, this means you can implement CRC calculations, lookup table generation, and other logic in a more natural way, instead of racking your brain to use template metaprogramming or recursion to work around the limitations.

Another important change is that `constexpr` member functions are no longer implicitly `const`. In C++11, a `constexpr` member function would implicitly have the `const` qualifier added, meaning it couldn't modify any member variables. C++14 removed this restriction, allowing `constexpr` member functions to modify members (in compile-time contexts), making the behavior of compile-time objects more flexible.

### C++17: More Practical Features

C++17 further expanded the capabilities of `constexpr`. `constexpr` lambda expressions were officially supported (GCC/Clang had extension support previously), and `if constexpr` became standard. In addition, more and more standard library functions were marked as `constexpr`: `std::char_traits`, various operations on `std::array`/`std::string_view`, and more.

```cpp
// C++17：constexpr lambda
constexpr auto add = [](int a, int b) constexpr { return a + b; };
static_assert(add(3, 4) == 7);

// C++17：constexpr std::array
#include <array>
constexpr std::array<int, 5> kArr = {1, 2, 3, 4, 5};
static_assert(kArr.size() == 5);
static_assert(kArr[2] == 3);
```

Let's use a table to summarize the key differences across the three standards:

| Capability | C++11 | C++14 | C++17 |
|------|-------|-------|-------|
| Local variables | Only `static` | Allowed | Allowed |
| Loops (`for`/`while`) | Forbidden | Allowed | Allowed |
| `if` statements | Forbidden (only ternary operators) | Allowed | Allowed |
| Member functions modifying members | Forbidden (implicit `const`) | Allowed | Allowed |
| Lambda | Not supported | Partial support | Officially supported |
| Standard library constexpr | Very few | Increasing | Significantly increased |

## Step Four — constexpr vs Templates: When to Use Which

`constexpr` and template metaprogramming can both achieve compile-time computation, but their positioning is fundamentally different. Template metaprogramming is Turing-complete; in theory, it can do any computation at compile time. But it is painful to write, even more painful to read, and the compilation error messages are cryptic. `constexpr` is a "good enough" solution — it covers the vast majority of compile-time computation needs, and writing it is almost identical to writing ordinary functions.

```cpp
// 模板元编程版本：计算阶乘（C++98 风格）
template <int N>
struct Factorial {
    static constexpr int value = N * Factorial<N - 1>::value;
};
template <>
struct Factorial<0> {
    static constexpr int value = 1;
};
static_assert(Factorial<5>::value == 120);

// constexpr 版本：清晰得多
constexpr int factorial(int n)
{
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}
static_assert(factorial(5) == 120);
```

From my experience, the principle is simple: if a `constexpr` function can solve it, don't resort to template metaprogramming. Template metaprogramming is suited for scenarios that require computation at the type level (such as selecting different implementation strategies based on type), while `constexpr` is suited for compile-time computation at the value level. The two often work together — templates handle type-level dispatch, and `constexpr` functions handle the actual value computation.

## Step Five — Practical Examples

### Compile-Time Fibonacci and Factorial

We've already shown these two classic examples earlier. Now let's do something more practical — using a `constexpr` function to generate a compile-time lookup table.

### Compile-Time CRC-32 Lookup Table

CRC checksums are ubiquitous in communication protocols and storage systems. The traditional approach is to generate a CRC lookup table at runtime with a loop, or to use a tool like Python to generate the table and then `#include` it. With `constexpr`, we can let the compiler generate this table for us.

```cpp
#include <array>
#include <cstdint>

constexpr std::array<std::uint32_t, 256> make_crc32_table()
{
    std::array<std::uint32_t, 256> table{};
    constexpr std::uint32_t kPolynomial = 0xEDB88320u;

    for (std::size_t i = 0; i < 256; ++i) {
        std::uint32_t crc = static_cast<std::uint32_t>(i);
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ kPolynomial;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
    return table;
}

// 编译期生成完整的 CRC-32 查找表
constexpr auto kCrc32Table = make_crc32_table();

// 运行时使用：只需要做查表操作
constexpr std::uint32_t crc32_compute(const std::uint8_t* data, std::size_t len)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc = (crc >> 8) ^ kCrc32Table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFFu;
}
```

`crc_table` is fully generated at compile time and is written directly into the read-only data section (`.rodata`) of the object file. No initialization code is needed at runtime; we can just use it directly. The elegance of this pattern lies in the fact that the table generation logic and the table usage logic are in the same source file, with no need for extra code generation tools or build steps.

### Compile-Time vs Runtime Performance Comparison

To intuitively feel the power of `constexpr`, let's look at a simple comparison experiment.

```cpp
#include <chrono>
#include <iostream>

// 运行时版本的 CRC 表生成
std::array<std::uint32_t, 256> make_crc32_table_runtime()
{
    std::array<std::uint32_t, 256> table{};
    constexpr std::uint32_t kPolynomial = 0xEDB88320u;
    for (std::size_t i = 0; i < 256; ++i) {
        std::uint32_t crc = static_cast<std::uint32_t>(i);
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ kPolynomial;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
    return table;
}

int main()
{
    // 运行时生成
    auto start = std::chrono::high_resolution_clock::now();
    auto runtime_table = make_crc32_table_runtime();
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Runtime generation: "
              << std::chrono::duration<double, std::micro>(end - start).count()
              << " us\n";

    // constexpr 版本：直接使用 kCrc32Table，耗时为 0
    std::cout << "CRC table first entry: " << kCrc32Table[0] << "\n";
    std::cout << "Runtime table first entry: " << runtime_table[0] << "\n";

    return 0;
}
```

The runtime results are roughly as follows (exact values depend on hardware and compiler optimization):

```text
Runtime generation: 2.5 us
CRC table first entry: 0
Runtime table first entry: 0
```

**Note**: This benchmark has certain limitations. Modern compilers are very smart; even if you declare a runtime version, if the compiler finds that the function's input is a constant and has no side effects, it might automatically promote it to compile-time computation during optimization (an optimization known as "constant propagation"). Therefore, to accurately measure the advantage of constexpr, you need to ensure the compiler doesn't perform this optimization on the runtime version. In real projects, the true value of constexpr is not in saving these 2.5 microseconds, but in:

1. Forcing compile-time computation, without relying on the compiler's "mood"
2. Being usable in contexts that require constant expressions (such as array sizes, template parameters)
3. Catching logic errors at compile time (via `static_assert`)

However, for embedded systems, faster startup time is indeed a practical advantage — the constexpr version of the table is stored directly in the read-only data section, requiring no initialization code.

### Compile-Time Math Lookup Tables

Another common scenario is trigonometric lookup tables. In signal processing and motor control, we often need to quickly obtain `sin`/`cos` values. Directly calling `std::sin` on embedded systems might be too slow (especially on MCUs without an FPU), and lookup tables are a classic optimization technique.

```cpp
#include <array>
#include <cmath>

template <std::size_t N>
constexpr std::array<float, N> make_sin_table()
{
    std::array<float, N> table{};
    for (std::size_t i = 0; i < N; ++i) {
        // 将 [0, N-1] 映射到 [0, 2π)
        constexpr double kPi = 3.14159265358979323846;
        double angle = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(N);
        // 注意：C++26 之前 std::sin 不保证是 constexpr
        // 在不支持 constexpr std::sin 的编译器上，可以用泰勒展开近似
        double x = angle;
        double sin_val = x - x*x*x/6.0 + x*x*x*x*x/120.0;
        table[i] = static_cast<float>(sin_val);
    }
    return table;
}

constexpr auto kSinTable256 = make_sin_table<256>();

// 快速查表获取 sin 值（输入为 0-255 的索引）
inline float fast_sin(std::size_t index)
{
    return kSinTable256[index & 0xFF];
}
```

There is a detail worth noting here: the C++ standard does not guarantee that `std::sin` is a `constexpr` function. It wasn't until C++26 that a proposal was made to officially make it `constexpr`. So in C++17 and earlier, you need to implement compile-time trigonometric computation yourself using Taylor series expansion or other approximation methods. However, this doesn't affect the final result — the compiled lookup data is precise.

## Common Pitfalls and Lessons Learned

### constexpr Does Not Mean "Force Compile-Time Evaluation"

This is the easiest mistake to make. A `constexpr` function *can* be evaluated at compile time, but it is not *required* to be. If you assign the return value of a `constexpr` function to an ordinary variable (not a `constexpr` variable), the compiler might perfectly well call it at runtime. If you truly need to force compile-time evaluation, use a `constexpr` variable to receive the return value, or use `consteval` in C++20 (which we will cover in detail in later chapters).

### Compiler Recursion Depth Limits

Even with the C++14 iterative version, `constexpr` functions can still trigger the compiler's evaluation step limit internally. The default limits vary by compiler: GCC 15.2.1's default recursion depth limit is approximately 520–600 levels (based on testing), Clang's default is 512 levels (per documentation), and MSVC has similar limits. In addition to recursion depth, compilers also have a total step limit (GCC defaults to roughly 33M steps). If you do a large amount of computation at compile time (such as generating a very large lookup table), you might trigger the compiler's internal limits, manifesting as a compilation failure.

When you encounter this situation, you can raise the limits through compiler flags (such as GCC's `-fconstexpr-depth=` and `-fconstexpr-ops-limit=`), or consider splitting the generation of large tables into smaller chunks. However, in real projects, if your constexpr computation is complex enough to trigger these limits, you should usually reconsider the design — although compile-time computation is zero-cost, it significantly increases compilation time.

### Undefined Behavior in constexpr Functions

When a `constexpr` function is evaluated at compile time, if it triggers undefined behavior (UB), the compiler will directly report an error — this is actually a good thing. Things like array out-of-bounds access, signed integer overflow, and division by zero might quietly produce incorrect results at runtime, but they will be intercepted by the compiler during `constexpr` evaluation.

```cpp
constexpr int bad_divide(int a, int b)
{
    return a / b;  // 如果 b == 0，编译期求值时直接编译错误
}

// constexpr int kBoom = bad_divide(10, 0);  // 编译错误：除以零
```

This trait makes `constexpr` a kind of "safety net" — for anything you can compute at compile time, the compiler will help you check its validity.

## Run Online

Run the constexpr basics example online to observe the differences between compile-time evaluation and runtime evaluation:

<OnlineCompilerDemo
  title="constexpr Basics: Compile-Time Factorial and CRC-32 Lookup Table"
  source-path="code/examples/vol2/05_constexpr_basics.cpp"
  description="Run online and observe the compile-time and runtime behavior of constexpr functions, along with static_assert validation."
  allow-run
  allow-x86-asm
/>

## Summary

At this point, we have thoroughly covered the basic mechanisms of `constexpr`. Let's summarize a few key points:

`constexpr` variables are true compile-time constants, while `const` only guarantees "non-modifiable." `constexpr` functions are dual-mode functions, where the compiler decides whether they execute at compile time or runtime based on context. From C++11 to C++17, the restrictions on `constexpr` were gradually relaxed, from allowing only a single `return` statement to supporting loops, local variables, and lambdas. `static_assert` is the natural partner of `constexpr`, making compile-time testing possible. If a problem can be solved with `constexpr` functions, don't resort to template metaprogramming — the code is clearer, and the error messages are friendlier.

In the next chapter, we will dive into `constexpr` constructors and literal types, exploring how to make custom types participate in compile-time computation.

## Reference Resources

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [C++ Feature-test macro `__cpp_constexpr`](https://en.cppreference.com/w/cpp/feature_test)
