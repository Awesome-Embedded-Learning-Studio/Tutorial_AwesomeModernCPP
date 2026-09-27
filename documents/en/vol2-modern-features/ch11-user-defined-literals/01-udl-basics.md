---
chapter: 11
cpp_standard:
- 11
- 14
- 17
description: The raw and cooked forms of operator"" and the standard library literals
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 2: constexpr Basics: The Art of Compile-Time Evaluation'
reading_time_minutes: 10
related:
- 'UDL in Practice: A Type-Safe Unit System'
tags:
- host
- cpp-modern
- intermediate
- 字面量
title: 'User-Defined Literals: The Basics'
translation:
  source: documents/vol2-modern-features/ch11-user-defined-literals/01-udl-basics.md
  source_hash: 4e4ddff2934406437f1073653139ea4834477d66b4091782985ff2935dae79d5
  translated_at: '2026-09-25T16:55:39+00:00'
  engine: anthropic
  token_count: 4000
---
# User-Defined Literals: Letting 1000_ms Tell You Its Unit

When we write embedded code, we keep running into the same uncomfortable situations: in `TIM1->ARR = (1000 - 1)`, is that 1000 in milliseconds or microseconds? Is `USART1->BRR = 0x271` 9600 baud or 115200? Is `#define BUFFER_SIZE 1024` in bytes or words? These "magic numbers" are hard to understand and easy to get wrong — worse still, conversions between different units depend entirely on the programmer doing the arithmetic by hand, and one careless slip is all it takes.

**User-defined literals** (UDL), introduced in C++11, exist to solve exactly this problem. They let us define our own literal suffixes, such as `100_ms`, `72_MHz`, and `4_KiB`, making the code more intuitive and safer — and every conversion can be done at compile time, with zero runtime overhead.

------

## The Four Forms of operator""

A user-defined literal is defined with the `operator""` suffix operator. Depending on the parameter type, there are a few major forms, corresponding to integer literals, floating-point literals, string literals, and character literals:

```cpp
// Integer literal (cooked form)
ReturnType operator""_suffix(unsigned long long value);

// Floating-point literal (cooked form)
ReturnType operator""_suffix(long double value);

// String literal (raw form)
ReturnType operator""_suffix(const char* str, size_t length);

// Character literal (cooked form)
ReturnType operator""_suffix(char c);
```

Here we need to distinguish two concepts: **cooked** and **raw**. A cooked literal is one the compiler has already parsed and converted — for integers and floating-point numbers, the compiler first parses them into numeric types and then passes them to `operator""`. A raw literal receives the original character sequence, with no parsing done by the compiler at all. String literals only support the raw form, while integer literals support both the cooked form (`unsigned long long`) and the raw form (`const char*`).

Let's start with the simplest possible example:

```cpp
#include <cstdint>

struct Milliseconds {
    std::uint64_t value;
    constexpr explicit Milliseconds(std::uint64_t v) : value(v) {}
};

constexpr Milliseconds operator""_ms(unsigned long long v) {
    return Milliseconds{v};
}

void delay(Milliseconds ms);

void example() {
    delay(500_ms);  // Clear: 500 milliseconds
    // delay(500);  // Compile error! The unit must be explicit
}
```

After the compiler parses `500_ms`, it calls `operator""_ms(500)`, which returns a `Milliseconds` object. The function signature `delay(Milliseconds)` only accepts arguments that carry a unit — a bare integer cannot get in, and the compiler rejects it outright. That is where the type safety comes from.

Let's break down how `500_ms` gets parsed:

![Anatomy of the user-defined literal 500_ms](./01-udl-anatomy.drawio)

### Integer and Floating-Point Overloads

You can define separate overloads for integers and floating-point numbers, so the same suffix behaves differently in different contexts:

```cpp
struct Frequency {
    std::uint32_t hz;
    constexpr explicit Frequency(std::uint32_t v) : hz(v) {}
};

// Integer version: 100_Hz
constexpr Frequency operator""_Hz(unsigned long long value) {
    return Frequency{static_cast<std::uint32_t>(value)};
}

// Floating-point version: 1.5_kHz
constexpr Frequency operator""_kHz(long double value) {
    return Frequency{static_cast<std::uint32_t>(value * 1000.0)};
}

void example() {
    auto f1 = 100_Hz;    // Integer overload, f1.hz = 100
    auto f2 = 1.5_kHz;   // Floating-point overload, f2.hz = 1500
}
```

### String Literals

The string literal operator receives a pointer to the string plus its length, which opens the door to compile-time string processing:

```cpp
#include <cstdint>

/// FNV-1a hash (compile time)
constexpr std::uint32_t hash_string(
    const char* str, std::uint32_t value = 2166136261u) {
    return *str
        ? hash_string(str + 1,
            (value ^ static_cast<std::uint32_t>(*str)) * 16777619u)
        : value;
}

constexpr std::uint32_t operator""_hash(
    const char* str, std::size_t len) {
    return hash_string(str);
}

void example() {
    constexpr auto id1 = "temperature"_hash;
    constexpr auto id2 = "humidity"_hash;
    static_assert(id1 != id2);
}
```

In embedded work this can be used to build efficient event IDs, message type identifiers, and the like — the string is converted to an integer at compile time, so the runtime cost is zero.

### Raw Integer Literals

Integer literals also have a raw form, which receives a `const char*` and lets you handle formats the compiler does not natively support:

```cpp
#include <cstdint>

struct Binary {
    std::uint64_t value;
};

constexpr Binary operator""_bin(const char* str, std::size_t length) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < length; ++i) {
        value = value * 2;
        if (str[i] == '1') value += 1;
    }
    return Binary{value};
}

void example() {
    auto b1 = 1010_bin;       // 10
    auto b2 = 11111111_bin;   // 255
}
```

This raw form was extremely useful before C++14 — it was C++14 that introduced `0b1010` binary literals. The standard supports them natively now, but the raw form is still there for implementing custom base conversions.

------

## Standard Library Literals

C++14 introduced a batch of commonly used literal suffixes into the standard library; to use them, pull in the corresponding namespace with `using namespace`. These suffixes come without a leading underscore — they live inside the `std::literals` namespaces and are the standard library's reserved literals.

### chrono Literals (C++14)

```cpp
#include <chrono>

using namespace std::chrono_literals;

void example() {
    auto t1 = 1s;         // std::chrono::seconds{1}
    auto t2 = 500ms;      // std::chrono::milliseconds{500}
    auto t3 = 2us;        // std::chrono::microseconds{2}
    auto t4 = 100ns;      // std::chrono::nanoseconds{100}
    auto t5 = 1min;       // std::chrono::minutes{1}
    auto t6 = 1h;         // std::chrono::hours{1}

    auto total = 1s + 500ms;  // 1500ms
}
```

### string Literals (C++14)

```cpp
#include <string>

using namespace std::string_literals;

void example() {
    auto s1 = "hello"s;    // std::string
    auto s2 = L"wide"s;    // std::wstring
    auto s3 = u"utf16"s;   // std::u16string
    auto s4 = U"utf32"s;   // std::u32string
}
```

### complex Literals (C++14)

```cpp
#include <complex>

using namespace std::complex_literals;

void example() {
    auto c1 = 3.0 + 4.0i;   // std::complex<double>{3.0, 4.0}
    auto c2 = 1.0i;          // the imaginary unit
}
```

### string_view Literals (C++17)

```cpp
#include <string_view>

using namespace std::string_view_literals;

void example() {
    auto sv = "hello"sv;   // std::string_view
}
```

------

## Naming Rules

The C++ standard has explicit rules about how UDL suffixes may be named:

**Suffixes that do not start with `_` are reserved for the standard library.** So suffixes like `1ms` and `3.14s`, which need no underscore, can only be defined by the standard library. User-defined suffixes **must start with `_`**, for example `_ms`, `_Hz`, `_V`.

In addition, identifiers that begin with `__` (a double underscore), or that contain `__` anywhere, are reserved for the implementation (the compiler) and must not be used.

The recommended naming style is `_` plus a short but clear suffix: `_ms`, `_us`, `_Hz`, `_kHz`, `_MHz`, `_V`, `_mV`, `_KiB`. When defining these in a header file, always place them inside a namespace to avoid polluting the global namespace:

```cpp
namespace mylib::literals {
    constexpr Milliseconds operator""_ms(unsigned long long v) {
        return Milliseconds{v};
    }
}

// Usage
using namespace mylib::literals;
auto t = 500_ms;
```

------

## Compile Time vs Runtime

UDL combined with `constexpr` enables purely compile-time unit conversion — one of its most powerful features. Always mark your literal operators `constexpr`; that way `500_ms` is optimized by the compiler into a constant, with no runtime overhead:

```cpp
constexpr Milliseconds operator""_ms(unsigned long long v) {
    return Milliseconds{v};
}

constexpr auto startup_delay = 100_ms;
// startup_delay is already constructed at compile time
// The generated code is equivalent to writing Milliseconds{100} directly
```

Without the `constexpr` marker, the literal operator becomes an ordinary function call — small cost once inlined, but you lose the ability to compute at compile time, and it can no longer be used in `static_assert` or as template arguments.

C++20 introduced `consteval`, which forces a literal operator to execute only at compile time:

```cpp
consteval Milliseconds operator""_ms(unsigned long long v) {
    return Milliseconds{v};
}

constexpr auto t1 = 100_ms;   // OK, evaluated at compile time
// Note: consteval requires the literal to be a compile-time constant
// For example, std::stoi("123")_ms fails to compile, because stoi is not constexpr
```

------

## Common Pitfalls

### Suffix Name Collisions

If you define a `_deg` suffix in a header file, and another library also defines a `_deg` with the same name but a different implementation, bringing both in via `using namespace` produces ambiguity. The solution is to use a distinctive prefix for your suffixes, or to always use full namespace qualification.

### Floating-Point Precision

Floating-point UDLs can have precision problems. In floating-point arithmetic, `0.1_V + 0.2_V` may not equal `0.3_V`. The solution is an integer representation — for example, storing millivolts instead of volts:

```cpp
struct Voltage {
    std::int64_t millivolts;  // stored as an integer
};

constexpr Voltage operator""_V(long double value) {
    return Voltage{
        static_cast<std::int64_t>(value * 1000.0 + 0.5)};
}

constexpr auto v1 = 0.1_V + 0.2_V;
constexpr auto v2 = 0.3_V;
static_assert(v1.millivolts == v2.millivolts);  // OK
```

### Operator Precedence

```cpp
auto x = 100_km / 2 * 3;  // (100_km / 2) * 3 = 150_km
auto y = 100_km / (2 * 3); // 100_km / 6 ≈ 16.67_km
```

Literal operators follow the same precedence and left-to-right associativity as ordinary operators. When writing complex expressions, be careful to add parentheses.

### Integer Overflow

Unit conversion on large numbers can overflow. If your UDL involves multiplication (say, multiplying by 1000000 inside `operator""_ms`), keep the upper limit of `unsigned long long` in mind (about 1.8 * 10^19) and document the range limits. Note that integer overflow is **undefined behavior** in C++, and the compiler may not emit a warning.

------

## General-Purpose Examples

To finish up, here are several commonly used literal definitions that you can drop straight into your own project:

```cpp
#include <cstdint>

namespace mylib::literals {

// ===== Time units =====
struct Milliseconds { std::uint64_t value; };
struct Microseconds { std::uint64_t value; };
struct Seconds      { std::uint64_t value; };

constexpr Milliseconds operator""_ms(unsigned long long v) {
    return Milliseconds{v};
}
constexpr Microseconds operator""_us(unsigned long long v) {
    return Microseconds{v};
}
constexpr Seconds operator""_s(unsigned long long v) {
    return Seconds{v};
}

// ===== Frequency units =====
struct Hertz { std::uint32_t value; };

constexpr Hertz operator""_Hz(unsigned long long v) {
    return Hertz{static_cast<std::uint32_t>(v)};
}
constexpr Hertz operator""_kHz(long double v) {
    return Hertz{static_cast<std::uint32_t>(v * 1000.0)};
}
constexpr Hertz operator""_MHz(long double v) {
    return Hertz{static_cast<std::uint32_t>(v * 1000000.0)};
}

// ===== Memory units =====
struct Bytes { std::uint64_t value; };

constexpr Bytes operator""_B(unsigned long long v) {
    return Bytes{v};
}
constexpr Bytes operator""_KiB(unsigned long long v) {
    return Bytes{v * 1024};
}
constexpr Bytes operator""_MiB(unsigned long long v) {
    return Bytes{v * 1024 * 1024};
}

// ===== Temperature units =====
struct Celsius    { double value; };
struct Fahrenheit { double value; };

constexpr Celsius operator""_degC(long double v) {
    return Celsius{static_cast<double>(v)};
}
constexpr Fahrenheit operator""_degF(long double v) {
    return Fahrenheit{static_cast<double>(v)};
}
constexpr Celsius operator""_degK(long double v) {
    return Celsius{static_cast<double>(v - 273.15)};
}

// ===== Angle units =====
struct Degrees { double value; };

constexpr Degrees operator""_deg(long double v) {
    return Degrees{static_cast<double>(v)};
}
constexpr Degrees operator""_rad(long double v) {
    return Degrees{static_cast<double>(v * 180.0 / 3.14159265358979323846)};
}

}  // namespace mylib::literals
```

Usage:

```cpp
using namespace mylib::literals;

auto delay_time = 100_ms;
auto sys_clock = 72_MHz;
auto buffer_size = 4_KiB;
auto room_temp = 25.0_degC;
auto angle = 3.14159_rad;
```

Every number carries its own unit along with it, and the code barely needs any comments (and honestly, that feels great!)

## References

- [cppreference: User-defined literals](https://en.cppreference.com/w/cpp/language/user_literal)
- [cppreference: std::literals](https://en.cppreference.com/w/cpp/symbol_index/literals)
