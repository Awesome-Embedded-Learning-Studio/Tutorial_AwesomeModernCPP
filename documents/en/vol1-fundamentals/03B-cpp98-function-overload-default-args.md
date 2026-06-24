---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Make function interfaces more flexible — function overloading allows
  same names with different parameters, default parameters reduce call overhead, and
  a guide to pitfalls and choices when both coexist
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
  source_hash: b833ba7e218d8fad18255fb88e3abe7d5fe96f9fe01c6afd186f7a8f1bce79f1
  translated_at: '2026-06-24T00:26:44.540270+00:00'
  engine: anthropic
  token_count: 2068
---
# C++98 Function Interfaces: Overloading and Default Parameters

> The complete repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to visit, and if you like it, give the project a Star to motivate the author.

In the previous post, we covered namespaces, references, and scope resolution—features that make code organization much clearer. Now, let's look at two significant improvements C++ offers at the function level: function overloading and default parameters.

Both features address the same problem—**how to design better function interfaces**. In C, if you wanted the same "concept" to support different argument types, you had to give each version a different name: `print_int()`, `print_float()`, `print_string()`... Coming up with names alone is enough to drive one crazy. Function overloading allows us to handle this with a single name. Default parameters approach this from another angle: when most arguments of a function take fixed values in the vast majority of call scenarios, why should we have to write out those "boilerplate arguments" every single time?

## 1. Function Overloading

### 1.1 Basic Concepts

Function overloading allows multiple functions to share the same name, provided their parameter lists are different. "Different parameter lists" refers to differences in the types or the number of parameters—note that **different return types do not count**; the compiler will not distinguish overloads based solely on the return type.

Let's look at the most basic example:

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

When we call the function, the compiler automatically selects the corresponding version based on the argument types:

```cpp
print(42);           // 调用 print(int)
print(3.14f);        // 调用 print(float)
print("Hello");      // 调用 print(const char*)
```

To achieve the same effect in C, we must write three separate functions—`print_int()`, `print_float()`, and `print_string()`—and manually decide which one to call each time. In contrast, the advantages of function overloading for API design are evident.

A difference in the number of parameters also constitutes overloading:

```cpp
void init_uart(int baudrate) {
    // 使用默认配置：8 数据位、1 停止位、无校验
}

void init_uart(int baudrate, int databits, int stopbits) {
    // 使用自定义配置
}
```

This pattern is very common in embedded development—peripheral initialization functions often need to provide both a "recommended configuration" and a "fully custom" entry point. Overloading makes this very natural.

### 1.2 Overload Resolution Rules

On the surface, calling an overloaded function seems as simple as "writing the name and passing the arguments." However, behind the scenes, the compiler executes a rigorous decision-making process known as **overload resolution**.

Whenever you call a function that has multiple overloaded versions, the compiler first gathers all candidate functions with matching names and argument counts. It then evaluates them one by one to answer a single question: **which one is the "best match"?** It is important to note that the compiler does not understand your business logic; it mechanically scores candidates according to language rules to select the version with the highest match.

Before we involve templates and variadic arguments, we can understand the compiler's criteria as a "matching priority chain" ranging from strong to weak. First comes **exact match**—where the argument type perfectly matches the parameter type. If no exact match exists, the compiler considers **promotion**, such as `char` to `int`. Next comes **standard conversion**, for example `int` to `double`. User-defined conversions are considered last. This order is critical because once a viable match is found at a certain level, subsequent rules are completely ignored, even if they might seem more "reasonable" to you.

Let's use a common example to demonstrate this. Suppose we define two functions, `process(int)` and `process(double)`:

```cpp
void process(int x) { }
void process(double x) { }
```

When calling `process(5)`, the compiler barely has to think: the literal `5` is an `int`, which is an exact match, whereas `process(double)` requires a conversion from `int` to `double`. Under the rules of overload resolution, an exact match overwhelmingly outweighs any form of conversion, so `process(int)` is definitely selected. Similarly, when calling `process(5.0)`, `5.0` is a `double`. This time, the exact match occurs for `process(double)`, while the other version requires a conversion that risks precision loss, so it is naturally eliminated.

Slightly more confusing is the case of `process(5.0f)`. The type of `5.0f` is `float`, and we do not have a `process(float)` overload. Here, the compiler compares two possible paths: converting `float` to `double`, and converting `float` to `int`. The former is a standard promotion between floating-point types, considered more natural and safe; the latter involves truncation semantics and thus has a lower priority. The result is that, even if you haven't explicitly written a `float` version, `process(double)` will still be called. This also illustrates a fact: **overload resolution is not about "least character matching," but "matching the most reasonable type path."**

The truly headache-inducing situations often arise when the rules cannot determine a winner. For example, if both `func(int, double)` and `func(double, int)` exist, and you call `func(5, 5)`, the matching cost for both candidate functions is exactly the same—for the first version, one argument is an exact match and the other requires a standard conversion; for the second version, the situation is symmetric. The "cost" on both sides is identical. The compiler will not try to guess your intent; instead, it will directly determine that the call is ambiguous and terminate with a compilation error.

This reflects a very important design philosophy in C++: **as long as there are equally viable choices that cannot be compared for superiority, the compiler would rather refuse to compile than make a decision for the programmer.** This is also the hallmark of C++'s strong type system—clarity always trumps convenience. From a practical standpoint, when designing interfaces, we should avoid distinguishing overloads solely by parameter order or subtle type differences, especially when involving built-in types or implicit conversions. Once ambiguity occurs, the most reliable approach is always to make the types explicit.

If we had to summarize this section in one sentence, it would be: **overload resolution is not intelligent inference, but a set of cold, rigid rules; when you feel "it should work," that is often precisely when it is most prone to errors.**

### 1.3 Practical Application of Overloading in Embedded Systems

In embedded development, the most common application scenario for function overloading is "unifying hardware operation interfaces for different data types." For example, a generic data transmission function might need to support inputs of various types:

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

The caller doesn't need to care about the specific processing `log` performs for each type—the interface is unified, yet the behavior is type-specific. In C, we would need four distinct names: `log_int()`, `log_float()`, `log_string()`, and `log_bytes()`.

However, function overloading isn't a silver bullet. It has a characteristic that can cause trouble from different perspectives: exported symbols. Since the symbol names of overloaded functions are "mangled" (name mangling is where the compiler encodes parameter type information into the final symbol name), calling C++ overloaded functions from C code, or using overloading in dynamic library interfaces, makes symbol resolution a tricky problem. The usual workaround is to add `extern "C"` before function declarations that need to be called by C code, but `extern "C"` and function overloading are mutually exclusive—because C doesn't support overloading, it naturally lacks name mangling. If your interface needs to be callable from both C and C++, overloading isn't the best fit.

## 2. Default Arguments

### 2.1 Why We Need Default Arguments

In real-world engineering, "the more parameters, the better" isn't true for functions. Often, function parameters fall into a few categories: **core required parameters**—which change with every call; **high-frequency but mostly static configuration**—which take a fixed value in the vast majority of scenarios; and **advanced options** that are rarely adjusted. If we are forced to spell out every single parameter in every call, the code becomes verbose and quickly obscures the truly important information.

Default arguments exist to solve this problem—**parameters for which you have already decided on a "default behavior" simply shouldn't be a concern for the caller**.

A very typical example in embedded development is UART configuration. The only thing that usually changes is the baud rate; data bits, stop bits, and parity bits remain almost constant across most projects. With default arguments, we can encode these "common sense" defaults directly into the interface:

```cpp
void configure_uart(int baudrate,
                   int databits = 8,
                   int stopbits = 1,
                   char parity = 'N') {
    // 配置 UART
}
```

This leaves us with the most common call form, containing only the single parameter we truly care about:

```cpp
configure_uart(115200);
```

And when we truly need to deviate from the default behavior, we can gradually "expand rightward" the parameters:

```cpp
configure_uart(115200, 8);           // 只改数据位
configure_uart(115200, 8, 2);        // 改数据位和停止位
configure_uart(115200, 8, 2, 'E');   // 全部自定义
```

From an interface design perspective, this is a very gentle approach to forward compatibility: we can continuously append new optional capabilities to the right side of the function without breaking existing code.

### 2.2 Rules for Default Parameters

The syntax for default parameters appears simple, but the rules are actually quite strict, and many developers fall into traps.

**Rule one: Default parameters must appear contiguously from right to left**. When processing a function call, the compiler can only determine which values use defaults by "omitting trailing parameters." In other words, we cannot skip intermediate parameters—if we want to pass a value to the third parameter, all preceding parameters must be provided explicitly. This also means that if we attempt to place a parameter without a default value after one that has a default value, the compiler will reject it outright.

```cpp
// 正确：默认参数从右向左连续
void init_spi(int freq, int mode = 0, int bits = 8);

// 错误：非默认参数不能出现在默认参数后面
// void bad_init(int freq = 1000000, int mode, int bits);  // 编译错误
```

Therefore, the order of parameters is critical when designing function signatures. A practical rule of thumb is: **place the parameters most frequently customized on the left, and the ones that rarely change on the right**.

**Rule Two: Default parameters can be specified only once and should be located at the declaration**. This is particularly important in projects where header files and source files are separated. The default value is part of the interface, not an implementation detail—if you write the default parameter again in the `.cpp` file, the compiler will treat it as an attempt to redefine the rule and will raise an error.

```cpp
// uart.h —— 声明时指定默认参数
void configure_uart(int baudrate, int databits = 8, int stopbits = 1);

// uart.cpp —— 定义时不要重复默认参数
void configure_uart(int baudrate, int databits, int stopbits) {
    // 实现
}
```

If someone writes this in a `.cpp` file:

```cpp
// 错误！默认参数不能同时在声明和定义中出现
void configure_uart(int baudrate, int databits = 8, int stopbits = 1) {
    // 实现
}
```

The compiler will directly throw a "redefinition of default argument" error. This is a very common pitfall for beginners—writing a default value in the declaration and then writing it again in the definition—and the error message is sometimes not so intuitive, making it quite tricky to locate.

### 2.3 Default Parameters in Embedded Systems

In embedded development, default parameters are particularly well-suited for "configuration interfaces" and "initialization functions". Peripherals like SPI, I2C, and timers usually have a "recommended configuration", and full customization is only needed in rare cases. With default parameters, the most common usage comes with almost zero overhead:

```cpp
// SPI 初始化：频率必须指定，其他参数几乎不变
void spi_init(int frequency, int mode = 0, int bit_order = 1);

// 使用
spi.init();              // 编译错误：频率是必选参数
spi.init(2000000);       // 只指定频率，其他用默认值
spi.init(2000000, 3);    // 指定频率和模式
```

The readability of this kind of interface is excellent: **the call site itself tells the story**, rather than relying on a string of mysterious magic numbers.

## 3. Overloading vs. Default Parameters: When to Use Which

Function overloading and default parameters can both make interfaces more flexible, but their use cases do not entirely overlap. The choice depends on the specific problem you are solving.

When you need to **handle arguments of different types**, function overloading is the only choice—default parameters cannot achieve this. For example, `print(int)` and `print(const char*)` have completely different parameter types and behaviors, which can only be implemented via overloading.

When you need to **reduce the number of arguments and provide default behavior**, default parameters are the more concise choice. For example, `configure_uart(115200)` and `configure_uart(115200, 8, 2, 'E')` perform the same task but with different levels of detail, so using default parameters is the most natural approach.

However, the situation requiring the most caution is **mixing the two**. If function overloading and default parameters are designed poorly, they can create very tricky ambiguity issues. Consider the following classic counter-example:

```cpp
void process(int value) {
    printf("Single: %d\n", value);
}

void process(int value, int factor = 2) {
    printf("Scaled: %d\n", value * factor);
}

process(10);  // 歧义！调用第一个？还是第二个（使用默认参数）？
```

When the compiler encounters `process(10)`, it finds that both versions are viable matches—the first is an exact match, and the second is also an exact match (with the second parameter using its default value). In this situation, the compiler cannot make a choice and reports an ambiguity error directly.

This example illustrates an important design principle: **Do not overlap function overloading and default parameters on the same interface**. If you find yourself hesitating about whether to add a default parameter to an overloaded version, it likely indicates that your interface design needs to be rethought.

My recommendation is: for the same function name, either use only overloading (multiple versions with different parameter types) or use only default parameters (one version where some parameters have default values), but do not mix the two. If you truly need to support both "different types" and "different parameter counts," consider encapsulating the logic for different types into distinct function names. While this may seem less "elegant" than overloading, it at least avoids ambiguity.

## Run Online

Comprehensive example of C++98 function overloading and default parameters:

<OnlineCompilerDemo
  title="C++98 Function Overloading and Default Parameters"
  source-path="code/examples/vol1/15_cpp98_overloading_default.cpp"
  description="Run online and observe C++98 classic patterns such as Logger class overloading and UART configuration default parameters."
  allow-run
/>

## Summary

In this chapter, we explored two important tools for C++ function interface design. Function overloading allows functions with the same name to exhibit different behaviors based on argument types and counts, with the compiler using a strict set of "overload resolution" rules to decide which version to call. Default parameters allow callers to omit trailing arguments that are "almost always the same value," making interfaces more concise and forward compatible.

Both are powerful tools for improving APIs, but they have their boundaries—overloading excels at handling "different types," while default parameters excel at handling "optional arguments." When the two conflict, prioritize keeping the interface clear rather than pursuing fancy syntactic sugar.

In the next post, we will enter the core domain of C++—classes and objects. If namespaces, references, and function overloading merely make C++ a "better C," then classes are where C++ truly undergoes a complete transformation.
