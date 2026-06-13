---
title: User-Defined Literal Fundamentals
description: operator"" Raw/Cooked Forms and Standard Library Literals
chapter: 11
order: 1
tags:
- host
- cpp-modern
- intermediate
- 字面量
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
reading_time_minutes: 9
prerequisites:
- 'Chapter 2: constexpr 基础'
related:
- UDL 实战
translation:
  source: documents/vol2-modern-features/ch11-user-defined-literals/01-udl-basics.md
  source_hash: 79f96fecadfca38c1c66530fe58a6e4434c6ce14d6dc59e0fd46dcda20c1dd9e
  translated_at: '2026-06-13T11:50:08.723988+00:00'
  engine: anthropic
  token_count: 2455
---
# Basics of User-Defined Literals

When writing embedded code, I often encounter frustrating scenarios: Is the `1000` in `delay(1000)` in milliseconds or microseconds? Is `Serial.begin(9600)` actually 9600 or 115200? Is `buffer[512]` in bytes or words? These "magic numbers" are not only hard to understand but also error-prone. Even worse, conversions between different units rely entirely on manual calculation by the programmer, where a single slip-up can cause problems.

**User-defined literals (UDL)**, introduced in C++11, are designed to solve this problem. They allow us to define our own literal suffixes, such as `100_ms`, `3.3_V`, or `16_kB`, making code more intuitive and safer. Furthermore, all conversions can be completed at compile time, resulting in zero runtime overhead.

------

## Four Forms of `operator""`

User-defined literals are defined via the `operator""` suffix operator. Based on different parameter types, there are several main definition forms, corresponding to integer literals, floating-point literals, string literals, and character literals:

```cpp
// Cooked integer:    operator"" _suffix(unsigned long long int)
// Cooked floating:    operator"" _suffix(long double)
// Raw character:      operator"" _suffix(const char*, std::size_t)
// Raw character pack: operator"" _suffix(const char*)
```

Here, we need to distinguish two pairs of concepts: **cooked** and **raw**. Cooked literals refer to literals that have already been parsed and converted by the compiler—for integer and floating-point types, the compiler parses them into numeric types before passing them to `operator""`. Raw literals receive the raw character sequence, and the compiler performs no parsing. String literals only support the raw form, while integer literals support both cooked (`unsigned long long int`) and raw (character sequence template) forms.

Let's start with a simplest example:

```cpp
struct Duration {
    unsigned long long int microseconds;
};

constexpr Duration operator"" _us(unsigned long long int us) {
    return Duration{us};
}

void delay(Duration d);

// Usage
delay(1000_us); // 1000 microseconds
```

`1000_us` is parsed by the compiler, which calls `operator""_us`, returning a `Duration` object. The function signature `void delay(Duration d)` only accepts parameters with units—you cannot pass a bare integer, and the compiler will report an error directly. This is the source of type safety.

### Integer and Floating-Point Overloads

You can define overloads for integer and floating-point types separately, allowing the same suffix to behave differently in different contexts:

```cpp
void operator"" _temp(long double kelvin) {
    // Handle floating-point temperature
}

void operator"" _temp(unsigned long long int kelvin) {
    // Handle integer temperature
}
```

### String Literals

String literal operators receive a pointer to a string and its length, which can be used for compile-time string processing:

```cpp
constexpr std::size_t operator"" _hash(const char* str, std::size_t len) {
    return std::hash<std::string_view>{}(std::string_view{str, len});
}

// Usage
constexpr auto id = "sensor_start"_hash; // Compile-time hash
```

In embedded systems, this can be used to implement efficient event IDs and message type identifiers—strings are converted to integers at compile time, with zero runtime overhead.

### Raw Integer Literals

Integer literals also have a raw form, accepting a character sequence template parameter, allowing you to handle formats not natively supported by the compiler:

```cpp
template <char... Chars>
constexpr unsigned long long int operator"" _bin() {
    // Parse Chars... as binary
    return parse_binary<Chars...>();
}

// Usage
auto value = 1010_bin; // Custom binary literal
```

This raw form was very useful before C++14—because C++14 introduced the `0b` binary literal. Although the standard now supports it, the raw form can still be used to implement custom base conversions.

------

## Standard Library Literals

C++14 introduced a batch of commonly used literal suffixes into the standard library. To use them, you need to introduce the corresponding namespaces via `using namespace`. These suffixes do not have an underscore prefix—because they are within the `std::literals` namespace, they are reserved for the standard library.

### chrono Literals (C++14)

```cpp
using namespace std::literals::chrono_literals;

auto timeout = 100ms;
auto interval = 5s;
```

### string Literals (C++14)

```cpp
using namespace std::literals::string_literals;

auto s = "hello"s; // std::string
```

### complex Literals (C++14)

```cpp
using namespace std::literals::complex_literals;

auto c = 3.0i; // Imaginary number
```

### string_view Literals (C++17)

```cpp
using namespace std::literals::string_view_literals;

auto sv = "data"sv; // std::string_view
```

------

## Naming Rules

Regarding the naming of UDL suffixes, the C++ standard has clear regulations:

**Suffixes not starting with an underscore are reserved for the standard library**. Therefore, suffixes like `ms`, `s`, `il`, which do not require an underscore, can only be defined by the standard library. User-defined suffixes **must start with an underscore**, such as `_ms`, `_Hz`, `_kB`.

Additionally, identifiers starting with `__` (double underscore) or containing `__` are reserved for the implementation (compiler) and cannot be used.

The recommended naming style is to use an underscore `_` followed by a short but clear suffix: `_ms`, `_us`, `_Hz`, `_ohm`, `_V`, `_A`, `_mA`, `_kB`. When defining in a header file, be sure to place them within a namespace to avoid polluting the global namespace:

```cpp
namespace my_literals {
    constexpr Duration operator"" _ms(unsigned long long int);
}
using namespace my_literals;
```

------

## Compile-Time vs Runtime

UDL combined with `constexpr` can achieve pure compile-time unit conversion, which is one of its most powerful features. Always mark literal operators as `constexpr`, so that `1000_ms` is optimized by the compiler into a constant with no runtime overhead:

```cpp
constexpr Duration operator"" _ms(unsigned long long int val) {
    return Duration{val * 1000}; // Compile-time multiplication
}

// Usage
constexpr auto d = 5_ms; // No runtime calculation
```

If you don't mark it `constexpr`, the literal operator becomes a normal function call—although the overhead is small after inlining, you lose the ability for compile-time computation and cannot use it for `constexpr` variables or template parameters.

C++20 introduced `consteval`, which forces the literal operator to execute only at compile time:

```cpp
consteval Duration operator"" _ms(unsigned long long int val) {
    return Duration{val * 1000};
}
```

------

## Common Pitfalls

### Suffix Naming Conflicts

If you define a `_ms` suffix in a header file, and another library also defines a `_ms` with a different implementation, ambiguity will arise during linking. The solution is to use a unique prefix for your suffixes or always use full namespace qualification.

### Floating-Point Precision

Floating-point UDLs may have precision issues. `0.1` in floating-point arithmetic may not exactly equal `0.1`. The solution is to use integers for representation—for example, storing millivolts instead of volts:

```cpp
constexpr int operator"" _mV(long double val) {
    return static_cast<int>(val * 1000);
}
```

### Operator Precedence

```cpp
auto result = 5_ms + 100_us; // OK
auto result = 5_ms * 2;      // OK
```

Literal operators have the same precedence as normal operators and associate left-to-right. Pay attention to parentheses when writing complex expressions.

### Integer Overflow

Unit conversion of large numbers might overflow. If your UDL involves multiplication (like multiplying by 1,000,000 in `_s`), consider the upper limit of `unsigned long long int` (approx 1.8 * 10^19) and note the range limitations in your documentation. Note that integer overflow is **undefined behavior** in C++, and the compiler may not issue a warning.

------

## General Examples

Finally, let's look at several commonly used literal definitions that you can directly apply to your project:

```cpp
namespace app {
    namespace literals {
        // Time
        constexpr uint64_t operator"" _hz(unsigned long long int hz) { return hz; }
        constexpr uint64_t operator"" _khz(unsigned long long int khz) { return khz * 1000; }
        constexpr uint64_t operator"" _mhz(unsigned long long int mhz) { return mhz * 1000000; }

        // Voltage
        constexpr uint32_t operator"" _mv(long double v) { return static_cast<uint32_t>(v * 1000); }

        // Memory
        constexpr size_t operator"" _kb(unsigned long long int kb) { return kb * 1024; }
        constexpr size_t operator"" _mb(unsigned long long int mb) { return mb * 1024 * 1024; }
    }
}
using namespace app::literals;

// Usage
I2C_Init(400_khz);
ADC_SetRef(3300_mv); // 3.3V in mV
uint8_t buffer[64_kb];
```

When using them:

```cpp
Timer_SetPrescaler(72_mhz);
UART_Init(115200_hz);
```

Every number is followed by its unit, so the code almost needs no comments (it's truly satisfying to look at!).

## Summary

User-defined literals essentially use compile-time capabilities to dress "bare numbers" in units—`1000_hz`, `3.3_v`, `64_kb` are understood at a glance, and all conversions are completed at compile time with zero runtime overhead. Remember these key points:

- `operator""` has four cooked forms (`unsigned long long int` / `long double` / `char` / `const char*`) plus one raw form (character sequence template). Daily use of cooked is sufficient; only use raw when you need to parse custom numeric syntax (binary, thousand separators).
- Suffixes **must start with an underscore** (`_ms`). Suffixes without underscores (`ms`) are reserved for the standard library; using them yourself will eventually lead to trouble.
- Use the existing ones in the standard library first (`std::literals`'s `ms`, `s`, `sv`), and define your own only if they are not enough.
- Literals are compile-time constants, so you can safely put them into `constexpr`, template parameters, and array sizes.

The cost is almost zero, and the benefit is eliminating the question "what unit is this number?" from code reviews. How to organize a full set of literal libraries in a real project will be expanded in the UDL in Practice article.

## Reference Resources

- [cppreference: User-defined literals](https://en.cppreference.com/w/cpp/language/user_literal)
- [cppreference: std::literals](https://en.cppreference.com/w/cpp/symbol_index/literals)
