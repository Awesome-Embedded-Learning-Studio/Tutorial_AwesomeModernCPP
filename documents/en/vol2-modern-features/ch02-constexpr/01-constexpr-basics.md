---
chapter: 2
cpp_standard:
- 11
- 14
- 17
description: From constexpr variables to constexpr functions, understand the mechanics
  of compile-time evaluation and the limits each standard progressively relaxed
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 0: Move Construction and Move Assignment'
reading_time_minutes: 20
related:
- constexpr Constructors and Literal Types
- 'Compile-Time Computation in Practice: From Lookup Tables to Compile-Time Strings'
tags:
- host
- cpp-modern
- intermediate
- constexpr
- 编译期计算
title: 'constexpr Basics: Moving Computation into Compile Time'
translation:
  source: documents/vol2-modern-features/ch02-constexpr/01-constexpr-basics.md
  source_hash: 6bae87c7e58cdbb5d4b626943535193b22f01d3a93a0a8aeedffbbef63e5d3a0
  translated_at: '2026-11-22T16:36:41+00:00'
  engine: glm-5.3-refresh
  token_count: 16107
---
# constexpr Basics: Moving Computation into Compile Time

I say, let's put the whole thing more bluntly.png

Actually, the scenarios where `constexpr` applies aren't about fast-or-not—sure, speed is one of the optimizations it brings. But the main reason we get to know `constexpr` and its good friends—`consteval`, `constinit`, and the rest—is to **take everything that can be settled during compilation** and have it computed up front.

Let's put it directly: the problem `constexpr` solves isn't really about speed—it's about whether the computation still needs to happen at all. When you write `constexpr int kBufferSize = 256;` in your code, you are telling the compiler: this value is already settled at compile time, just write it straight into the binary and be done. At runtime, not a single instruction is spent.

For instance, the moment we happily write `constexpr int kBufferSize = 256;`, we're telling the compiler: this value is settled at compile time, write it into the binary, end of story. Doing it this way lets the runtime pull the number out with a single immediate-value pulse from the CPU, instead of grinding 256 out the hard way through several registers—or even a few memory accesses—before the number finally surfaces (whew, that was rough).

All this talk is worth less than one run, so let's look straight at the assembly output of a piece of test code (GCC 15.2.1, -O2 optimization):

```cpp
constexpr int kBufferSize = 256;

int get_buffer_size()
{
    return kBufferSize;
}
```

The compiled assembly—I've verified it myself:

```asm
get_buffer_size():
    movl    $256, %eax
    ret
```

See: the function just returns the immediate value 256, with no memory access and no computation steps. The value was worked out by the compiler at compile time and hard-coded into the instruction—more direct than any explanation in words.

## Starting from constexpr Variables

### Compile-Time Constants vs const

Let's start from the pair of concepts people mix up most easily: `const` and `constexpr`. The semantics of `const` is "this variable cannot be modified after initialization"—as for the initial value, leaving it to be computed at runtime is perfectly allowed. `constexpr` is different: it demands that the initial value be determinable at compile time. The two look alike, but what they promise is completely different, and we should part ways between them early on.

```cpp
// const：运行时常量，初始值可以来自运行时
int get_runtime_value();
const int kSize = get_runtime_value();     // OK，kSize 是 const 但不是编译期常量

// constexpr：编译期常量，初始值必须能在编译期算出来
constexpr int kBufferSize = 256;           // OK，256 是字面量
constexpr int kMask = kBufferSize - 1;     // OK，由编译期常量计算而来

// constexpr int kBad = get_runtime_value(); // 编译错误！初始值不是常量表达式
```

`kSize`, then: the compiler won't let you modify it, but its value isn't settled until runtime. So you can't use it to declare an array size (C-style arrays in C++ require a compile-time constant as their length), and it can't serve as a non-type template parameter. `kBufferSize` has neither of those restrictions—its value is fixed at compile time.

There really is a rule in the standard that runs against many people's intuition, and it deserves a pause: the C++ standard says that as long as a `const` integer variable is initialized with a constant expression, it becomes a constant expression itself. So at global or namespace scope, a declaration like `const int kSize = 256;` can be used as an array size and as a non-type template parameter. With that, the impression that "const can never enter compile-time contexts" stops holding at integer types.

> One more remark on linkage: at global or namespace scope, both `const` variables and `constexpr` variables carry internal linkage by default (same as `static`). That is a property at the linking level, a different matter from the "can it be used as a constant expression" question above—just keep the two things filed apart.

So you might ask: what does `constexpr` have going for it? What it puts on the table is intent, out in the open: by tagging `constexpr`, you are explicitly demanding a compile-time constant. Its range of application is all literal types, integers included but not only. And the compiler will strictly require the initializer to be a constant expression, erroring out on the spot if it can't be done. `const` has no such enforcement—whether a `const` initializer counts as a constant expression depends entirely on what you initialized it with, and the compiler doesn't check that for you. So when a compile-time constant is what we need, we write `constexpr` explicitly and don't rely on the luck of "a const that happened to be initialized with a constant".

### Requirements for constexpr Variables

So what conditions does a `constexpr` variable declaration have to satisfy? Let's check: there are three of them. Its type must be a literal type, its initialization must be completed on the spot, and the initializer expression must be a constant expression.

What is a "literal type"? For now, you only need to know: scalar types (`int`, `float`, pointers, these), reference types, and class types with `constexpr` constructors—those are what "literal type" refers to. The next article in this same chapter, on `constexpr` constructors and literal types, unfolds the term in full. Understanding it by the scope above is enough for this article.

## constexpr Functions: Two Evaluation Moments

`constexpr` functions are the most interesting part of this machinery. They can work in two settings: when all of their arguments are compile-time constants and the context demands compile-time evaluation, what you get is a compile-time result. Otherwise, they simply stay for runtime like ordinary functions. Which path is taken when is decided by context as far as the compiler is concerned—we'll see it in examples.

### The Basic Form

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

See—one function, two paths out. This is the design intent of `constexpr` functions: you write the code once, and when it executes is decided by the compiler from context. When the context wants a constant, it naturally has the result computed at compile time. When the context wants a runtime value, it honestly runs as an ordinary function. That ability makes `constexpr` functions far more flexible than pure compile-time machinery such as template metaprogramming—we'll set them side by side later on.

We turned the two evaluation paths of this very function into an animation: you can play it, pause it, or step through it one press at a time, and get a clear look at the differences between the two sides:

<Anim id="constexpr-two-worlds" />

### static_assert and constexpr Working Together

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

If you introduce a bug in your `factorial` implementation (say, mistyping `n <= 1` as `n < 1`), the `static_assert` blows up in your face at compile time and points straight at the problem. The error gets caught at compile time—an ability worth real money in large projects. And these tests are zero-cost—they generate no runtime code whatsoever.

## What Each Standard Can Do: C++11 to C++17

The capabilities of `constexpr` differ enormously across C++ standards. If we can't tell what our tool can do in which standard, the code may compile happily here and tip over once the standard changes. Let's go version by version and see what got relaxed.

### C++11: Extremely Strict Restrictions

C++11 introduced `constexpr`, but under extremely strict constraints. The body of a `constexpr` function could contain nothing but a single `return` statement (plus statements that generate no code, such as `static_assert` or `using` declarations). That means you could not write loops, could not declare local variables, and could not write `if-else`—all logic had to be squeezed into a ternary operator expression or recursive calls.

```cpp
// C++11 风格：只能用递归和三元运算符
constexpr int fibonacci_cxx11(int n)
{
    return n <= 1 ? n : fibonacci_cxx11(n - 1) + fibonacci_cxx11(n - 2);
}
```

This way of writing looks tidy, but the hidden danger sits precisely in recursion depth. Compilers impose a default limit on the recursion depth of `constexpr` evaluation, and GCC's default is 512. We measured it the dumb way: write a linear recursion `linear_recursive(N)` that decrements by 1 down to 0, so its depth equals its argument, then compile one program per depth. N=511 compiles; N=512 and the compiler errors out. The behavior is identical on Compiler Explorer's GCC 15.2 and on our local 16.2.1:

```shell
> g++ -std=c++17 -O2 depth512.cpp
```

And then we get:

```text
depth512.cpp:3:45: error: 'constexpr' evaluation depth exceeds maximum of 512
    (use '-fconstexpr-depth=' to increase the maximum)
```

> Where does the number 512 come from? The C++ standard recommends that implementations support at least 512 recursive `constexpr` invocations, and GCC picked exactly that floor. Curiously, the GCC manual documents defaults for the other two limits: 33554432 (about 33.5M operations) for total steps under `-fconstexpr-ops-limit`, and 262144 for loop iterations under `-fconstexpr-loop-limit`. Depth alone has no documented default; the 512 in the error message is the number actually in force.

Now we owe you a confession. An earlier version of this article showed a table from a sequential test: evaluating depths 100 through 600 in one program, shallowest first, produced "512 OK, 520 OK, 600 compile error", so we wrote "GCC's limit is roughly 520-600 levels". Really? Release that commented-out depth-600 evaluation in the old test program and recompile—it passes too. The boundary that old table measured came from the test order itself, not from the compiler's actual limit.

Where did the fake boundary come from? GCC caches `constexpr` evaluation results per function. Evaluate a shallow argument once, even just `linear_recursive(1)`, and later deeper evaluations reuse the already-computed layers, which no longer count toward depth. A sequential test walks from shallow to deep, storing each shallow result in the cache before the deeper lines run, so the depth counter never accumulates. You can measure this cache directly: after evaluating `linear_recursive(100)` once in the same file, 600 passes and 612 fails—the boundary moved to exactly 100 + 512. Warming up with a different function does nothing at all; the cache is keyed by the function itself.

Clang has no such per-function cache. Feed the same sequential test to Clang and the depth-512 line fails on the spot—you cannot even reproduce our old table. The companion `constexpr_limits_test.cpp` keeps the shallow-to-deep test on purpose: its output marks the 512, 520, and 600 lines as `cache hit`, every one of them seemingly passing, every one of them carried by the cache. So if you want a compiler's real limit, compile one depth per program; numbers from a sequential test don't count.

What about `fibonacci(50)`? Do not worry about its depth—50 levels is nowhere near 512. The old version of this article said the limit "usually is not triggered" there, and that was only half true: the depth limit indeed is not, but the call tree grows exponentially, and the total-step limit intercepts it first—still a failed build. GCC compiles `fibonacci(30)`, about 2.7 million calls, without complaint; at 50 you get a different error:

```text
$ g++ -std=c++17 -O2 fib50.cpp
fib50.cpp:3:12: error: 'constexpr' evaluation operation count exceeds limit of 33554432
    (use '-fconstexpr-ops-limit=' to increase the limit)
```

If you genuinely need deeper recursion, two ways out. Since C++14 you can rewrite the recursion as a loop—depth stays at 1, and a 600-level workload never comes near the limit. If rewriting is not an option, raise the cap with `-fconstexpr-depth=`. But that flag only raises depth; the total-step limit stays where it was, so do not expect one option to fix everything.

You do not have to take our word for any of this. The test program sits right below (both `linear_recursive` and its loop version live in the same source file)—click "Try it yourself" to run it. With the default `kDepth = 511` it compiles; change it to 512 and hit Run, and the result pane shows the compiler's error verbatim. Then uncomment the `kWarm` line and set `kDepth` to 600: the shallow evaluation lands in the cache, and the program compiles again. The trailing `linear_loop(600)` demonstrates the loop version, swallowing a 600-level workload with ease. The online environment is Compiler Explorer's GCC 15.2:

<OnlineCompilerDemo
  title="Hands-On: The constexpr Recursion Depth Limit"
  source-path="code/examples/vol2/51_constexpr_depth_limit.cpp"
  description="Measure GCC's limit online: with kDepth at 511 it compiles; change it to 512 and hit Run—the result pane shows the error verbatim. Uncomment the kWarm line and set kDepth to 600, and the shallow evaluation lands in the cache—it compiles again. The online environment is Compiler Explorer's GCC 15.2."
  run-options="-O2 -std=c++17"
  allow-run
/>

We turned the whole story into an animation—recursion frames stacking down level by level, the error at 512, the loop version holding depth at 1, and `-fconstexpr-depth=` raising the cap. Step through it segment by segment:

<Anim id="constexpr-depth-limit" />

### C++14: Major Relaxations

C++14 is the turning point where `constexpr` became genuinely practical—let's see what got relaxed: function bodies could now use local variables, `if-else` statements, and `for`/`while` loops. Still forbidden were a few things: `goto` and `label` statements, `static` or `thread_local` local variables, local variables of non-literal types, and `try` blocks, which we couldn't write either.

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

Finally, we no longer have to cram all the logic into recursion. For embedded developers, this means you can implement CRC computation, lookup-table generation, and similar logic the natural way, instead of racking your brain to work around the limits with template metaprogramming or recursion.

Another change that's easy to overlook: `constexpr` member functions are no longer implicitly `const`. In C++11, a `constexpr` member function implicitly received the `const` qualifier, which meant it could not modify any member variables. C++14 removed that restriction, so a `constexpr` member function can modify members (in compile-time contexts), making the behavior of compile-time objects considerably more flexible.

The standard library stirred too: functions marked `constexpr` gradually grew in number from C++14 onward—our everyday `std::min` and `std::max` got the mark in C++14. Lambdas still couldn't take part in constant expressions in C++14: closure types weren't yet inside the literal-type tent, and the `constexpr` specifier came later, officially arriving in C++17. So the accurate reading of "partially supported" is: lambdas themselves existed as of C++14; their qualification to participate in `constexpr` had to wait for C++17.

### C++17: More Practical Features

C++17 kept extending the capabilities of `constexpr`: on the lambda side things officially landed, and `if constexpr` became standard equipment. On the standard-library side, more and more functions got marked `constexpr`—within everyday reach there are the various operations of `std::array` and `std::tuple`.

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

Let's gather the capability differences of the three standards into one table:

| Capability                        | C++11                    | C++14              | C++17              |
| --------------------------------- | ------------------------ | ------------------ | ------------------ |
| Local variables                   | `return` only            | Allowed            | Allowed            |
| Loops (`for`/`while`)             | Forbidden                | Allowed            | Allowed            |
| `if-else` statements              | Forbidden (ternary only) | Allowed            | Allowed            |
| Member functions modifying members | Forbidden (implicit `const`) | Allowed         | Allowed            |
| Lambda                            | Not supported            | Partially supported | Officially supported |
| constexpr in the standard library | Very little              | More               | A large increase   |

## constexpr and Template Metaprogramming: How to Choose

Let's lay the two roads side by side. `constexpr` and template metaprogramming can both do compile-time computation, but their positions are entirely different. Template metaprogramming is Turing-complete and can, in theory, do any computation at compile time; the price is that it's painful to write, even more painful to read, and the error messages it compiles into read like scripture from another world. `constexpr` takes the other road: it covers the vast majority of compile-time computation needs, and writing it feels no different from writing an ordinary function.

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

From our own experience, the rule is simple: if a `constexpr` function can handle it, there's no need to bring in template metaprogramming. Template metaprogramming fits scenarios where the computation must happen at the type level (choosing different implementation strategies by type, say), while `constexpr` fits compile-time computation at the value level. The two often work together—templates dispatch at the type level, and `constexpr` functions do the concrete value computation.

## In Practice: Generating Lookup Tables at Compile Time

We've already seen the classic factorial and fibonacci examples. Now for something more practical: let's use a `constexpr` function to generate a lookup table at compile time.

### A Compile-Time CRC-32 Lookup Table

We bump into CRC checksums everywhere in communication protocols and storage systems. The traditional approach generates the CRC lookup table at runtime with a loop, or generates the table with a tool like Python and `#include`s it into the code. With `constexpr`, we can simply have the compiler generate the whole table at compile time.

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

`kCrc32Table` is generated in full well before runtime and ends up written directly into the object file's read-only data section (`.rodata`). No initialization code whatsoever is needed at runtime—just use it as is. This pattern has a practical benefit, too: how the table is generated and how it's used live in the same source file, sparing us extra code-generation tools and sparing the build steps anything new.

We turned the process—generating the whole table at compile time and looking it up at runtime—into an animation you can step through segment by segment:

<Anim id="constexpr-crc-rodata" />

### Compile-Time vs Runtime Performance Comparison

Let's use a simple comparison experiment to see where the difference between compile-time generation and runtime generation lies.

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

The comparison program sits right below (the compile-time generation of `kCrc32Table` and the runtime version live in the same source file)—click "Try it yourself" and run it. The exact number on the Runtime generation line depends on hardware and compiler optimization; the two first-entry lines are always 0:

<OnlineCompilerDemo
  title="动手验证：CRC 表的编译期 vs 运行期生成"
  source-path="code/examples/vol2/32_crc32_runtime_vs_constexpr.cpp"
  description="在线对比：运行时生成一张 CRC-32 表只要几微秒，而 constexpr 版本的表在编译期就已就位，运行时零成本。"
  run-options="-O2 -std=c++17"
  allow-run
/>

That said, this comparison experiment has its limits. Modern compilers are very smart: even if you declare the runtime version, if the compiler sees that the function's input is constant and it has no side effects, it may promote it to compile-time computation during optimization—most people call that "constant folding", and "constant propagation" gets used by extension too. So to measure `constexpr`'s advantage accurately, you have to make sure the compiler doesn't apply that optimization to the runtime version. And in real projects, `constexpr`'s true value isn't in saving those few microseconds. We are explicitly demanding the computation complete at compile time, not banking on the compiler's mood that day, and the result is usable where constant expressions are required—array sizes, template parameters, and the like. If a logic error sneaks in, `static_assert` intercepts it at compile time and you fix it on the spot, instead of finding out at runtime.

For embedded systems, though, the faster startup time is a real, tangible gain—the `constexpr` version's table sits directly in the read-only data section, and we don't run a single line of initialization code.

### Compile-Time Math Lookup Tables

Let's look at another common scenario: trigonometric lookup tables. In signal processing and motor control you often need `sin`/`cos` values fast; calling `std::sin` directly can be too slow on MCUs (microcontrollers) without an FPU (floating-point unit), and a lookup table is the classic optimization.

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

One detail deserves a full confession: the C++ standard does not guarantee that `std::sin` is a `constexpr` function; not until C++26 was it officially marked `constexpr`. So in C++17 and earlier, we have to work the table out ourselves with an approximation. The code picks a three-term Taylor expansion, but a three-term expansion only converges respectably in the neighborhood of 0—spreading it across the full 0-to-2π range is a real problem: check index 128 (exactly 180°), and the table's value comes out to about 0.52, while the true sine is 0. To actually produce a usable table, range reduction is unavoidable: fold the angle into the 0-to-π/2 range, then lean on symmetry to fill in the values of the four quadrants. So treat this as a mechanism demonstration and nothing more—`constexpr` did compute the table at compile time. To use it directly, we'd still owe one more step.

## Things Easy to Misjudge

### constexpr Does Not Force Compile-Time Evaluation

This is the easiest mistake to make. The promise a `constexpr` function gives is that it "can" be evaluated at compile time, not that it "must" be. If you assign the return value of a `constexpr` function to an ordinary variable (not a `constexpr` variable), the compiler is entirely free to call it at runtime. If you truly need to force compile-time evaluation, receive the return value in a `constexpr` variable—or use `consteval` from C++20, which the later article in this same chapter on `consteval` and `constinit` unfolds in detail.

### The Compiler's Recursion Depth Limit

We measured GCC's recursion depth limit standalone back in the C++11 section. Two related facts deserve a second look. First, how the defaults differ across compilers: GCC's depth limit is exactly 512 (that is what the standalone measurements hit), and Clang's default is also 512 levels (documented, and confirmed on clang 22 in our tests); MSVC has similar limits. Second, the total-step limit: switching to the C++14 iterative style escapes depth, but not total steps. GCC defaults to 33554432 steps (about 33.5M), while Clang's `-fconstexpr-steps` default is only about 1M, dozens of times tighter. The `fibonacci(30)` example with its 2.7 million calls compiles happily on GCC yet blows Clang's step limit outright. Total steps govern the overall amount of compile-time evaluation; if you do massive computation at compile time (generating a very large lookup table, say), you may hit this limit, and the symptom is a failed build.

When that happens, you can raise the limits with compiler options (such as GCC's `-fconstexpr-depth=` and `-fconstexpr-ops-limit=`), or consider splitting the generation of a large table into smaller chunks. In practice, though, if your constexpr computation is complex enough to trigger these limits, you should usually reconsider the design—compile-time computation may be zero-cost, but it noticeably inflates compile time.

### Undefined Behavior in constexpr Functions

When a `constexpr` function is evaluated at compile time and triggers undefined behavior (UB), the compiler rejects it with an error outright—and that is actually a good thing. Things like out-of-bounds array access, signed integer overflow, and division by zero may quietly produce wrong results at runtime, but during `constexpr` evaluation the compiler intercepts them.

```cpp
constexpr int bad_divide(int a, int b)
{
    return a / b;  // 如果 b == 0，编译期求值时直接编译错误
}

// constexpr int kBoom = bad_divide(10, 0);  // 编译错误：除以零
```

So for whatever we can compute at compile time, the compiler goes over its legality for you, one item at a time.

## Run It Online

You can also run the constexpr basics example online and observe the difference between compile-time and runtime evaluation:

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
