# constexpr Basics: The Art of Compile-Time Evaluation

## Introduction

Simply put, `constexpr` solves a core problem that isn't about "speed," but rather "whether calculation is even needed." When you write `constexpr` in your code, you are telling the compiler: this value is already determined at compile time, so just write it directly into the binary. It doesn't cost a single instruction at runtime. This is more thorough than any runtime optimization.

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

As we can see, the function directly returns the immediate value 256, without any memory access or computation. This is direct evidence of "the compiler doing the math for you and embedding an immediate value."

In this chapter, we start from scratch to understand the ins and outs of `constexpr`: what it is, what it isn't, what restrictions each C++ standard version has relaxed, and how to use it to write safer, faster code.

## Step One — Understanding constexpr Variables

### Compile-Time Constants vs const

Many people confuse `const` and `constexpr`, which is a misconception we need to correct early on. The semantics of `const` are "this variable cannot be modified after initialization," but its initial value can be computed entirely at runtime. `constexpr`, on the other hand, has stronger semantics: it requires the variable's initial value to be determinable at compile time.

```cpp
// const：运行时常量，初始值可以来自运行时
int get_runtime_value();
const int kSize = get_runtime_value();     // OK，kSize 是 const 但不是编译期常量

// constexpr：编译期常量，初始值必须能在编译期算出来
constexpr int kBufferSize = 256;           // OK，256 是字面量
constexpr int kMask = kBufferSize - 1;     // OK，由编译期常量计算而来

// constexpr int kBad = get_runtime_value(); // 编译错误！初始值不是常量表达式
```

`runtime_val` is a `const` variable, and the compiler won't let you modify it, but its value is only determined at runtime. This means you can't use it to declare an array size (C-style arrays in C++ require a compile-time constant for their length), nor can you use it as a non-type template parameter. `compile_val`, however, has no such restrictions—because it has a definite value at compile time.

Here is an easy trap to fall into: the C++ standard specifies that if a `const` integer variable is initialized with a constant expression, it is itself a constant expression. This means that in global or namespace scope, a declaration like `const int N = 10;` can actually be used for array sizes and non-type template parameters. This contradicts the intuition many people have that "const cannot be used in compile-time contexts." But the advantage of `constexpr` lies in: it explicitly expresses your intent, it applies to all literal types (not just integers), and it strictly requires the initial value to be a constant expression.

Here is another easy trap to fall into: in global or namespace scope, `const` integer variables in C++ have internal linkage by default (just like `static`), and `constexpr` variables also have internal linkage. But if your `const` variable happens to be initialized with a value computable at compile time, the compiler might treat it as a constant expression—this is a compiler extension, not guaranteed by the standard. So if you need a compile-time constant, explicitly write `constexpr`, and don't rely on the compiler to make that decision for you.

### Requirements for constexpr Variables

To declare a variable as `constexpr`, the following conditions must be met: it must be a literal type, it must be immediately initialized, and the initializing expression must be a constant expression. We will dive into the concept of literal types in the next chapter; for now, you just need to know that scalar types (`int`, `float`, pointers, etc.), reference types, and class types with a `constexpr` constructor all qualify as literal types.

## Step Two — constexpr Functions: The Double Agent

`constexpr` functions are the most interesting part of `constexpr`. We call them "double agents" because they can work in two scenarios: when all their arguments are compile-time constants and the context requires compile-time evaluation, they execute at compile time; otherwise, they execute at runtime just like regular functions.

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

### The Golden Duo: static_assert and constexpr

`static_assert` is a compile-time assertion, and its first parameter must be a constant expression. This naturally pairs with `constexpr` functions—you can use `static_assert` to verify the compile-time behavior of `constexpr` functions.

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

If you write a bug in the implementation of `factorial` (for example, mistakenly writing `*` as `+`), `static_assert` will blow up immediately at compile time, telling you exactly what went wrong. This ability to "catch errors at compile time" is incredibly valuable in large projects. Moreover, these tests are zero-cost—they don't generate any runtime code.

## Step Three — Standard Evolution: From Strict Constraints to Greater Freedom

The capabilities of `constexpr` vary drastically across different C++ standards. Understanding these differences is crucial for writing portable and correct `constexpr` code.

### C++11: Extremely Strict Limitations

C++11 introduced `constexpr`, but with extremely strict limitations. The body of a `constexpr` function could only contain a single `return` statement (along with `using`, `typedef` declarations, and other statements that generate no code). This meant you couldn't write loops, declare local variables, or use `if`—all logic had to be compressed into a ternary operator expression or a recursive call.

```cpp
// C++11 风格：只能用递归和三元运算符
constexpr int fibonacci_cxx11(int n)
{
    return n <= 1 ? n : fibonacci_cxx11(n - 1) + fibonacci_cxx11(n - 2);
}
```

This code looks concise, but it has an implicit issue: recursion depth. Compilers have a default limit on the recursion depth of `constexpr` evaluation, and the exact value depends on the compiler implementation. Based on actual testing, GCC 15.2.1 has a recursion depth limit of about 520–600 levels; exceeding this limit triggers a compilation error. If you compute a value on the scale of `factorial(50)`, although the expanded call tree is large, the call depth is relatively shallow (only 50 levels), so it usually won't trigger the limit. But if you hand-write a linear recursion (e.g., decrementing by 1 and recursing down to 0), it will exceed the limit when the argument is large.

To verify this, we wrote a test program (see `constexpr_depth_test.cpp`), with the following actual results:

```text
Depth 100: 100 (OK)
Depth 256: 256 (OK)
Depth 512: 512 (OK)
Depth 520: 520 (OK)
Depth 600: [编译错误]
```

This shows that the 512/1024 figures mentioned in articles are conservative estimates, and the actual situation varies by compiler and version. If you need to handle deeper recursion, consider switching to an iterative version (supported starting in C++14), or use compiler flags to adjust the limit (such as GCC's `-fconstexpr-depth=`).

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

Finally, we no longer have to cram all logic into recursion. For embedded developers, this means you can implement CRC calculations, lookup table generation, and other logic in a more natural way, instead of racking your brain to bypass limitations with template metaprogramming or recursion.

Another important change is that `constexpr` member functions are no longer implicitly `const`. In C++11, a `constexpr` member function would implicitly have the `const` qualifier added, meaning it couldn't modify any member variables. C++14 removed this restriction, allowing `constexpr` member functions to modify members (in compile-time contexts), making the behavior of compile-time objects much more flexible.

### C++17: More Practical Features

C++17 further expanded the capabilities of `constexpr`. `constexpr` lambda expressions were officially supported (GCC/Clang had extension support previously), and `if constexpr` became standard. Additionally, more and more standard library functions were marked as `constexpr`: `std::char_traits`, various operations on `std::array`, `std::begin`/`std::end`, and more.

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
| `if` statements | Forbidden (must use ternary operator) | Allowed | Allowed |
| Member functions modifying members | Forbidden (implicitly `const`) | Allowed | Allowed |
| Lambda | Not supported | Partially supported | Officially supported |
| Standard library constexpr | Very few | Increased | Massively increased |

## Step Four — constexpr vs Templates: When to Use Which

`constexpr` and template metaprogramming can both achieve compile-time computation, but their positioning is entirely different. Template metaprogramming is Turing-complete; in theory, it can do any computation at compile time. But it is painful to write, even more painful to read, and its compiler error messages are cryptic. `constexpr`, on the other hand, is a "good enough" solution—it covers the vast majority of compile-time computation needs, and writing it is almost identical to writing regular functions.

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

From our experience, the principle is simple: if a `constexpr` function can solve it, don't resort to template metaprogramming. Template metaprogramming is suited for scenarios that require computation at the type level (such as selecting different implementation strategies based on type), while `constexpr` is suited for compile-time computation at the value level. The two often work together—templates handle type-level dispatch, and `constexpr` functions handle the actual value computation.

## Step Five — Practical Examples

### Compile-Time Fibonacci and Factorial

We've already shown these two classic examples earlier. Now let's do something more practical—using a `constexpr` function to generate a compile-time lookup table.

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

`crc_table` is fully generated at compile time and is written directly into the read-only data section (`.rodata`) of the object file. No initialization code is needed at runtime; we can just use it directly. The elegance of this pattern lies in: the table generation logic and the table usage logic reside in the same source file, with no need for extra code generation tools or build steps.

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

The runtime results are roughly as follows (exact numbers depend on hardware and compiler optimization):

```text
Runtime generation: 2.5 us
CRC table first entry: 0
Runtime table first entry: 0
```

**Note**: This benchmark has certain limitations. Modern compilers are very smart; even if you declare a runtime version, if the compiler finds that the function's input is a constant and has no side effects, it might automatically promote it to a compile-time computation during the optimization phase (an optimization known as "constant propagation"). Therefore, to accurately measure the advantage of constexpr, you need to ensure the compiler doesn't perform this optimization on the runtime version. In real projects, the true value of constexpr isn't in saving these 2.5 microseconds, but rather in:

1. Forcing compile-time computation, without relying on the compiler's "mood"
2. Being usable in contexts that require constant expressions (like array sizes, template parameters)
3. Catching logic errors at compile time (via `static_assert`)

However, for embedded systems, faster startup time is indeed a practical advantage—the `constexpr` version of the table is stored directly in the read-only data section, requiring no initialization code.

### Compile-Time Math Lookup Tables

Another common scenario is trigonometric lookup tables. In signal processing and motor control, we often need to quickly obtain `sin`/`cos` values. Directly calling `std::sin` on embedded systems might be too slow (especially on MCUs without an FPU), making lookup tables a classic optimization technique.

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

There is a detail worth noting here: the C++ standard does not guarantee that `std::sin` is a `constexpr` function. It wasn't until C++26 that a proposal was made to officially make it `constexpr`. So in C++17 and earlier, you need to implement compile-time trigonometric calculations yourself using Taylor series expansion or other approximation methods. However, this doesn't affect the final result—the compiled lookup table data is precise.

## Common Pitfalls and Lessons Learned

### constexpr Doesn't Mean "Forced Compile-Time Evaluation"

This is the easiest mistake to make. A `constexpr` function *can* be evaluated at compile time, but it isn't *required* to be. If you assign the return value of a `constexpr` function to a regular variable (not a `constexpr` variable), the compiler might very well call it at runtime. If you truly need to force compile-time evaluation, use a `constexpr` variable to receive the return value, or use `consteval` in C++20 (which we will cover in detail in a later chapter).

### Compiler Recursion Depth Limits

Even with the iterative version in C++14, `constexpr` functions can still trigger the compiler's evaluation step limit internally. The default limits vary by compiler: GCC 15.2.1 has a default recursion depth limit of about 520–600 levels (based on testing), Clang defaults to 512 levels (per documentation), and MSVC has similar limits. Beyond recursion depth, compilers also have a total step limit (GCC defaults to about 33M steps). If you do massive computations at compile time (like generating a very large lookup table), you might trigger these internal compiler limits, manifesting as a compilation failure.

When you encounter this, you can raise the limits via compiler flags (like GCC's `-fconstexpr-depth=` and `-fconstexpr-ops-limit=`), or consider splitting the generation of large tables into smaller chunks. However, in real projects, if your constexpr computation is complex enough to trigger these limits, you should usually reconsider the design—while compile-time computation is zero-cost at runtime, it significantly increases compilation time.

### Undefined Behavior in constexpr Functions

When a `constexpr` function is evaluated at compile time, if it triggers undefined behavior (UB), the compiler will directly report an error—which is actually a good thing. Things like array out-of-bounds access, signed integer overflow, and division by zero might silently produce incorrect results at runtime, but they get intercepted by the compiler during `constexpr` evaluation.

```cpp
constexpr int bad_divide(int a, int b)
{
    return a / b;  // 如果 b == 0，编译期求值时直接编译错误
}

// constexpr int kBoom = bad_divide(10, 0);  // 编译错误：除以零
```

This trait makes `constexpr` a kind of "safety net"—for anything you can compute at compile time, the compiler will help you check its validity.

## Summary

At this point, we have thoroughly covered the basic mechanisms of `constexpr`. Let's summarize a few key points:

`constexpr` variables are true compile-time constants, whereas `const` only guarantees "immutability." `constexpr` functions are dual-mode functions, where the compiler decides whether they execute at compile time or runtime based on context. From C++11 to C++17, the restrictions on `constexpr` were gradually relaxed, going from only allowing a single `return` statement to supporting loops, local variables, and lambdas. `static_assert` is the natural partner of `constexpr`, making compile-time testing possible. If a problem can be solved with `constexpr` functions, don't resort to template metaprogramming—the code is clearer, and the error messages are friendlier.

In the next chapter, we will dive into `constexpr` constructors and literal types, exploring how to make custom types participate in compile-time computation.

## Reference Resources

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [C++ Feature-test macro `__cpp_constexpr`](https://en.cppreference.com/w/cpp/feature_test)
