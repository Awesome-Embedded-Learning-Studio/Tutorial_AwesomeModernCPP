---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Making function interfaces more flexible — function overloading allows
  functions with the same name but different parameters, default parameters reduce
  the calling burden, and a guide to pitfalls and choices when both coexist
difficulty: beginner
order: 3
platform: host
prerequisites:
- C++98入门：命名空间、引用与作用域解析
reading_time_minutes: 14
related:
- C++98面向对象：类与对象深度剖析
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: 'C++98 Function Interfaces: Overloading and Default Arguments'
translation:
  source: documents/vol1-fundamentals/03B-cpp98-function-overload-default-args.md
  source_hash: e7f2d1bcdb15cf0cb880f98cca1823730246313c2f43515914d654b8bcb06bbb
  translated_at: '2026-06-16T03:31:29.468898+00:00'
  engine: anthropic
  token_count: 2068
---
# C++98 Function Interfaces: Overloading and Default Arguments

> The full repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to visit, and if you like it, give the project a Star to motivate the author.

In the previous post, we covered namespaces, references, and scope resolution—features that make code organization much clearer. Now, let's look at two significant improvements C++ offers at the function level: function overloading and default arguments.

Both features solve the same problem—**how to design better function interfaces**. In C, if you wanted the same "concept" to support different argument types, you had to give each version a different name: `abs_int`, `abs_long`, `abs_double`... Naming alone is enough to drive you crazy. Function overloading allows you to handle this with a single name. Default arguments approach the issue from another angle: if most arguments for a function take fixed values in the vast majority of call scenarios, why force the caller to write out those "boilerplate arguments" every single time?

## 1. Function Overloading

### 1.1 Basic Concepts

Function overloading allows multiple functions to share the same name, provided their parameter lists are different. "Different parameter lists" means differences in the type or number of parameters—note that **different return types do not count**, as the compiler cannot distinguish overloads based solely on the return type.

Let's look at the most basic example:

```cpp
void print(int i) {
    // ...
}

void print(double f) {
    // ...
}

void print(const char* str) {
    // ...
}
```

When calling these functions, the compiler automatically selects the corresponding version based on the types of the arguments passed:

```cpp
print(42);     // Calls print(int)
print(3.14);   // Calls print(double)
print("hi");   // Calls print(const char*)
```

In C, to achieve the same effect, you would have to write three functions with different names—`print_int`, `print_double`, `print_string`—and then manually decide which one to use every time you called them. By comparison, the advantage of function overloading in API design is obvious.

Different parameter counts can also constitute an overload:

```cpp
void init_spi(); // Use default settings
void init_spi(int prescaler, bool msb_first); // Fully custom
```

This pattern is very common in embedded development—peripheral initialization functions often need to provide both a "recommended configuration" entry point and a "fully customizable" one. Overloading makes this feel very natural.

### 1.2 Overload Resolution Rules

On the surface, calling an overloaded function seems as simple as "writing the name and passing arguments." But in reality, the compiler executes a very strict decision-making process behind the scenes—this process is called **Overload Resolution**.

Whenever you call a function that has multiple overloaded versions, the compiler first collects all candidate functions with matching names and consistent argument counts. It then evaluates them one by one, trying to answer a question: **which one is the "best fit"?** It is important to emphasize that the compiler does not understand your business semantics; it mechanically scores them according to language rules and finally selects the version with the highest match.

Before involving templates and variadic arguments, the compiler's judgment criteria can be understood as a "matching priority chain" from strong to weak. First is **Exact Match**—the type of the argument is exactly the same as the parameter; if no exact match exists, it considers **Promotion**, such as `float` being promoted to `double`; next comes **Standard Conversion**, for example, `int` converting to `long`; finally, user-defined type conversions are considered. This order is critical because once a feasible match is found at a certain level, the subsequent rules are completely ignored, even if they seem more "reasonable" to you.

Let's use a common example to demonstrate. Suppose we define both `void func(int)` and `void func(double)`:

```cpp
void func(int i) {
    // ...
}

void func(double d) {
    // ...
}

func(10);    // Calls func(int)
func(10.0);  // Calls func(double)
func(10.5f); // Calls func(double)
```

When calling `func(10)`, the compiler hardly needs to think: the literal `10` is an `int`, which belongs to exact match, while `func(double)` requires a conversion from `int` to `double`. Under overload resolution rules, exact match has an overwhelming advantage over any form of conversion, so `func(int)` is ultimately called. Similarly, when calling `func(10.0)`, `10.0` is a `double`. This time, the exact match occurs on `func(double)`, and the other version requires a conversion with precision risks, so it is naturally eliminated.

Slightly more confusing is the `func(10.5f)` case. The type of `10.5f` is `float`, and we do not have a `float` overload. At this point, the compiler compares two possible paths: `float` converting to `int`, and `float` converting to `double`. The former is a standard promotion between floating-point types, considered more natural and safe; the latter involves truncation semantics and therefore has lower priority. The result is that even if you didn't explicitly write a `float` version, it will still call `func(double)`. This also reflects a fact: **overload resolution is not "least character matching," but "most reasonable type path matching."**

The real headache arises when the rules cannot determine a winner. For example, if both `void func(long, int)` and `void func(int, long)` exist, when you call `func(10, 10)`, the matching cost for both candidate functions is exactly the same—for the first version, one argument is an exact match and the other requires a standard conversion; for the second version, the situation is symmetric. The "cost" on both sides is identical. The compiler will not try to guess your intent; instead, it will directly determine that the call is ambiguous and terminate with a compilation error.

This reflects a very important design philosophy in C++: **as long as there are equally feasible choices that cannot be compared for superiority, the compiler would rather refuse to compile than make a decision for the programmer**. This is also the underlying tone of C++'s strong type system—clarity always trumps convenience. From a practical standpoint, when designing interfaces, we should try to avoid distinguishing overloads solely by parameter order or subtle type differences, especially when involving built-in types or implicit conversions. Once ambiguity occurs, the most reliable approach is always to make the types explicit.

To summarize this section in one sentence: **overload resolution is not intelligent inference, but a set of cold, rigid rule systems; when you think "it should work," it is often when it is most prone to errors.**

### 1.3 Practical Applications of Overloading in Embedded Systems

In embedded development, the most common application scenario for function overloading is to "unify hardware operation interfaces for different data types." For example, a generic data sending function might need to support different input types:

```cpp
void send_data(uint8_t data);
void send_data(uint16_t data);
void send_data(uint32_t data);
void send_data(const uint8_t* buf, size_t len);
```

The caller doesn't need to care at all what `send_data` internally does for each type—the interface is unified, but the behavior is specific to the type. In C, this would require four different names: `send_data8`, `send_data16`, `send_data32`, `send_buffer`.

However, function overloading is not a panacea. It has a feature that can cause trouble from different perspectives—exported symbols. Because overloaded functions have their symbol names "mangled" after compilation (name mangling, where the compiler uses an encoding rule to embed parameter type information into the final symbol name), if you call a C++ overloaded function in C code, or use overloading in dynamically exported library interfaces, symbol resolution becomes a problem that needs special handling. The usual approach is to add `extern "C"` before the function declaration that needs to be called by C code, but `extern "C"` and function overloading are mutually exclusive—because C has no overloading, and thus no name mangling. If your interface needs to be called by both C and C++, overloading is not very suitable.

## 2. Default Arguments

### 2.1 Why We Need Default Arguments

In real-world engineering, "the more parameters, the better" is not true for functions. Often, a function's parameters will always mix a few roles: **Core Required Parameters**—different every call; **High-Frequency but Almost Constant Configuration**—fixed values in the vast majority of scenarios; and **Advanced Options Adjusted Only in Rare Cases**. If forced to write out these parameters every time, the code is not only verbose but also quickly obscures the truly important information.

Default arguments exist precisely to solve this problem—**for those parameters where you have already decided on "default behavior," just don't let the caller worry about them**.

A very typical example in embedded development is UART configuration. What really changes every time is often just the baud rate; as for data bits, stop bits, and parity bits, they are almost constant in most projects. With default arguments, we can encode "common sense" into the interface:

```cpp
void uart_init(uint32_t baudrate,
               uint8_t data_bits = 8,
               uint8_t stop_bits = 1,
               uint8_t parity = 0); // 0: None, 1: Odd, 2: Even
```

This way, the most common call form leaves only the one parameter you truly care about:

```cpp
uart_init(115200); // Uses 8N1 by default
```

And when you really need to deviate from the default behavior, you can gradually "expand" the parameters to the right:

```cpp
uart_init(115200, 7);       // 7 data bits, 1 stop bit, no parity
uart_init(115200, 8, 2);    // 8 data bits, 2 stop bits, no parity
uart_init(115200, 8, 1, 2); // 8 data bits, 1 stop bit, even parity
```

From an interface design perspective, this is a very gentle means of forward compatibility: you can continuously append new optional capabilities to the right side of the function without breaking existing code.

### 2.2 Rules for Default Arguments

The syntax of default arguments looks simple, but the rules are actually very strict, and many people fall into traps.

**Rule One: Default arguments must appear continuously from right to left.** When processing a function call, the compiler can only determine which values use defaults by "omitting trailing arguments." In other words, you cannot skip intermediate parameters—if you want to pass a value to the third parameter, all preceding parameters must be explicitly given. This also means that if you try to place a parameter without a default value after a parameter that has one, the compiler will refuse directly.

```cpp
// Error: 'stop_bits' has a default, but 'data_bits' (to its left) does not
void uart_init(uint32_t baudrate,
               uint8_t stop_bits = 1,
               uint8_t data_bits); // ❌ Compile error!
```

Therefore, when designing function signatures, the order of parameters is very important. A practical principle is: **put the parameters most often needing customization on the far left, and the parameters that almost never change on the far right**.

**Rule Two: Default arguments can only be specified once, and should be in the declaration.** This is particularly important in projects where header files and source files are separated. The default value is part of the interface, not an implementation detail—if you write default arguments again in the `.cpp` file, the compiler will think you are trying to redefine the rules and will report an error directly.

```cpp
// header.h
void uart_init(uint32_t baudrate, uint8_t data_bits = 8);

// source.cpp
void uart_init(uint32_t baudrate, uint8_t data_bits = 8) { // ❌ Error: redefinition of default parameter
    // ...
}
```

If someone writes this in the `.cpp` file:

```cpp
// source.cpp
void uart_init(uint32_t baudrate, uint8_t data_bits = 8) {
    // ...
}
```

The compiler will directly give you a "redefinition of default parameter" error. This pitfall is very common among beginners—"wrote default values in the declaration, then wrote them again in the definition"—and the error message is sometimes not so intuitive, making it quite tricky to locate.

### 2.3 Applications of Default Arguments in Embedded Systems

In embedded development, default arguments are particularly suitable for "configuration interfaces" and "initialization functions." Peripherals like SPI, I2C, and timers often have a "recommended configuration," and only in rare cases is full customization needed. Through default arguments, the most common usage is almost zero burden:

```cpp
timer_init(TIMER2, 1000);               // 1kHz interrupt, default resolution
timer_init(TIMER2, 1000, TIMER_MICROS); // Microsecond resolution
```

The readability of such interfaces is very strong: **the call site itself is already "telling a story,"** rather than a string of mysterious magic numbers.

## 3. Overloading vs Default Arguments: When to Use Which

Both function overloading and default arguments can make interfaces more flexible, but their applicable scenarios do not completely overlap. The choice depends on the specific problem you face.

When you need to **handle different parameter types**, function overloading is the only choice—default arguments cannot do this. For example, `send_data(int)` and `send_data(double)`, their parameter types are completely different, and their behaviors are different; this can only be achieved by overloading.

When you need to **reduce the number of parameters and provide default behavior**, default arguments are the more concise choice. For example, `init_spi()` and `init_spi(int, bool)`, they do the same thing, just with different levels of detail; using default arguments is most natural.

But the situation that requires the most vigilance is **mixing the two**. If function overloading and default arguments are designed poorly, they can produce very tricky ambiguity problems. Look at this classic negative example:

```cpp
void func(int a);
void func(int a, int b = 10);

func(10); // ❌ Ambiguous! Which one to call?
```

When the compiler faces `func(10)`, it finds that both versions can match—the first is an exact match, and the second is also an exact match (just the second parameter uses a default value). In this case, the compiler cannot make a choice and directly reports an ambiguity error.

This example illustrates an important design principle: **do not overlap overloading and default arguments on the same interface**. If you find yourself hesitating on "should I add a default parameter to this overload version," it likely means your interface design needs rethinking.

The author's suggestion is: for the same function name, either use only overloading (multiple versions with different parameter types) or use only default arguments (one version with some parameters having default values), but do not mix the two. If you really need to support both "different types" and "different parameter counts," consider encapsulating the logic for different types into different function names—although this looks less "elegant" than overloading, it at least won't produce ambiguity.

## Run Online

Run the comprehensive example of C++98 function overloading and default arguments online:

<OnlineCompilerDemo
  title="C++98 Function Overloading and Default Arguments"
  source-path="code/examples/vol1/15_cpp98_overloading_default.cpp"
  description="Run online and observe C++98 classic patterns such as Logger class overloading and UART configuration default arguments."
  allow-run
/>

## Summary

In this chapter, we learned two important tools in C++ function interface design. Function overloading allows functions with the same name to exhibit different behaviors based on differences in parameter types and counts. The compiler decides which version to ultimately call through a strict set of "overload resolution" rules. Default arguments allow callers to omit those "almost always the same value" trailing parameters, making interfaces more concise and more forward compatible.

Both are powerful tools for making APIs better, but they have their boundaries—overloading is good at handling "different types," while default arguments are good at handling "optional parameters." When the two conflict, prioritize keeping the interface clear rather than pursuing fancy syntactic sugar.

In the next post, we will enter the core domain of C++—classes and objects. If namespaces, references, and function overloading just make C++ a "better C," then classes are where C++ truly undergoes a metamorphosis.
