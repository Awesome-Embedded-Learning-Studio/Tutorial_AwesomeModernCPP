---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Making function interfaces more flexible — function overloading allows
  same-name functions with different parameters, default arguments reduce calling
  overhead, plus a guide to the pitfalls and choices when the two coexist.
difficulty: beginner
order: 3
platform: host
prerequisites:
- C++98入门：命名空间、引用与作用域解析
reading_time_minutes: 16
related:
- C++98面向对象：类与对象深度剖析
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: 'C++98 Function Interfaces: Overloading and Default Parameters'
translation:
  source: documents/vol1-fundamentals/03B-cpp98-function-overload-default-args.md
  source_hash: 2c0260b54e970b83ef9d58528f911cd1ffa96a33024664f46215602718d619f3
  translated_at: '2026-04-20T03:03:23.821082+00:00'
  engine: anthropic
  token_count: 2011
---
# C++98 Function Interfaces: Overloading and Default Arguments

> The complete repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to check it out, and if you like it, give it a Star to encourage the author.

In the previous chapter, we learned about namespaces, references, and scope resolution—features that make code organization much clearer. Now let's look at two important improvements C++ provides at the function level: function overloading and default arguments.

Both features solve the same problem—**how to design better function interfaces**. In C, if you wanted the same "concept" to support different parameter types, you had to give each version a different name: `print_int()`, `print_float()`, `print_string()`... Just coming up with names is enough to drive you crazy. Function overloading lets you handle this with a single name. Default arguments approach the problem from a different angle: when most parameters of a function take fixed values in the vast majority of call sites, why force the caller to spell out those "boilerplate parameters" every single time?

## 1. Function Overloading

### 1.1 Basic Concepts

Function overloading allows multiple functions to share the same name, as long as their parameter lists differ. "Differing parameter lists" means differences in the types or number of parameters—note that **different return types do not count**, as the compiler will not use the return type alone to distinguish overloads.

Here is the most basic example:

```cpp
void print(int value) {
    printf("Integer: %d\n", value);
}

void print(float value) {
    printf("Float: %f\n", value);
}

void print(const char* str) {
    printf("String: %s\n", str);
}
```

When calling the function, the compiler automatically selects the appropriate version based on the types of the arguments:

```cpp
print(42);           // 调用 print(int)
print(3.14f);        // 调用 print(float)
print("Hello");      // 调用 print(const char*)
```

In C, to achieve the same effect, you would have to write three functions with different names—`print_int()`, `print_float()`, `print_string()`—and then manually decide which one to call each time. By comparison, the advantage of function overloading in API design is obvious.

Different parameter counts can also form an overload:

```cpp
void init_uart(int baudrate) {
    // 使用默认配置：8 数据位、1 停止位、无校验
}

void init_uart(int baudrate, int databits, int stopbits) {
    // 使用自定义配置
}
```

This pattern is extremely common in embedded development—peripheral initialization functions often need to provide both a "recommended configuration" and a "fully custom" entry point, and overloading makes this very natural.

### 1.2 Overload Resolution Rules

On the surface, calling an overloaded function seems as simple as "writing the name and passing the arguments." But in reality, the compiler executes a very strict decision-making process behind the scenes—this process is called **overload resolution**.

Whenever you call a function that has multiple overloaded versions, the compiler first collects all candidate functions with matching names and matching parameter counts, then evaluates them one by one, trying to answer a single question: **which one is the "best fit"?** It is important to emphasize that the compiler does not understand your business semantics; it mechanically scores according to language rules and ultimately selects the version with the highest match.

Before involving templates and variadic arguments, the compiler's judgment criteria can be understood as a "matching priority chain" from strong to weak. First is **exact match**—the argument and parameter types are completely identical; if no exact match exists, it considers **type promotion**, such as `char` being promoted to `int`; further down is **standard type conversion**, for example `int` converting to `double`; and only lastly does it consider user-defined type conversions. This order is critical, because as long as a viable match can be found at a given level, the subsequent rules are completely ignored, even if they seem more "reasonable" to you.

Let's demonstrate with a very common example. Suppose we define both `process(int)` and `process(double)`:

```cpp
void process(int x) { }
void process(double x) { }
```

When calling `process(5)`, the compiler barely needs to think: the literal `5` is inherently `int`, which is an exact match, while `process(double)` requires a conversion from `int` to `double`. Under the rules of overload resolution, an exact match has an overwhelming advantage over any form of conversion, so the version that gets called is definitely `process(int)`. Similarly, when calling `process(5.0)`, `5.0` is `double`; this time the exact match occurs on `process(double)`, and the other version would require a conversion with precision risks, so it is naturally eliminated.

A slightly more confusing case is something like `process(5.0f)`. The type of `5.0f` is `float`, and we do not have a `process(float)` overload. At this point, the compiler compares two possible paths: `float` converting to `double`, and `float` converting to `int`. The former is a standard promotion between floating-point types, considered more natural and safe; the latter involves truncation semantics and therefore has lower priority. The result is that even if you did not explicitly write a `float` version, it will still call `process(double)`. This also illustrates a fact: **overload resolution is not "minimum character matching," but "most reasonable type path matching."**

The truly headache-inducing situations usually arise when the rules cannot determine a winner. For example, if both `func(int, double)` and `func(double, int)` overloads exist, when you call `func(5, 5)`, the matching cost for both candidate functions is exactly the same—for the first version, one parameter is an exact match and the other requires a standard conversion; for the second version, the situation is exactly symmetrical. The "cost" on both sides is identical, and the compiler will not try to guess your intent; instead, it directly determines that the call is ambiguous and terminates with a compilation error.

Behind this lies a very important design philosophy of C++: **as long as there are equally viable choices that cannot be compared in terms of superiority, the compiler would rather refuse to compile than make a decision on behalf of the programmer.** This is also the underlying tone of C++'s strong type system—clarity always trumps convenience. From a practical standpoint, when designing interfaces, we should avoid distinguishing overloads solely by parameter order or subtle type differences, especially when built-in types or implicit conversions are involved. Once ambiguity arises, the most reliable approach is always to spell out the types explicitly.

If we were to summarize this section in one sentence, it would be: **overload resolution is not intelligent inference, but a cold, rigid rule system; when you feel like "it should work," that is often exactly when it is most likely to throw an error.**

### 1.3 Practical Applications of Overloading in Embedded Systems

In embedded development, the most common use case for function overloading is "unifying hardware operation interfaces across different data types." For example, a generic data send function might need to support different input types:

```cpp
class Logger {
public:
    void log(int value) {
        printf("[INFO] %d\n", value);
    }

    void log(float value) {
        printf("[INFO] %.2f\n", value);
    }

    void log(const char* message) {
        printf("[INFO] %s\n", message);
    }

    void log(const uint8_t* data, size_t length) {
        printf("[INFO] Data (%zu bytes): ", length);
        for (size_t i = 0; i < length; ++i) {
            printf("%02X ", data[i]);
        }
        printf("\n");
    }
};

// 使用
Logger logger;
logger.log(42);                    // [INFO] 42
logger.log(25.5f);                 // [INFO] 25.50
logger.log("System started");      // [INFO] System started
uint8_t packet[] = {0x01, 0x02};
logger.log(packet, 2);             // [INFO] Data (2 bytes): 01 02
```

The caller does not need to care at all about what processing `log` does internally for each type—the interface is unified, but the behavior is type-specific. In C, this would require four different names: `log_int()`, `log_float()`, `log_string()`, and `log_bytes()`.

However, function overloading is not a silver bullet. It has a characteristic that can cause trouble from different angles—exported symbols. Because the symbol names of overloaded functions are "mangled" after compilation (name mangling: the compiler uses an encoding rule to embed parameter type information into the final symbol name), if you call a C++ overloaded function from C code, or use overloading in dynamically linked library export interfaces, symbol resolution becomes a problem that requires special handling. The usual approach is to add `extern "C"` before the declaration of functions that need to be called from C code, but `extern "C"` and function overloading are mutually exclusive—because C has no overloading, it naturally has no name mangling either. If your interface needs to be called from both C and C++, overloading is not a good fit.

## 2. Default Arguments

### 2.1 Why We Need Default Arguments

In real-world engineering, "more parameters" is not "better parameters." Often, a function's parameters will always include a mix of different roles: **core required parameters**—different on every call; **high-frequency but nearly unchanged configuration**—taking fixed values in the vast majority of scenarios; and **advanced options** that are only adjusted in rare cases. If you are forced to spell out all these parameters on every call, not only does the code become verbose, but it also quickly obscures the truly important information.

Default arguments exist precisely to solve this problem—**for parameters where you have already decided on a "default behavior," simply don't make the caller worry about them.**

A very typical example in embedded development is UART configuration. What really changes every time is often just the baud rate; as for data bits, stop bits, and parity bits, they remain almost unchanged in most projects. With default arguments, we can encode "common sense" into the interface:

```cpp
void configure_uart(int baudrate,
                   int databits = 8,
                   int stopbits = 1,
                   char parity = 'N') {
    // 配置 UART
}
```

This way, the most common call form is reduced to just the one parameter you actually care about:

```cpp
configure_uart(115200);
```

And when you truly need to deviate from the default behavior, you can gradually "unfold" parameters from right to left:

```cpp
configure_uart(115200, 8);           // 只改数据位
configure_uart(115200, 8, 2);        // 改数据位和停止位
configure_uart(115200, 8, 2, 'E');   // 全部自定义
```

From an interface design perspective, this is a very gentle form of forward compatibility: you can continuously append new optional capabilities to the right side of the function without breaking existing code.

### 2.2 Rules for Default Arguments

The syntax of default arguments seems simple, but the rules are actually very strict, and many people have fallen into these traps.

**Rule one: default arguments must appear contiguously from right to left.** When the compiler processes a function call, it can only determine which values use defaults by "omitting trailing parameters." In other words, you cannot skip intermediate parameters—if you want to pass a value to the third parameter, all preceding parameters must be explicitly given. This also means that if you try to place a parameter without a default value after one that has a default value, the compiler will reject it outright.

```cpp
// 正确：默认参数从右向左连续
void init_spi(int freq, int mode = 0, int bits = 8);

// 错误：非默认参数不能出现在默认参数后面
// void bad_init(int freq = 1000000, int mode, int bits);  // 编译错误
```

Therefore, when designing function signatures, the order of parameters is very important. A practical principle is: **put the parameters that most often need customization on the left, and put the parameters that almost never change on the right.**

**Rule two: default arguments can only be specified once, and should be placed in the declaration.** This point is especially important in projects where header files and source files are separated. Default values are part of the interface, not implementation details—if you write the default arguments again in the `.cpp`, the compiler will think you are trying to redefine the rules and will directly report an error.

```cpp
// uart.h —— 声明时指定默认参数
void configure_uart(int baudrate, int databits = 8, int stopbits = 1);

// uart.cpp —— 定义时不要重复默认参数
void configure_uart(int baudrate, int databits, int stopbits) {
    // 实现
}
```

If someone writes this in the `.cpp` file:

```cpp
// 错误！默认参数不能同时在声明和定义中出现
void configure_uart(int baudrate, int databits = 8, int stopbits = 1) {
    // 实现
}
```

The compiler will immediately give you a redefinition of default arguments error. This trap is very common among beginners—"writing default values in the declaration and then writing them again in the definition"—and the error messages are sometimes not very intuitive, making it quite tedious to track down.

### 2.3 Applications of Default Arguments in Embedded Systems

In embedded development, default arguments are particularly well-suited for "configuration-type interfaces" and "initialization functions." Peripherals like SPI, I2C, and timers often have a set of "recommended configurations," and only in rare cases do they need to be fully customized. Through default arguments, the most common usage becomes almost zero-burden:

```cpp
// SPI 初始化：频率必须指定，其他参数几乎不变
void spi_init(int frequency, int mode = 0, int bit_order = 1);

// 使用
spi.init();              // 编译错误：频率是必选参数
spi.init(2000000);       // 只指定频率，其他用默认值
spi.init(2000000, 3);    // 指定频率和模式
```

The readability of this kind of interface is very strong: **the call site itself is already "telling a story,"** rather than being a string of mysterious magic numbers.

## 3. Overloading vs. Default Arguments: When to Use Which

Both function overloading and default arguments can make interfaces more flexible, but their applicable scenarios do not completely overlap. The choice of which to use depends on the specific problem you are facing.

When you need to **handle different parameter types**, function overloading is the only choice—default arguments cannot do this. For example, `print(int)` and `print(const char*)` have completely different parameter types and different behaviors; this can only be implemented with overloading.

When you need to **reduce the number of parameters and provide default behavior**, default arguments are the more concise choice. For example, `configure_uart(115200)` and `configure_uart(115200, 8, 2, 'E')` do the same thing, just with different levels of detail, and using default arguments is the most natural approach.

But the situation that requires the most vigilance is **mixing the two**. If function overloading and default arguments are poorly designed, they can produce very tricky ambiguity issues. Look at this classic anti-pattern:

```cpp
void process(int value) {
    printf("Single: %d\n", value);
}

void process(int value, int factor = 2) {
    printf("Scaled: %d\n", value * factor);
}

process(10);  // 歧义！调用第一个？还是第二个（使用默认参数）？
```

When the compiler faces `process(10)`, it finds that both versions can match—the first is an exact match, and the second is also an exact match (just with the second parameter using a default value). In this situation, the compiler cannot make a choice and directly reports an ambiguity error.

This example illustrates an important design principle: **do not let overloading and default arguments overlap on the same interface.** If you find yourself hesitating over "should I add a default argument to this overloaded version," it likely means your interface design needs to be rethought.

My recommendation is: for the same function name, either use only overloading (multiple versions with different parameter types) or use only default arguments (one version with some parameters having default values), but do not mix the two. If you truly need to support both "different types" and "different parameter counts" simultaneously, consider encapsulating the different type handling logic into different function names—while this may not look as "elegant" as overloading, at least it will not produce ambiguity.

## Summary

In this chapter, we learned about two important tools in C++ function interface design. Function overloading allows functions with the same name to exhibit different behaviors based on differences in parameter types and counts, and the compiler uses a strict set of "overload resolution" rules to decide which version to ultimately call. Default arguments allow callers to omit trailing parameters that are "almost always the same value," making interfaces more concise and more forward-compatible.

Both are powerful tools for making APIs easier to use, but they also have their own boundaries—overloading excels at handling "different types," while default arguments excel at handling "optional parameters." When the two conflict, prioritize keeping the interface clear rather than pursuing flashy syntactic sugar.

In the next chapter, we will enter the core domain of C++—classes and objects. If namespaces, references, and function overloading only make C++ a "better C," then classes are where C++ truly undergoes a fundamental transformation.
