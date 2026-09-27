---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the rules of function overloading and the use of default parameters,
  understand the overload resolution mechanism, and avoid the common conflicts between
  the two.
difficulty: beginner
order: 3
platform: host
prerequisites:
- Parameter Passing
reading_time_minutes: 20
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Overloading and Default Parameters
translation:
  source: documents/vol1-fundamentals/ch03/03-overloading-default.md
  source_hash: bf916b462b1dbc6481b0ad8276693ed87dd5bd7224cfae3f0e1b469a4e22c060
  translated_at: '2026-09-25T10:24:51+00:00'
  engine: anthropic
  token_count: 3500
---
# Overloading and Default Parameters: One Name, Many Roles

Pass by value, pass by reference, pass by const reference... Huh? You forgot already? Quick, flip back to the previous chapter! I'll be right here waiting.

All right—since you've kept reading onward, I'll take that as proof you really are ready. Good. Let's go.

Suppose we want to write a `print` function that prints integers, floating-point numbers, and strings. All three tasks are essentially "printing", but C's rule is that every function must have a unique name. So you end up writing `print_int()`, `print_float()`, and `print_string()`—coming up with the names alone is maddening enough, and at every call site you still have to figure out which one to use.

C++ says: the same concept doesn't need different names. **Function overloading** lets functions sharing one name behave differently depending on their parameters, while **default parameters** spare us from writing out yet again those arguments that "almost always carry the same value". These two features are fundamental skills for designing good interfaces.

## Function Overloading: Same Name, Different Parameters

The core rule of function overloading is remarkably simple—one sentence is all we need to memorize: multiple functions can share the same name as long as their **parameter lists** differ, where "differ" means either **different parameter types** or **a different number of parameters**. Note that the return type is not part of the consideration: the compiler will never distinguish overloads by return type alone. Many beginners get this wrong, thinking "returning `int` and returning `double` should surely count as different functions, right?"—no, it doesn't, because a call site may completely ignore the return value, and in that context the compiler never sees the return type at all.

Here is the most basic example:

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

When we call them, the compiler automatically picks the matching version based on the type of each argument:

```cpp
print(42);       // calls print(int)
print(3.14);     // calls print(double)
print("Hello");  // calls print(const char*)
```

To achieve the same effect in C, you need three functions with three names, and at every call you have to decide which one to use. By contrast, the API-design advantage of overloading is plain to see: one name is all we need to remember.

A different number of parameters also constitutes overloading. You will see this pattern all the time in real projects: peripheral initialization functions usually need to offer two entry points—a "recommended configuration" and a "fully custom" one.

```cpp
void init_uart(int baudrate)
{
    // Use the default configuration: 8 data bits, 1 stop bit, no parity
}

void init_uart(int baudrate, int databits, int stopbits, char parity)
{
    // Use a custom configuration
}
```

## Overload Resolution: How the Compiler Picks a Version

On the surface, calling an overloaded function looks like the simplest thing in the world—write a name, pass some arguments. Behind the scenes, though, the compiler runs a very strict decision procedure: **overload resolution**. Whenever you call a function that has multiple overloaded versions, the compiler gathers every candidate whose name matches, then evaluates them one by one: **which one is the "best fit"**? Note that the compiler does not understand your business semantics; it just mechanically scores candidates against the language rules and selects the best-matching version.

As long as templates aren't involved, we can think of the compiler's criteria as a "matching priority chain" running from strongest to weakest. At the top sits the **exact match**: the argument type and the parameter type are identical. Only if no exact match can be found does it consider **promotions**, such as `char` promoted to `int` or `float` promoted to `double`. After that come **standard conversions**, for example `int` converted to `double`; user-defined conversions come last. This ordering matters enormously: once a viable match is found at some level, the rules below that level are never consulted at all.

Let's demonstrate with the most common example. Suppose both `process(int)` and `process(double)` are defined:

```cpp
void process(int x) { /* ... */ }
void process(double x) { /* ... */ }
```

When we call `process(5)`, the literal `5` is itself an `int`—an exact match for `process(int)`—whereas `process(double)` would require a conversion from `int` to `double`. An exact match overwhelms any form of conversion, so the call is guaranteed to resolve to `process(int)`. Conversely, in `process(5.0)` the `5.0` is a `double`, so this time the exact match happens on `process(double)`.

The slightly confusing case is `process(5.0f)`. The type of `5.0f` is `float`, and we have no `process(float)` overload. The compiler then compares two possible paths: `float` promoted to `double`, and `float` converted to `int`. The former is a standard promotion between floating-point types, considered more natural and safer; the latter involves truncation semantics and ranks lower. So the call still lands on `process(double)`. **Overload resolution cares about how sound the type path is—promotion beats truncation**.

The truly headache-inducing cases arise when the rules cannot break the tie. Suppose both `func(int, double)` and `func(double, int)` exist. When we call `func(5, 5)`, the two candidates cost exactly the same: for the first version, one argument is an exact match and the other needs a standard conversion; for the second version, the situation is perfectly symmetric. **The compiler does not try to guess our intent—it declares the call ambiguous and terminates with a compile error.**

Overload ambiguity is not always as obvious as in the example above. When several overloaded versions are defined and implicit conversions exist between the parameter types (say `int` and `long`, or `float` and `double`), ambiguity can pop up where you least expect it. The most dependable practice is: **when designing an interface, avoid distinguishing overloads solely by parameter order or subtle type differences**. If ambiguity does appear, spell out the types, or simply use different function names.

Behind this lies a very important C++ design philosophy: whenever several equally viable options exist whose merits cannot be compared, the compiler would rather refuse to compile than make the decision for the programmer. This is the consistent attitude of C++'s strong type system, and we will meet it again when we study type conversions and templates later on: explicitness always outranks convenience.

## Default Parameters: Less for the Caller to Worry About

In real projects, function parameters are not "the more the better". More often than not, a function's parameters mix several roles: core required parameters that differ on every call; high-frequency yet nearly constant configuration that takes a fixed value in the vast majority of scenarios; and advanced options that only a handful of scenarios ever tweak. If every call were forced to spell out every single parameter, the code would bloat and the information that actually matters would drown in it.

Default parameters exist precisely to solve this problem—**for parameters whose "default behavior" you have already decided on, just spare the caller the worry**.

```cpp
void configure_uart(int baudrate,
                    int databits = 8,
                    int stopbits = 1,
                    char parity = 'N')
{
    // Configure the UART
}
```

In the most common call form, only the one parameter we actually care about remains:

```cpp
configure_uart(115200);              // Only specify the baud rate; everything else defaults
configure_uart(115200, 8);           // Only change the data bits
configure_uart(115200, 8, 2);        // Change data bits and stop bits
configure_uart(115200, 8, 2, 'E');   // Fully custom
```

From an interface-design perspective, this is a very gentle form of forward compatibility: we can keep appending new optional capabilities on the right side of the function without breaking existing code.

The syntax of default parameters looks simple, but the rules are actually very strict, and there are plenty of pitfalls for us to step into.

**Parameters with default values can only appear consecutively at the end of the parameter list.** Read the parameter list from left to right: once any parameter carries a default value, every parameter to its right must carry one too—no gaps allowed. The reason lies in the call syntax: arguments are slotted into parameters from left to right, and the compiler can only decide which values take their defaults by "omitting trailing arguments". So we cannot skip a middle parameter: to pass a value for the third parameter, all preceding parameters must be given explicitly. Applied to interface design, parameter ordering becomes important: **put the parameters you most often need to customize on the far left, and the ones that almost never change on the far right**.

```cpp
// Correct: default parameters appear consecutively at the end of the parameter list
void init_spi(int freq, int mode = 0, int bits = 8);

// Wrong: a non-default parameter cannot appear after a default parameter
// void bad_init(int freq = 1000000, int mode, int bits);  // compile error
```

**A default parameter may be specified only once, and that specification belongs in the declaration.** This point matters especially in projects that separate headers from source files. The default value is part of the interface; if we write the default arguments yet again in the `.cpp`, the compiler treats it as redefining the rules and reports an error outright.

```cpp
// uart.h — specify the default parameters in the declaration
void configure_uart(int baudrate, int databits = 8, int stopbits = 1);

// uart.cpp — do not repeat the default parameters in the definition
void configure_uart(int baudrate, int databits, int stopbits)
{
    // Implementation
}
```

Writing the defaults in the declaration and then again in the definition—this mistake is extremely common among beginners, and the diagnostics are sometimes not all that straightforward, making it a pain to track down. Remember: **default parameters go in the declaration, never in the definition**.

## Overloading or Default Parameters: How to Choose

Both function overloading and default parameters make interfaces more flexible, but their applicable scenarios do not fully overlap. Which one to use depends on the concrete problem in front of us.

When we need to **handle parameters of different types**, function overloading is the only choice—default parameters cannot do this. `print(int)` and `print(const char*)` take completely different parameter types and behave differently; only overloading can express that.

If the requirement is to **reduce the number of parameters and provide default behavior**, default parameters are the cleaner choice. `configure_uart(115200)` and `configure_uart(115200, 8, 2, 'E')` do the same thing, just at different levels of detail—default parameters are the most natural fit here.

But the situation to stay most alert about is **mixing the two**. Function overloading and default parameters, when poorly designed together, produce genuinely nasty ambiguity problems. Take this classic counterexample:

```cpp
void process(int value)
{
    std::printf("Single: %d\n", value);
}

void process(int value, int factor = 2)
{
    std::printf("Scaled: %d\n", value * factor);
}

process(10);  // Ambiguous! Call the first? Or the second (using its default parameter)?
```

Look at the compiler's predicament when facing `process(10)`: both versions match. The first is an exact match; so is the second (its second argument just takes the default value). Both sides cost exactly the same, the compiler cannot choose, and it reports an ambiguity error outright.

Overlapping overloads and default parameters on the same interface is a combination that is almost guaranteed to backfire. My advice is: for a given function name, use either overloading alone (multiple versions with different parameter types) or default parameters alone (a single version where some parameters have defaults)—never mix the two. If you genuinely need to support both "different types" and "different parameter counts" at once, consider packaging the different-type handling logic under different function names—it looks less "elegant" than overloading, sure, but at least it never becomes ambiguous.

## Hands-On Practice — overload.cpp

Let's consolidate the earlier usage into one complete program, demonstrating multiple `print` overloads, practical use of default parameters, plus a deliberately created ambiguity error and how to fix it:

```cpp
// overload.cpp
// Platform: host
// Standard: C++17

#include <cstdint>
#include <cstdio>
#include <cstring>

// ---- Multiple print overloads ----

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

// ---- Default parameter example ----

void draw_rect(int width, int height, bool fill = false,
               char brush = '#')
{
    std::printf("绘制矩形 %dx%d, fill=%s, brush='%c'\n",
                width, height,
                fill ? "true" : "false",
                brush);
}

// ---- Fixing the ambiguity: different function names instead of mixing ----

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
    // Demonstrate overloading
    std::printf("=== 函数重载 ===\n");
    print(42);
    print(3.14159);
    print("Hello, overloading!");

    // Demonstrate default parameters
    std::printf("\n=== 默认参数 ===\n");
    draw_rect(10, 5);                  // fill=false, brush='#'
    draw_rect(10, 5, true);            // fill=true,  brush='#'
    draw_rect(10, 5, true, '*');       // Fully custom

    // Demonstrate the fixed "overloading + different parameter counts"
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

If you define both `process(int)` and `process(int, int = 2)` from the earlier ambiguity example and then call `process(10)`, the compiler will report an error outright:

```text
overload.cpp:xx:xx: error: call of overloaded 'process(int)' is ambiguous
```

The fix is exactly what we demonstrated above: split the two versions into different function names, or drop one of the overloads and switch to default parameters (keeping only one version), so that the semantics at the call site are no longer murky.

## Run It Online

You can also run the combined example of function overloading and default parameters online:

<OnlineCompilerDemo
  title="Function Overloading and Default Parameters"
  source-path="code/examples/vol1/11_overloading_default.cpp"
  description="Run it online and observe how overload resolution matches types and how default parameters get filled in."
  allow-run
/>

## Try It Yourself

### Exercise 1: An Overloaded max Family

Write a set of overloaded functions named `max_value`, taking two `int`s, two `double`s, and two `const char*` respectively (compare lexicographically and return the larger pointer). Call each of them from `main` and print the results.

```text
max_value(3, 7)          -> 7
max_value(2.5, 1.8)      -> 2.5
max_value(apple, banana) -> banana
```

::: details Reference solution

```cpp
#include <iostream>
#include <cstring>
void max_value(int a, int b);
void max_value(double a, double b);
void max_value(const char *a, const char *b);
int main()
{
    max_value(3, 7);
    max_value(2.5, 1.8);
    max_value("apple", "banana");
    return 0;
}
void max_value(int a, int b)
{
    std::cout << "max_value(" << a << ", " << b << ") -> " << (a > b ? a : b) << std::endl;
}
void max_value(double a, double b)
{
    std::cout << "max_value(" << a << ", " << b << ") -> " << (a > b ? a : b) << std::endl;
}
void max_value(const char *a, const char *b)
{
    std::cout << "max_value(" << a << ", " << b << ") -> " << (strcmp(a, b) > 0 ? a : b) << std::endl;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
max_value(3, 7) -> 7
max_value(2.5, 1.8) -> 2.5
max_value(apple, banana) -> banana
```

:::

### Exercise 2: A Logging Function with Default Parameters

Write a `log_message` function with the signature `void log_message(const char* text, const char* level = "INFO", bool show_timestamp = false)`. Call it with different argument combinations and observe how the default parameters behave.

::: details Reference solution

```cpp
#include <chrono>
#include <iostream>

void log_message(const char *text, const char *level = "INFO", bool show_timestamp = false);

int main()
{
    // Demonstrate default parameters with different argument combinations.
    log_message("应用程序已启动");
    log_message("配置已加载", "DEBUG");
    log_message("计划任务正在运行", "INFO", true);
    log_message("无法打开数据文件", "ERROR", true);

    return 0;
}

void log_message(const char *text, const char *level, bool show_timestamp)
{
    if (show_timestamp)
    {
        // system_clock represents wall-clock time, suitable for log timestamps.
        const auto now = std::chrono::system_clock::now();
        const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   now.time_since_epoch())
                                   .count();
        std::cout << "[" << timestamp << "] ";
    }

    std::cout << "[" << level << "] " << text << std::endl;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output (the timestamp values depend on the exact moment we run it):

```text
[INFO] 应用程序已启动
[DEBUG] 配置已加载
[timestamp-1] [INFO] 计划任务正在运行
[timestamp-2] [ERROR] 无法打开数据文件
```

Here `timestamp-1` and `timestamp-2` stand for the millisecond timestamps obtained by the two calls respectively. If the two calls straddle a millisecond boundary, the values will differ; even if they are identical, that only means the two calls happened within the same millisecond—it does not mean a timestamp got reused.

:::

### Exercise 3: Does It Compile, or Is It Ambiguous

Does the following code compile? If so, which `func` gets called? Think it through first, then verify on your machine:

```cpp
void func(int x) { }
void func(short x) { }

int main()
{
    func('A');  // Ambiguous? Or does it compile?
    return 0;
}
```

Hint: the type of `'A'` is `char`. Think about it: what conversion rank do `char` → `int` and `char` → `short` each belong to? Do integral promotion and integral conversion carry the same priority in overload resolution?

::: details Reference solution

```cpp
#include <iostream>
void func(int x);
void func(short x);

int main()
{
    func('A'); // Ambiguous? Or does it compile?
    return 0;
}
void func(int x)
{
    std::cout << "func(int): " << x << std::endl;
}
void func(short x)
{
    std::cout << "func(short): " << x << std::endl;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
func(int): 65
```

Here `char` → `int` is an integral promotion, while `char` → `short` is an integral conversion; integral promotion takes precedence over integral conversion, so what we see is `func(int)`.

:::
