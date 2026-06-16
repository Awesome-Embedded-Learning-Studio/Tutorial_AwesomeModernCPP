---
chapter: 11
cpp_standard:
- 11
- 14
- 17
description: operator"" Raw/Cooked Forms and Standard Library Literals
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 2: constexpr 基础'
reading_time_minutes: 10
related:
- UDL 实战
tags:
- host
- cpp-modern
- intermediate
- 字面量
title: User-Defined Literal Fundamentals
translation:
  source: documents/vol2-modern-features/ch11-user-defined-literals/01-udl-basics.md
  source_hash: 399538f5c720dfe279d1838408a4792d62a187811cca2e39f1e1b1e5ee5636d6
  translated_at: '2026-06-16T03:59:47.425093+00:00'
  engine: anthropic
  token_count: 2450
---
# Basics of User-Defined Literals

When writing embedded code, we often encounter frustrating scenarios: Is the `1000` in `delay(1000)` milliseconds or microseconds? Is `9600` or `115200` the correct baud rate? Is `1024` bytes or words? These "magic numbers" are not only hard to understand but also error-prone. Even worse, conversions between different units rely entirely on manual calculation by the programmer, where a single slip-up can cause problems.

**User-defined literals (UDL)**, introduced in C++11, are designed to solve this problem. They allow us to define custom literal suffixes, such as `1000_ms`, `3.3_V`, or `10_kHz`, making code more intuitive and safer. Furthermore, all conversions can be completed at compile time with zero runtime overhead.

------

## The Four Forms of `operator""`

We define user-defined literals via the `operator""` suffix operator. Based on different parameter types, there are several main definition forms, corresponding to integer literals, floating-point literals, string literals, and character literals:

```cpp
// Cooked forms (compiler parses the value first)
ReturnType operator "" _suffix(unsigned long long int);  // Integer literal
ReturnType operator "" _suffix(long double);              // Floating-point literal
ReturnType operator "" _suffix(char);                     // Character literal

// Raw form (compiler passes the raw character sequence)
ReturnType operator "" _suffix(const char*, size_t);      // String literal
```

Here, we need to distinguish two pairs of concepts: **cooked** and **raw**. Cooked literals refer to literals that the compiler has already parsed and converted—for integer and floating-point types, the compiler parses them into numeric types before passing them to `operator""`. Raw literals receive the raw character sequence, and the compiler performs no parsing. String literals only support the raw form, while integer literals support both cooked (`unsigned long long int`) and raw (const char sequence) forms.

Let's start with a simple example:

```cpp
constexpr Duration operator "" _ms(unsigned long long ms) {
    return Duration{ms};
}

// Usage
auto d = 100_ms; // Calls operator"" _ms(100)
```

`100_ms` is parsed by the compiler, which calls `operator"" _ms(100)`, returning a `Duration` object. The function signature `Duration operator"" _ms(unsigned long long)` only accepts parameters with units—you cannot pass a bare integer; the compiler will report an error directly. This is the source of type safety.

### Integer and Floating-Point Overloads

You can define overloads for integer and floating-point types separately, allowing the same suffix to behave differently in different contexts:

```cpp
// Integer: 1000 -> 1000 milliseconds
Duration operator "" _Hz(unsigned long long freq) {
    return Duration{1000 / freq}; // Simplified for demo
}

// Floating-point: 1.5 -> 1.5 kHz (1500 Hz)
Frequency operator "" _kHz(long double khz) {
    return Frequency{static_cast<unsigned long long>(khz * 1000)};
}
```

### String Literals

String literal operators receive a pointer to the string and its length, which can be used for compile-time string processing:

```cpp
constexpr uint32_t operator "" _id(const char* s, size_t len) {
    uint32_t hash = 0;
    for (size_t i = 0; i < len; ++i) {
        hash = hash * 31 + s[i];
    }
    return hash;
}

// Usage
constexpr auto EVENT_CLICK = "click"_id; // Calculated at compile time
```

In embedded systems, this can be used to implement efficient event IDs or message type identifiers—strings are converted to integers at compile time with zero runtime overhead.

### Raw Integer Literals

Integer literals also have a raw form that accepts a character sequence template, allowing you to handle formats not natively supported by the compiler:

```cpp
Binary operator "" _bin(const char* str) {
    return Binary{str}; // Custom parsing logic
}

// Usage
auto value = 1010_bin; // Custom binary format
```

This raw form was very useful before C++14—since C++14 is when `0b` binary literals were introduced. Although the standard now supports them, the raw form can still be used to implement custom base conversions.

------

## Standard Library Literals

C++14 introduced a batch of commonly used literal suffixes into the standard library. To use them, you need to introduce the corresponding namespaces via `using namespace`. These suffixes do not have an underscore prefix—because they reside within the `std::literals` (or nested) namespaces, they are reserved for the standard library.

### chrono Literals (C++14)

```cpp
using namespace std::literals::chrono_literals;

auto t1 = 500ms;
auto t2 = 2s;
auto t3 = 100us;
```

### string Literals (C++14)

```cpp
using namespace std::literals::string_literals;

auto s1 = "hello"s; // std::string
```

### complex Literals (C++14)

```cpp
using namespace std::literals::complex_literals;

auto c = 3.0i; // std::complex<double>
```

### string_view Literals (C++17)

```cpp
using namespace std::literals::string_view_literals;

auto sv = "world"sv; // std::string_view
```

------

## Naming Rules

Regarding the naming of UDL suffixes, the C++ standard has clear rules:

**Suffixes not starting with an underscore are reserved for the standard library**. Therefore, suffixes like `ms`, `s`, `min` that do not require an underscore can only be defined by the standard library. User-defined suffixes **must start with an underscore**, such as `_ms`, `_kHz`, `_MHz`.

Additionally, identifiers starting with `__` (double underscore) or containing `__` are reserved for the implementation (compiler) and must not be used.

The recommended naming style is to use an underscore followed by a short but clear suffix: `_ms`, `_us`, `_hz`, `_khz`, `_v`, `_mv`, `_ma`, `_ua`. When defining them in header files, be sure to place them within a namespace to avoid polluting the global namespace:

```cpp
namespace my_literals {
    constexpr Duration operator "" _ms(unsigned long long);
    constexpr Voltage operator "" _v(long double);
}
```

------

## Compile-Time vs. Runtime

UDL combined with `constexpr` enables pure compile-time unit conversion, which is one of its most powerful features. Be sure to mark literal operators as `constexpr` so that `1000_ms` is optimized into a constant by the compiler with no runtime overhead:

```cpp
constexpr Duration operator "" _ms(unsigned long long val) {
    return Duration{val}; // Compile-time calculation
}

// Usage
constexpr auto timeout = 5000_ms; // No runtime cost
```

If you do not mark it `constexpr`, the literal operator becomes a normal function call—although the overhead is small after inlining, you lose the ability for compile-time calculation and cannot use it in `constexpr` contexts or template parameters.

C++20 introduced `consteval`, which forces literal operators to execute only at compile time:

```cpp
consteval Duration operator "" _ms(unsigned long long val) {
    return Duration{val};
}
```

------

## Common Pitfalls

### Suffix Naming Conflicts

If you define a `_ms` suffix in a header file, and another library also defines a `_ms` suffix with a different implementation, ambiguity will arise upon linking. The solution is to use unique prefixes for suffixes or always use full namespace qualification.

### Floating-Point Precision

Floating-point UDLs may have precision issues. `0.1` in floating-point arithmetic may not equal exactly `0.1`. The solution is to use integers for representation—for example, storing millivolts instead of volts:

```cpp
// Good: Use integer millivolts
constexpr int operator "" _mV(long double v) {
    return static_cast<int>(v * 1000);
}

auto voltage = 3.3_mV; // 3300
```

### Operator Precedence

```cpp
auto result = 5_s + 100_ms * 2; // Is this (5_s + 100_ms) * 2 or 5_s + (100_ms * 2)?
```

Literal operators have the same precedence as normal operators and associate left-to-right. When writing complex expressions, pay attention to adding parentheses.

### Integer Overflow

Unit conversion of large numbers might overflow. If your UDL involves multiplication (like multiplying by 1,000,000 for `_MHz`), consider the upper limit of `unsigned long long` (about 1.8 * 10^19) and note the range limitations in your documentation. Note that integer overflow is **undefined behavior** in C++, and the compiler may not issue a warning.

------

## General Examples

Finally, let's look at a few commonly used literal definitions that you can directly apply to your project:

```cpp
namespace app {
    namespace literals {
        // Time units (milliseconds)
        constexpr uint32_t operator "" _ms(unsigned long long val) {
            return static_cast<uint32_t>(val);
        }

        // Frequency (Hz)
        constexpr uint32_t operator "" _Hz(unsigned long long val) {
            return static_cast<uint32_t>(val);
        }

        // Voltage (millivolts)
        constexpr uint32_t operator "" _mV(long double val) {
            return static_cast<uint32_t>(val * 1000);
        }
    }
}

using namespace app::literals;
```

Usage:

```cpp
// Configure UART
UART_Init(115200_Hz);

// Configure ADC sampling period
ADC_SetPeriod(10_ms);

// Configure voltage threshold
Comparator_SetThreshold(1200_mV);
```

Every number is followed by its unit, making the code almost self-documenting (it's truly satisfying!).

## Summary

User-defined literals essentially use compile-time capabilities to dress "bare numbers" in units—`1000_ms`, `3.3_V`, `115200_Hz` are understandable at a glance, and all conversions are completed at compile time with zero runtime overhead. Remember these key points:

- `operator""` has four cooked forms (`unsigned long long` / `long double` / `char` / `const char*`) plus one raw form (string template). For daily use, cooked forms are sufficient; only use raw forms when parsing custom numeric syntax (binary, thousand separators).
- Suffixes **must start with an underscore** (e.g., `_ms`). Suffixes without underscores (like `ms`) are reserved for the standard library; using them yourself will eventually lead to trouble.
- Use what's available in the standard library first (like `std::chrono`'s `ms`, `s`, `us`), and create your own only when they aren't enough.
- Literals are compile-time constants, so you can safely put them in `constexpr`, template parameters, and array sizes.

The cost is almost zero, and the benefit is eliminating the question "what unit is this number?" from code reviews entirely. How to organize a full set of literal libraries for your own real-world engineering projects will be expanded upon in the UDL in Practice chapter.

## Reference Resources

- [cppreference: User-defined literals](https://en.cppreference.com/w/cpp/language/user_literal)
- [cppreference: std::literals](https://en.cppreference.com/w/cpp/symbol_index/literals)
