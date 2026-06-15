---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the rules of function overloading and the use of default parameters,
  understand the overload resolution mechanism, and avoid common conflicts between
  the two.
difficulty: beginner
order: 3
platform: host
prerequisites:
- 参数传递方式
reading_time_minutes: 13
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Overloading and Default Arguments
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/ch03/03-overloading-default.md
  source_hash: 28497e2a384345bb03ad75c710cf07b8c97bc5f06e45a831a8159aa3fdf9e24c
  token_count: 2149
  translated_at: '2026-05-26T10:46:56.509778+00:00'
---
# Overloading and Default Arguments

In the previous chapter, we clarified the various ways to pass parameters—by value, by pointer, and by reference. Now a new question arises: suppose we want to write a `print` function to print an integer, a floating-point number, and a string. These three tasks are fundamentally all "printing," but C's rule dictates that every function must have a unique name. So you end up writing `print_int`, `print_float`, `print_str`—just coming up with names is exhausting enough, and you still have to figure out which one to call each time.

C++ says: the same concept doesn't need different names. **Function overloading** allows functions with the same name to exhibit different behaviors based on their parameters, while **default arguments** make those parameters that "almost always receive the same value" completely transparent. These two features are fundamental skills for designing good interfaces, and in this chapter, we will thoroughly master them.

## Step 1 — Understanding Function Overloading

The core rule of function overloading is very simple: multiple functions can share the same name as long as their **parameter lists** differ—either in the types of the parameters or in the number of parameters. Note that the return type is not a factor—the compiler will not distinguish overloads based solely on the return type. Many beginners get confused here, thinking "returning `int` and returning `double` should surely count as different functions," but they don't, because the call site might completely ignore the return value, leaving the compiler unable to see the return type in that context.

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

When calling, the compiler automatically selects the corresponding version based on the type of the actual arguments:

```cpp
print(42);       // 调用 print(int)
print(3.14);     // 调用 print(double)
print("Hello");  // 调用 print(const char*)
```

To achieve the same effect in C, you would need three functions with three different names, and you would have to figure out which one to use every time you make a call. In contrast, the advantage of overloading at the API design level is obvious—callers only need to remember one name.

A different number of parameters can also constitute an overload. This pattern is extremely common in real-world engineering—peripheral initialization functions often need to provide both a "recommended configuration" and a "fully customizable" entry point:

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

On the surface, calling an overloaded function seems as simple as "writing a name and passing some arguments." But in reality, the compiler executes a very strict decision-making process behind the scenes—**overload resolution**. Whenever you call a function that has multiple overloaded versions, the compiler collects all candidate functions with matching names and evaluates them one by one: **which one is the "best fit"?** It's important to emphasize that the compiler doesn't understand your business semantics; it mechanically scores according to the language rules and selects the version with the highest match.

In cases not involving templates, the compiler's criteria can be understood as a "matching priority chain" from strong to weak. At the very top is **exact match**—the actual argument and formal parameter types are completely identical; if an exact match cannot be found, it considers **promotion**, such as `char` promoting to `int` or `float` promoting to `double`; further down is **standard conversion**, such as `int` converting to `double`; and only lastly does it consider user-defined type conversions. This order is critical—as long as a viable match can be found at a certain level, the rules at subsequent levels are completely ignored.

Let's demonstrate this with the most common example. Suppose we define both `void print(int)` and `void print(double)`:

```cpp
void process(int x) { /* ... */ }
void process(double x) { /* ... */ }
```

When calling `print(42)`, the literal `42` is inherently an `int`, which is an exact match for `print(int)`, whereas `print(double)` requires a conversion from `int` to `double`. An exact match has an overwhelming advantage over any form of conversion, so the final call will definitely be to `print(int)`. Conversely, the `3.14` in `print(3.14)` is a `double`, so this time the exact match occurs on `print(double)`.

A slightly more confusing situation is something like `print(3.14f)`. The type of `3.14f` is `float`, and we don't have a `print(float)` overload. At this point, the compiler compares two possible paths: `float` promoting to `double`, and `float` converting to `int`. The former is a standard promotion between floating-point types, considered more natural and safe; the latter involves truncation semantics and has a lower priority. Therefore, it will still call `print(double)`. This also illustrates a fact: **overload resolution is not "least-character-change matching," but "most-reasonable type-path matching."**

The truly headache-inducing situations usually arise when the rules cannot determine a winner. For example, if both `void foo(int, double)` and `void foo(double, int)` exist, when you call `foo(1, 2.0)`, the matching cost for both candidate functions is exactly the same—for the first version, one parameter is an exact match and the other requires a standard conversion; for the second version, the situation is exactly symmetrical. The compiler won't try to guess your intent; it will directly determine that the call is ambiguous and terminate with a compilation error.

> ⚠️ **Pitfall Warning**
> Overload ambiguity isn't always as obvious as the example above. When you define multiple overloaded versions and implicit conversion relationships exist between the parameters (such as `int` and `double`, `float` and `int`), ambiguity can pop up in unexpected places. The most reliable approach is: **when designing interfaces, avoid distinguishing overloads solely by parameter order or subtle type differences.** Once ambiguity occurs, write the types explicitly, or simply use different function names.

Behind this lies a very important design philosophy of C++: as long as there are equally viable choices that cannot be compared in terms of superiority, the compiler would rather refuse to compile than make a decision for the programmer. This is also the underlying tone of C++'s strong type system—explicitness always trumps convenience.

## Step 3 — Mastering Default Arguments

In real-world engineering, "more parameters" is not always better for a function. Often, a function's parameters will include a mix of roles: core required parameters that differ with every call; high-frequency but almost unchanging configurations that take a fixed value in the vast majority of scenarios; and advanced options that are only adjusted in very rare scenarios. If callers are forced to write out every single parameter every time, the code becomes not only verbose but also quickly obscures the truly important information.

Default arguments exist precisely to solve this problem—**for parameters where you have already decided on a "default behavior," just spare the caller the worry.**

```cpp
void configure_uart(int baudrate,
                    int databits = 8,
                    int stopbits = 1,
                    char parity = 'N')
{
    // 配置 UART
}
```

The most common calling form is reduced to just the one parameter you actually care about:

```cpp
configure_uart(115200);              // 只指定波特率，其余全部默认
configure_uart(115200, 8);           // 只改数据位
configure_uart(115200, 8, 2);        // 改数据位和停止位
configure_uart(115200, 8, 2, 'E');   // 全部自定义
```

From an interface design perspective, this is a very gentle forward-compatibility mechanism: you can continuously append new optional capabilities to the right side of a function without breaking existing code.

The syntax of default arguments seems simple, but the rules are actually very strict, and many people fall into traps.

**Rule 1: Default arguments must appear contiguously from right to left.** When processing a function call, the compiler can only determine which values use defaults by "omitting trailing parameters." You cannot skip intermediate parameters—if you want to pass a value to the third parameter, all preceding parameters must be explicitly provided. Therefore, the order of parameters in a function signature is very important: **place the parameters that most often need customization on the far left, and the parameters that almost never change on the far right.**

```cpp
// 正确：默认参数从右向左连续
void init_spi(int freq, int mode = 0, int bits = 8);

// 错误：非默认参数不能出现在默认参数后面
// void bad_init(int freq = 1000000, int mode, int bits);  // 编译错误
```

**Rule 2: Default arguments can only be specified once, and they should be placed in the declaration.** This point is especially important in projects where header files and source files are separated. The default value is part of the interface, not an implementation detail—if you write the default arguments again in the `.cpp` file, the compiler will think you are trying to redefine the rules and will directly report an error.

```cpp
// uart.h —— 声明时指定默认参数
void configure_uart(int baudrate, int databits = 8, int stopbits = 1);

// uart.cpp —— 定义时不要重复默认参数
void configure_uart(int baudrate, int databits, int stopbits)
{
    // 实现
}
```

> ⚠️ **Pitfall Warning**
> Writing default values in the declaration and then writing them again in the definition—this error is very common among beginners, and sometimes the error messages aren't very intuitive, making it quite tedious to locate. Remember: **write default arguments in the declaration, not in the definition.**

## Step 4 — Overloading vs. Default Arguments: How to Choose

Both function overloading and default arguments can make interfaces more flexible, but their applicable scenarios do not completely overlap. Which one to choose depends on the specific problem you are facing.

When you need to **handle different types of parameters**, function overloading is the only choice—default arguments cannot do this. `print(int)` and `print(const char*)` have completely different parameter types and behaviors, so this can only be implemented with overloading.

When you need to **reduce the number of parameters and provide default behavior**, default arguments are the more concise choice. `init()` and `init(baud_rate, parity, stop_bits)` do the same thing, just with different levels of detail, so using default arguments is the most natural approach.

But the situation that requires the most vigilance is **mixing the two**. If function overloading and default arguments are poorly designed, they can produce very tricky ambiguity issues. Look at this classic anti-pattern:

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

When the compiler faces `foo(42)`, it finds that both versions can match—the first is an exact match, and the second is also an exact match (just with the second parameter using a default value). The cost is exactly the same on both sides, the compiler cannot make a choice, and it directly reports an ambiguity error.

> ⚠️ **Pitfall Warning**
> Overloading and default arguments overlapping on the same interface is an almost guaranteed-to-fail combination. My advice is: for the same function name, either use only overloading (multiple versions with different parameter types) or use only default arguments (one version with some parameters having default values), but do not mix the two. If you truly need to support both "different types" and "different parameter counts" simultaneously, consider encapsulating the logic for different types into different function names—while this might not look as "elegant" as overloading, at least it won't produce ambiguity.

## Hands-On Practice — overload.cpp

Let's integrate the previous usages into a complete program, demonstrating multiple `print` overloads, the practical application of default arguments, and a deliberately created ambiguity error along with its fix:

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

Output:

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

If you define both `void foo(int)` and `void foo(int, int = 0)` from the earlier ambiguity example, and then call `foo(42)`, the compiler will directly report an error:

```text
overload.cpp:xx:xx: error: call of overloaded 'process(int)' is ambiguous
```

The solution is the approach we demonstrated—split the two versions into different function names, or remove one of the overloads and use default arguments instead (keeping only one version), so that the semantics at the call site are no longer ambiguous.

## Run Online

Run a comprehensive example of function overloading and default arguments online:

<OnlineCompilerDemo
  title="Function Overloading and Default Arguments"
  source-path="code/examples/vol1/11_overloading_default.cpp"
  description="Run online and observe the type matching of function overloading and the filling behavior of default arguments."
  allow-run
/>

## Try It Yourself

### Exercise 1: The max Overload Family

Write a set of overloaded functions `max`, accepting two `int`s, two `double`s, and two `const char*`s (compare lexicographically and return the pointer to the larger one). Call them in `main` and print the results.

```text
max_value(3, 7)         -> 7
max_value(2.5, 1.8)     -> 2.5
max_value("apple", "banana") -> banana
```

### Exercise 2: Log Function with Default Arguments

Write a `log` function with the signature `void log(const char* msg, LogLevel level = LogLevel::Info, bool timestamp = true)`. Call it with different parameter combinations and observe the behavior of the default arguments.

### Exercise 3: Compilable or Ambiguous?

Can the following code compile? If so, which `foo` will be called? Think it through before verifying on a machine:

```cpp
void func(int x) { }
void func(short x) { }

int main()
{
    func('A');  // 歧义？还是能编译？
    return 0;
}
```

Hint: The type of `1.0f` is `float`. What conversion levels do `float` → `double` and `float` → `int` belong to, respectively? Do integer promotion and integer conversion have the same priority in overload resolution?

## Summary

In this chapter, we learned about two important tools in C++ function interface design. Function overloading allows functions with the same name to exhibit different behaviors based on differences in parameter types and counts. The compiler uses a strict set of overload resolution rules to decide which version to ultimately call—exact match takes priority over promotion, promotion takes priority over standard conversion, and when two candidate functions are evenly matched, the compiler directly reports an ambiguity error. Default arguments allow callers to omit trailing parameters that "almost always have the same value," with the rule being that defaults must appear contiguously from right to left and are specified only once in the declaration. Each has its own area of expertise—overloading handles "different types," while default arguments handle "optional parameters"—but mixing them easily produces ambiguity and requires extra caution.

In the next chapter, we will look at `inline` and `constexpr` functions—when the overhead of a function call itself becomes a problem, what mechanisms does C++ provide to eliminate it?
