---
chapter: 2
cpp_standard:
- 11
- 14
- 17
description: From constexpr variables to constexpr functions, master the core mechanisms
  of compile-time computation and the evolution of the standard
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 0: Move Construction and Move Assignment'
reading_time_minutes: 17
related:
- constexpr Constructors and Literal Types
- 'Compile-Time Computation in Practice: From Lookup Tables to Compile-Time Strings'
tags:
- host
- cpp-modern
- intermediate
- constexpr
- 编译期计算
title: 'constexpr Basics: The Art of Compile-Time Evaluation'
translation:
  source: documents/vol2-modern-features/ch02-constexpr/01-constexpr-basics.md
  source_hash: d9391d3e959c68b5be694608b6003607b9ce2e17cc106a8f31230338c9bdd5a0
  translated_at: '2026-09-25T14:55:05+00:00'
  engine: anthropic
  token_count: 7300
---
# constexpr Basics: The Art of Compile-Time Evaluation

Let's keep it simple! The core problem `constexpr` solves is not "is it fast", but "does it even need to be computed". When you write `constexpr int kBufferSize = 256;` in your code, you are telling the compiler: this value is already settled at compile time—just write it straight into the binary. Not a single instruction needs to be spent at runtime. That is more thorough than any runtime optimization.

To verify this, let's look at the assembly output of a piece of test code (GCC 15.2.1, -O2 optimization):

```cpp
constexpr int kBufferSize = 256;

int get_buffer_size()
{
    return kBufferSize;
}
```

The compiled assembly (verified):

```asm
get_buffer_size():
    movl    $256, %eax
    ret
```

As you can see, the function simply returns the immediate value 256—no memory access, no computation whatsoever. This is tangible evidence that "the compiler works it out for you and writes down an immediate value".

In this chapter, we sort out the full story of `constexpr` from scratch: what it is, what it isn't, which restrictions each C++ standard version has relaxed, and how to use it to write safer and faster code.

## Step 1—Getting Clear on constexpr Variables

### Compile-Time Constants vs const

Many people treat `const` and `constexpr` as the same thing—a misconception worth correcting as early as possible. The semantics of `const` is "this variable cannot be modified after initialization", but its initial value can perfectly well be computed at runtime. `constexpr` carries stronger semantics: it demands that the variable's initial value be determinable at compile time.

```cpp
// const: a runtime constant; the initial value may come from the runtime
int get_runtime_value();
const int kSize = get_runtime_value();     // OK, kSize is const but not a compile-time constant

// constexpr: a compile-time constant; the initial value must be computable at compile time
constexpr int kBufferSize = 256;           // OK, 256 is a literal
constexpr int kMask = kBufferSize - 1;     // OK, computed from a compile-time constant

// constexpr int kBad = get_runtime_value(); // Compile error! The initializer is not a constant expression
```

`kSize` is a `const` variable: the compiler won't let you modify it, but its value isn't settled until runtime. That means you cannot use it to declare an array size (C-style arrays in C++ require a compile-time constant as their length), nor as a non-type template parameter. `kBufferSize` has neither of those restrictions—because its value is fixed at compile time.

Here is a pit that is easy to fall into: the C++ standard says that if a `const` integer variable is initialized with a constant expression, then it is itself a constant expression. This means that at global or namespace scope, a declaration like `const int kSize = 256;` can in fact be used as an array size or a non-type template parameter. That runs against the intuition many people hold that "const cannot be used in compile-time contexts". The advantage of `constexpr` is that it states your intent explicitly, applies to all literal types (not just integers), and strictly requires the initializer to be a constant expression.

Here is another pit that is easy to stumble into: at global or namespace scope, a `const` integer variable in C++ has internal linkage by default (just like `static`), and `constexpr` variables have internal linkage too. But if your `const` variable happens to be initialized with a value that can already be computed at compile time, the compiler may go ahead and treat it as a constant expression—that is compiler extension behavior, not something the standard guarantees. So if you need a compile-time constant, write `constexpr` explicitly instead of counting on the compiler to decide for you.

### Requirements for constexpr Variables

For a variable to be declared `constexpr`, a few conditions must hold: it must be of literal type, it must be initialized immediately, and the initializer expression must be a constant expression. We will unpack the notion of literal types in detail in the next article; for now, it is enough to know that scalar types (`int`, `float`, pointers, and so on), reference types, and class types with `constexpr` constructors all count as literal types.

## Step 2—constexpr Functions: A Double Agent

`constexpr` functions are the most interesting part of `constexpr`. We call them a "double agent" because they can work in two settings: when all of their arguments are compile-time constants and the context demands compile-time evaluation, they execute at compile time; otherwise they execute at runtime just like ordinary functions.

### The Basic Form

```cpp
constexpr int square(int x)
{
    return x * x;
}

// Compile-time evaluation: the argument is a literal, and the context is a constexpr variable initialization
constexpr int kResult = square(8);  // The compiler directly replaces kResult with 64

// Runtime evaluation: the argument comes from the runtime
int runtime_input = 42;
int result = square(runtime_input);  // An ordinary function call, executed at runtime
```

See—one function, two destinies. This is actually the essence of how `constexpr` functions are designed: you write the code once, and the compiler decides, based on context, when to execute it. This "context-adaptive" nature makes `constexpr` functions far more flexible than pure compile-time machinery such as template metaprogramming.

We turned the two destinies of this very function into an animation: you can play it, pause it, or single-step through it with the step button, and get a clear look at the differences between the compile-time and runtime sides:

<Anim id="constexpr-two-worlds" />

### static_assert and constexpr: A Perfect Match

`static_assert` is a compile-time assertion: its first argument must be a constant expression. That naturally pairs it with `constexpr` functions—you can use `static_assert` to verify how a `constexpr` function behaves at compile time.

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

If you introduce a bug in your `factorial` implementation (say, mistyping `n <= 1` as `n < 1`), the `static_assert` blows up immediately at compile time and tells you where the problem is. This ability to "catch errors at compile time" is extremely valuable in large projects. And these tests are zero-cost—they generate no runtime code whatsoever.

## Step 3—The Evolution of the Standard: From Hands Tied to Free Rein

The capabilities of `constexpr` differ enormously across C++ standards. Understanding those differences is essential for writing portable and correct `constexpr` code.

### C++11: Extremely Strict Restrictions

C++11 introduced `constexpr`, but under extremely strict constraints. The body of a `constexpr` function could contain nothing but a single `return` statement (plus statements that generate no code, such as `static_assert` or `using` declarations). That means you could not write loops, could not declare local variables, and could not write `if-else`—all logic had to be squeezed into a ternary operator expression or recursive calls.

```cpp
// C++11 style: only recursion and the ternary operator
constexpr int fibonacci_cxx11(int n)
{
    return n <= 1 ? n : fibonacci_cxx11(n - 1) + fibonacci_cxx11(n - 2);
}
```

This code looks tidy, but it hides a problem: recursion depth. Compilers impose a default limit on the recursion depth of `constexpr` evaluation, and the exact number depends on the implementation. In our measurements, GCC 15.2.1 caps recursion depth at roughly 520-600 levels; exceeding that limit triggers a compile error. If you compute a value on the scale of `fibonacci(50)`, the expanded call tree is huge, but the call depth is shallow (only 50 levels), so the limit usually is not triggered. But if you hand-write a linear recursion (say, decrementing by 1 each time down to 0), a large argument will blow past the limit.

To verify this, we wrote a test program (see `constexpr_limits_test.cpp`), and the measured results were:

```text
Depth 100: 100 (OK)
Depth 256: 256 (OK)
Depth 512: 512 (OK)
Depth 520: 520 (OK)
Depth 600: [compile error]
```

This shows that the 512/1024 figures mentioned in the article are conservative estimates; the actual situation varies by compiler and version. If you need to handle deeper recursion, consider switching to an iterative version (supported since C++14), or adjusting the limit with a compiler option (such as GCC's `-fconstexpr-depth=`).

### C++14: Major Relaxations

C++14 is the turning point where `constexpr` became genuinely practical. Function bodies could now use local variables, `if-else` statements, and `for`/`while` loops. The only things still forbidden were `goto`, `label` statements, and local variables of non-literal types.

```cpp
// C++14 style: a much more natural way to write it
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

Finally, we no longer have to cram all the logic into recursion. For embedded developers, this means you can implement CRC computation, lookup-table generation, and similar logic the natural way, instead of racking your brain to work around the limits with template metaprogramming or recursion.

Another important change: `constexpr` member functions are no longer implicitly `const`. In C++11, a `constexpr` member function implicitly received the `const` qualifier, which meant it could not modify any member variables. C++14 removed that restriction, so a `constexpr` member function can modify members (in compile-time contexts), making the behavior of compile-time objects more flexible.

### C++17: More Practical Features

C++17 extended the capabilities of `constexpr` further. `constexpr` lambda expressions became officially supported (GCC/Clang had offered them as extensions before), and `if constexpr` became standard equipment. In addition, more and more functions in the standard library were marked `constexpr`: the various operations of `std::array` and `std::tuple`, `std::min`/`std::max`, and more.

```cpp
// C++17: constexpr lambda
constexpr auto add = [](int a, int b) constexpr { return a + b; };
static_assert(add(3, 4) == 7);

// C++17: constexpr std::array
#include <array>
constexpr std::array<int, 5> kArr = {1, 2, 3, 4, 5};
static_assert(kArr.size() == 5);
static_assert(kArr[2] == 3);
```

Let's summarize the key differences across the three standards in a table:

| Capability | C++11 | C++14 | C++17 |
|------|-------|-------|-------|
| Local variables | `return` only | Allowed | Allowed |
| Loops (`for`/`while`) | Forbidden | Allowed | Allowed |
| `if-else` statements | Forbidden (ternary operator only) | Allowed | Allowed |
| Member functions modifying members | Forbidden (implicit `const`) | Allowed | Allowed |
| Lambda | Not supported | Partially supported | Officially supported |
| constexpr in the standard library | Very little | More | A large increase |

## Step 4—constexpr vs Templates: When to Use Which

Both `constexpr` and template metaprogramming can do compile-time computation, but their positions are entirely different. Template metaprogramming is Turing-complete and can, in theory, perform any computation at compile time; but it is painful to write, even more painful to read, and its compile error messages read like an alien tongue. `constexpr` is the "good enough" solution—it covers the vast majority of compile-time computation needs, and writing it feels almost the same as writing an ordinary function.

```cpp
// Template metaprogramming version: computing the factorial (C++98 style)
template <int N>
struct Factorial {
    static constexpr int value = N * Factorial<N - 1>::value;
};
template <>
struct Factorial<0> {
    static constexpr int value = 1;
};
static_assert(Factorial<5>::value == 120);

// constexpr version: much clearer
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

In our experience, the rule is simple: if a `constexpr` function can handle it, don't reach for template metaprogramming. Template metaprogramming fits scenarios where the computation must happen at the type level (choosing different implementation strategies by type, say), while `constexpr` fits compile-time computation at the value level. The two often work together—templates dispatch at the type level, and `constexpr` functions do the concrete value computation.

## Step 5—Practical Examples

### Compile-Time Fibonacci and Factorial

We already showed these two classic examples earlier. Now let's have something more practical—generating a compile-time lookup table with a `constexpr` function.

### A Compile-Time CRC-32 Lookup Table

CRC checksums are everywhere in communication protocols and storage systems. The traditional approach generates the CRC lookup table at runtime with a loop, or generates the table with a tool like Python and `#include`s it into the code. With `constexpr`, we can have the compiler generate this table for us.

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

// Generate the full CRC-32 lookup table at compile time
constexpr auto kCrc32Table = make_crc32_table();

// Runtime use: only a table lookup is needed
constexpr std::uint32_t crc32_compute(const std::uint8_t* data, std::size_t len)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc = (crc >> 8) ^ kCrc32Table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFFu;
}
```

`kCrc32Table` is generated in full at compile time and ends up written directly into the object file's read-only data section (`.rodata`). At runtime, no initialization code is needed—just use it as is. The elegance of this pattern: the table-generation logic and the table-using logic live in the same source file, with no extra code-generation tools or build steps.

### Compile-Time vs Runtime Performance Comparison

To get a feel for the power of `constexpr`, let's run a simple comparison experiment.

```cpp
#include <chrono>
#include <iostream>

// Runtime version of the CRC table generation
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
    // Runtime generation
    auto start = std::chrono::high_resolution_clock::now();
    auto runtime_table = make_crc32_table_runtime();
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Runtime generation: "
              << std::chrono::duration<double, std::micro>(end - start).count()
              << " us\n";

    // constexpr version: uses kCrc32Table directly, zero elapsed time
    std::cout << "CRC table first entry: " << kCrc32Table[0] << "\n";
    std::cout << "Runtime table first entry: " << runtime_table[0] << "\n";

    return 0;
}
```

The output looks roughly like this (exact numbers depend on hardware and compiler optimization):

```text
Runtime generation: 2.5 us
CRC table first entry: 0
Runtime table first entry: 0
```

**Note**: this benchmark has its limitations. Modern compilers are very smart: even if you declare the runtime version, if the compiler sees that the function's input is constant and it has no side effects, it may promote it to compile-time computation during optimization (an optimization known as "constant propagation"). So, to measure the advantage of constexpr accurately, you need to make sure the compiler does not apply that optimization to the runtime version. In real projects, the true value of constexpr lies not in saving those 2.5 microseconds, but in:

1. Forcing the computation to happen at compile time, independent of the compiler's "mood"
2. Being usable in contexts that require constant expressions (array sizes, template parameters, and so on)
3. Catching logic errors at compile time (via static_assert)

That said, for embedded systems the faster startup time is a real, practical advantage—the constexpr version's table sits directly in the read-only data section and needs no initialization code at all.

### Compile-Time Math Lookup Tables

Another common scenario is trigonometric lookup tables. In signal processing and motor control, you often need `sin`/`cos` values fast. Calling `std::sin` directly can be too slow on embedded targets (especially MCUs without an FPU), and a lookup table is the classic optimization.

```cpp
#include <array>
#include <cmath>

template <std::size_t N>
constexpr std::array<float, N> make_sin_table()
{
    std::array<float, N> table{};
    for (std::size_t i = 0; i < N; ++i) {
        // Map [0, N-1] to [0, 2π)
        constexpr double kPi = 3.14159265358979323846;
        double angle = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(N);
        // Note: before C++26, std::sin is not guaranteed to be constexpr
        // On compilers that do not support constexpr std::sin, a Taylor expansion can approximate it
        double x = angle;
        double sin_val = x - x*x*x/6.0 + x*x*x*x*x/120.0;
        table[i] = static_cast<float>(sin_val);
    }
    return table;
}

constexpr auto kSinTable256 = make_sin_table<256>();

// Fast table lookup for sin values (input is an index in 0-255)
inline float fast_sin(std::size_t index)
{
    return kSinTable256[index & 0xFF];
}
```

One detail worth noting here: the C++ standard does not guarantee that `std::sin` is a `constexpr` function. Not until C++26 is there a proposal to make it officially `constexpr`. So in C++17 and earlier, you need to implement compile-time trigonometric computation yourself with a Taylor expansion or another approximation. This does not affect the final result, though—the compiled lookup-table data is exact.

## Common Pitfalls and Gotchas

### constexpr Does Not Force Compile-Time Evaluation

This is the easiest mistake to make. A `constexpr` function merely "can" be evaluated at compile time, it is not "required" to be. If you assign the return value of a `constexpr` function to an ordinary variable (not a `constexpr` variable), the compiler is entirely free to call it at runtime. If you truly need to force compile-time evaluation, receive the return value in a `constexpr` variable, or use `consteval` in C++20 (which we will cover in detail in a later article).

### The Compiler's Recursion Depth Limit

Even with the C++14 iterative version, the inside of a `constexpr` function can still trip the compiler's evaluation step limits. Each compiler has different defaults: GCC 15.2.1 caps recursion depth at roughly 520-600 levels (measured), Clang defaults to 512 levels (documented), and MSVC has similar limits. Beyond recursion depth, compilers also cap total steps (GCC defaults to about 33M steps); if you do massive computation at compile time (generating a very large lookup table, say), you may hit the compiler's internal limits, and the symptom is a failed build.

When that happens, you can raise the limits with compiler options (such as GCC's `-fconstexpr-depth=` and `-fconstexpr-ops-limit=`), or consider splitting the generation of a large table into smaller chunks. In practice, though, if your constexpr computation is complex enough to trigger these limits, you should usually reconsider the design—compile-time computation may be zero-cost, but it noticeably inflates compile time.

### Undefined Behavior in constexpr Functions

When a `constexpr` function is evaluated at compile time and triggers undefined behavior (UB), the compiler rejects it with an error outright—and that is actually a good thing. Things like out-of-bounds array access, signed integer overflow, and division by zero may quietly produce wrong results at runtime, but during `constexpr` evaluation the compiler intercepts them.

```cpp
constexpr int bad_divide(int a, int b)
{
    return a / b;  // If b == 0, compile-time evaluation fails with a compile error
}

// constexpr int kBoom = bad_divide(10, 0);  // Compile error: division by zero
```

This property makes `constexpr` a kind of safety net—whatever you can compute at compile time, the compiler checks its legality for you.

## Run It Online

Run the constexpr basics example online and observe the difference between compile-time and runtime evaluation:

<OnlineCompilerDemo
  title="constexpr Basics: Compile-Time Factorial and CRC-32 Lookup Table"
  source-path="code/examples/vol2/05_constexpr_basics.cpp"
  description="Run it online and observe the compile-time and runtime behavior of constexpr functions, along with static_assert checks."
  allow-run
  allow-x86-asm
/>

## References

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [C++ feature-test macro `__cpp_constexpr`](https://en.cppreference.com/w/cpp/feature_test)
