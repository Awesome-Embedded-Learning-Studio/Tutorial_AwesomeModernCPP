---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the rules of function overloading and the usage of default parameters,
  understand the overload resolution mechanism, and avoid common conflicts between
  the two.
difficulty: beginner
order: 3
platform: host
prerequisites:
- 参数传递方式
reading_time_minutes: 14
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Overloading and Default Parameters
translation:
  source: documents/vol1-fundamentals/ch03/03-overloading-default.md
  source_hash: b244969ba339349878a21892aab8a6e088718079aa413e6308523671a91bd88d
  translated_at: '2026-06-24T00:30:20.469314+00:00'
  engine: anthropic
  token_count: 2145
---
# Overloading and Default Parameters

In the previous chapter, we clarified the various methods of parameter passing: pass by value, pass by pointer, and pass by reference. Now, a new question arises: suppose we want to write a `print` function to print integers, floating-point numbers, and strings. These three tasks are essentially "printing," but the rules of C require every function to have a unique name. Consequently, we would have to write `print_int()`, `print_float()`, and `print_string()`—naming them is tedious enough, and calling them requires manually deciding which one to use.

C++ says: the same concept does not need different names. **Function overloading** allows functions with the same name to exhibit different behaviors based on their arguments, while **default parameters** make those arguments that are "almost always passed with the same value" completely transparent. These two features are fundamental to designing good interfaces, so let's master them in this chapter.

## First Step — Understanding Function Overloading

The core rule of function overloading is very simple: multiple functions can share the same name as long as their **parameter lists** differ—either in the types of the parameters or in the number of parameters. Note that the return type is not a factor—the compiler will not distinguish overloads based solely on the return type. Many beginners get confused here, thinking "returning `int` and returning `double` should count as different functions," but they really don't, because the call site might completely ignore the return value, so the compiler cannot see the return type in that context.

Let's look at the most basic example:

```cpp
#include <cstdio>

void print(int value)
{
    std::printf("Integer: %d\n", value);
}

void print(double value)
{
    std::printf("Double: %f\n", value);
}

void print(const char* str)
{
    std::printf("String: %s\n", str);
}
```

When called, the compiler automatically selects the corresponding version based on the argument types:

```cpp
print(42);       // 调用 print(int)
print(3.14);     // 调用 print(double)
print("Hello");  // 调用 print(const char*)
```

To achieve the same effect in C, we would need three separate functions with three different names, requiring us to decide which one to call with every use. In contrast, the advantage of overloading in API design is obvious—callers only need to remember a single name.

Differences in the number of parameters can also constitute overloading. This pattern is extremely common in real-world engineering—peripheral initialization functions often provide both a "recommended configuration" entry point and a "fully customizable" one:

```cpp
void init_uart(int baudrate)
{
    // 使用默认配置：8 数据位、1 停止位、无校验
}

void init_uart(int baudrate, int databits, int stopbits, char parity)
{
    // 使用自定义配置
}
```

## Step 2 — Understanding Overload Resolution

On the surface, calling an overloaded function seems as simple as "writing the name and passing arguments." However, behind the scenes, the compiler executes a rigorous decision-making process known as **overload resolution**. Whenever we call a function that has multiple overloaded versions, the compiler gathers all candidate functions with matching names and evaluates them one by one to determine: **which one is the "best fit"?** It is important to emphasize that the compiler does not understand your business semantics; it mechanically scores candidates according to language rules to select the version with the highest match.

When templates are not involved, we can understand the compiler's criteria as a "matching priority chain" ranging from strong to weak. At the top of the hierarchy is **exact match**—where the type of the argument exactly matches the type of the parameter. If an exact match cannot be found, the compiler considers **promotion**, such as `char` to `int` or `float` to `double`. Next comes **standard conversion**, for example, `int` to `double`. User-defined type conversions are considered last. This order is critical: once a viable match is found at a certain level, the rules in subsequent levels are completely ignored.

Let's use a common example to demonstrate this. Suppose we define both `process(int)` and `process(double)`:

```cpp
void process(int x) { /* ... */ }
void process(double x) { /* ... */ }
```

When calling `process(5)`, the literal `5` is inherently an `int`, which is an exact match for `process(int)`. Meanwhile, `process(double)` requires a conversion from `int` to `double`. An exact match takes precedence over any form of conversion, so `process(int)` is definitely the one called. Conversely, the `5.0` in `process(5.0)` is a `double`, so the exact match occurs for `process(double)`.

A slightly more confusing case is `process(5.0f)`. The type of `5.0f` is `float`, and we don't have a `process(float)` overload. At this point, the compiler compares two possible paths: promoting `float` to `double`, or converting `float` to `int`. The former is a standard promotion between floating-point types and is considered more natural and safe; the latter involves truncation semantics and has lower priority. Therefore, `process(double)` is ultimately called. This illustrates a key fact: **overload resolution is not "least character matching," but "most reasonable type path matching."**

The truly headache-inducing situations often arise when the rules cannot determine a winner. For example, if both `func(int, double)` and `func(double, int)` exist, calling `func(5, 5)` results in identical matching costs for both candidate functions—for the first version, one argument is an exact match and the other requires a standard conversion; for the second version, the situation is symmetric. The compiler won't try to guess your intent; it simply judges the call to be ambiguous and terminates with a compilation error.

> ⚠️ **Warning**
> Overload ambiguity is not always as obvious as the example above. When you define multiple overloaded versions and implicit conversions exist between parameters (such as `int` and `long`, or `float` and `double`), ambiguity can pop up in unexpected places. The most reliable approach is: **when designing interfaces, avoid distinguishing overloads solely by parameter order or subtle type differences.** If ambiguity arises, specify the types explicitly, or simply use different function names.

This reflects a crucial design philosophy in C++: as long as there are equally viable choices that cannot be compared for superiority, the compiler would rather refuse to compile than make a decision for the programmer. This is also a fundamental characteristic of C++'s strong type system—clarity always trumps convenience.

## Step 3 — Master Default Arguments

In real-world engineering, "the more parameters, the better" is not true for functions. Often, a function's parameters fall into a few categories: core required parameters that change with every call; high-frequency configurations that remain almost unchanged and take fixed values in the vast majority of scenarios; and advanced options that are adjusted only in rare cases. If forced to write out every parameter explicitly in every call, the code becomes not only verbose but also quickly obscures the truly important information.

Default arguments exist precisely to solve this problem—**for parameters where you have already decided on a "default behavior," just don't make the caller worry about them.**

```cpp
void configure_uart(int baudrate,
                    int databits = 8,
                    int stopbits = 1,
                    char parity = 'N')
{
    // 配置 UART
}
```

The most common invocation form retains only the parameter we truly care about:

```cpp
configure_uart(115200);              // 只指定波特率，其余全部默认
configure_uart(115200, 8);           // 只改数据位
configure_uart(115200, 8, 2);        // 改数据位和停止位
configure_uart(115200, 8, 2, 'E');   // 全部自定义
```

From an interface design perspective, this is a very gentle approach to forward compatibility: we can continuously append new optional capabilities to the right side of a function without breaking existing code.

The syntax for default parameters appears simple, but the rules are actually quite strict, and many developers run into pitfalls.

**Rule one: Default parameters must appear contiguously from right to left.** When the compiler processes a function call, it can only determine which values should use defaults by "omitting trailing parameters." We cannot skip intermediate parameters—if we want to pass a value to the third parameter, all preceding parameters must be explicitly provided. Therefore, the order of parameters in a function signature is critical: **place the parameters most frequently customized on the left, and the parameters that rarely change on the right.**

```cpp
// 正确：默认参数从右向左连续
void init_spi(int freq, int mode = 0, int bits = 8);

// 错误：非默认参数不能出现在默认参数后面
// void bad_init(int freq = 1000000, int mode, int bits);  // 编译错误
```

**Rule Two: Default parameters can be specified only once, and they should be placed in the declaration.** This is particularly important in projects where header files and source files are separated. The default value is part of the interface, not an implementation detail—if you repeat the default parameter in the `.cpp` file, the compiler will treat it as an attempt to redefine the rule and raise an error.

```cpp
// uart.h —— 声明时指定默认参数
void configure_uart(int baudrate, int databits = 8, int stopbits = 1);

// uart.cpp —— 定义时不要重复默认参数
void configure_uart(int baudrate, int databits, int stopbits)
{
    // 实现
}
```

> ⚠️ **Warning**
> Defining a default value in both the declaration and the definition is a common mistake for beginners. The error messages can sometimes be quite unintuitive, making it frustrating to locate the issue. Remember: **write default parameters in the declaration, not the definition**.

## Step 4 — Overloading vs. Default Parameters: Which One to Choose

Function overloading and default parameters both make interfaces more flexible, but their use cases do not entirely overlap. The choice depends on the specific problem you are solving.

When you need to **handle different argument types**, function overloading is the only option—default parameters cannot do this. For `print(int)` and `print(const char*)`, the parameter types are completely different, and the behaviors differ as well. This can only be achieved through overloading.

When you need to **reduce the number of arguments and provide default behavior**, default parameters are the more concise choice. `configure_uart(115200)` and `configure_uart(115200, 8, 2, 'E')` perform the same task, just with varying levels of detail. Using default parameters is the most natural approach here.

However, the situation requiring the most caution is **mixing the two**. If function overloading and default parameters are designed poorly, they can create very tricky ambiguity issues. Consider this classic counter-example:

```cpp
void process(int value)
{
    std::printf("Single: %d\n", value);
}

void process(int value, int factor = 2)
{
    std::printf("Scaled: %d\n", value * factor);
}

process(10);  // 歧义！调用第一个？还是第二个（使用默认参数）？
```

When the compiler encounters `process(10)`, it finds that both versions are viable matches—the first is an exact match, and the second is also an exact match (only the second parameter uses a default value). Since the cost is identical on both sides, the compiler cannot make a choice and reports an ambiguity error directly.

> ⚠️ **Warning**
> Overloading and default parameters overlapping on the same interface is an almost guaranteed recipe for trouble. Our advice is: for a given function name, either use only overloading (multiple versions with different parameter types) or use only default parameters (one version where some parameters have default values), but do not mix the two. If you truly need to support both "different types" and "different parameter counts," consider encapsulating the logic for different types into distinct function names. While this may seem less "elegant" than overloading, it at least avoids ambiguity.

## Live Demo — overload.cpp

Let's integrate the previous usage into a complete program to demonstrate multiple `print` overloads, the practical application of default parameters, and an intentionally created ambiguity error with its fix:

```cpp
// overload.cpp
// Platform: host
// Standard: C++17

#include <cstdint>
#include <cstdio>
#include <cstring>

// ---- 多个 print 重载 ----

void print(int value)
{
    std::printf("int:    %d\n", value);
}

void print(double value)
{
    std::printf("double: %.2f\n", value);
}

void print(const char* str)
{
    std::printf("string: %s\n", str);
}

// ---- 默认参数示例 ----

void draw_rect(int width, int height, bool fill = false,
               char brush = '#')
{
    std::printf("绘制矩形 %dx%d, fill=%s, brush='%c'\n",
                width, height,
                fill ? "true" : "false",
                brush);
}

// ---- 修复歧义：用不同的函数名替代混搭 ----

void scale_value(int value)
{
    std::printf("原始值: %d\n", value);
}

void scale_value(int value, int factor)
{
    std::printf("缩放后: %d (factor=%d)\n", value * factor, factor);
}

int main()
{
    // 演示重载
    std::printf("=== 函数重载 ===\n");
    print(42);
    print(3.14159);
    print("Hello, overloading!");

    // 演示默认参数
    std::printf("\n=== 默认参数 ===\n");
    draw_rect(10, 5);                  // fill=false, brush='#'
    draw_rect(10, 5, true);            // fill=true,  brush='#'
    draw_rect(10, 5, true, '*');       // 全部自定义

    // 演示修复后的"重载 + 不同参数数量"
    std::printf("\n=== 不同参数数量 ===\n");
    scale_value(7);
    scale_value(7, 3);

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o overload overload.cpp
./overload
```

**Output:**

```text
=== 函数重载 ===
int:    42
double: 3.14
string: Hello, overloading!

=== 默认参数 ===
绘制矩形 10x5, fill=false, brush='#'
绘制矩形 10x5, fill=true, brush='#'
绘制矩形 10x5, fill=true, brush='*'

=== 不同参数数量 ===
原始值: 7
缩放后: 21 (factor=3)
```

If we define both `process(int)` and `process(int, int = 2)` from the previous ambiguous example, and then call `process(10)`, the compiler will report an error directly:

```text
overload.cpp:xx:xx: error: call of overloaded 'process(int)' is ambiguous
```

The solution is exactly what we demonstrated—split the two versions into different function names, or remove one overload and use default parameters (keeping a single version), so the call site semantics are no longer ambiguous.

## Run Online

Run the comprehensive example of function overloading and default parameters online:

<OnlineCompilerDemo
  title="Function Overloading and Default Parameters"
  source-path="code/examples/vol1/11_overloading_default.cpp"
  description="Run online to observe type matching in function overloading and default parameter filling behavior."
  allow-run
/>

## Try It Yourself

### Exercise 1: The `max` Overload Family

Write a set of overloaded functions named `max_value` that accept two `int`, two `double`, and two `const char*` (compare lexicographically and return the larger pointer). Call them in `main` and print the results.

```text
max_value(3, 7)         -> 7
max_value(2.5, 1.8)     -> 2.5
max_value("apple", "banana") -> banana
```

### Exercise 2: Logging function with default parameters

Write a `log_message` function with the signature `void log_message(const char* text, const char* level = "INFO", bool show_timestamp = false)`. Call it using different parameter combinations to observe how default parameters behave.

### Exercise 3: Compilable or ambiguous

Will the code below compile successfully? If so, which `func` will be called? Think it through before verifying on the machine:

```cpp
void func(int x) { }
void func(short x) { }

int main()
{
    func('A');  // 歧义？还是能编译？
    return 0;
}
```

**Hint:** The type of `'A'` is `char`. What kind of conversion levels do `char` → `int` and `char` → `short` belong to? Do integral promotion and integral conversion have the same priority in overload resolution?

## Summary

In this chapter, we explored two important tools for C++ function interface design. Function overloading allows functions with the same name to exhibit different behaviors based on argument types and the number of arguments. The compiler determines which version to call through a strict set of overload resolution rules—an exact match takes precedence over a promotion, and a promotion takes precedence over a standard conversion. When two candidate functions are equally good, the compiler reports an ambiguity error. Default parameters allow callers to omit trailing arguments that are "almost always the same value." The rule is that default values must appear contiguously from right to left and are specified only once at the declaration. Both tools have their strengths—overloading handles "different types," while default parameters handle "optional arguments"—but combining them can easily lead to ambiguity, so we must be cautious.

In the next chapter, we will look at `inline` and `constexpr` functions—when the overhead of a function call becomes the problem, what mechanisms does C++ provide to eliminate it?
