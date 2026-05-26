---
title: User-Defined Literal Basics
description: Raw/cooked forms of `operator""` and standard library literals
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
reading_time_minutes: 15
prerequisites:
- 'Chapter 2: constexpr 基础'
related:
- UDL 实战
translation:
  source: documents/vol2-modern-features/ch11-user-defined-literals/01-udl-basics.md
  source_hash: bf1662276299ecf1114d8fe0dac06e8ab11838f4108d20922adf6d969692ab9b
  translated_at: '2026-05-26T11:36:34.727568+00:00'
  engine: anthropic
  token_count: 2330
---
# User-Defined Literal Basics

When writing embedded code, we often run into frustrating scenarios: does the 1000 in `delay(1000)` represent milliseconds or microseconds? Is `9600` or `115200` the correct baud rate? Does `1024` refer to bytes or words? These "magic numbers" are not only hard to read but also error-prone. Even worse, conversions between different units rely entirely on manual calculation, leaving plenty of room for mistakes.

**User-defined literals (UDL)**, introduced in C++11, exist to solve this problem. They allow us to define custom literal suffixes, such as `1000_ms`, `9600_baud`, and `1024_bytes`, making code more intuitive and safer. All conversions happen at compile time, resulting in zero runtime overhead.

------

## The Four Forms of operator""

We define user-defined literals through the `operator""` suffix operator. Depending on the parameter type, there are several main definition forms, corresponding to integer literals, floating-point literals, string literals, and character literals:

```cpp
// 整数字面量（cooked 形式）
ReturnType operator""_suffix(unsigned long long value);

// 浮点数字面量（cooked 形式）
ReturnType operator""_suffix(long double value);

// 字符串字面量（raw 形式）
ReturnType operator""_suffix(const char* str, size_t length);

// 字符字面量（cooked 形式）
ReturnType operator""_suffix(char c);
```

There are two pairs of concepts to distinguish here: **cooked** and **raw**. Cooked literals are those that the compiler has already parsed and converted. For integer and floating-point types, the compiler parses them into numeric types before passing them to `operator""`. Raw literals receive the original character sequence without any parsing by the compiler. String literals only support the raw form, while integer literals support both the cooked (`unsigned long long`) and raw (`const char*`) forms.

Let's start with a simple example:

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
    delay(500_ms);  // 清晰：500 毫秒
    // delay(500);  // 编译错误！必须明确单位
}
```

`1000_ms` is parsed by the compiler, which calls `operator""_ms`, returning a `Milliseconds` object. The function signature `explicit Milliseconds(uint32_t ms)` only accepts parameters with units—bare integers won't compile, and the compiler will directly report an error. This is where type safety comes from.

### Integer and Floating-Point Overloads

We can define separate overloads for integer and floating-point types, allowing the same suffix to behave differently in different contexts:

```cpp
struct Frequency {
    std::uint32_t hz;
    constexpr explicit Frequency(std::uint32_t v) : hz(v) {}
};

// 整数版本：100_Hz
constexpr Frequency operator""_Hz(unsigned long long value) {
    return Frequency{static_cast<std::uint32_t>(value)};
}

// 浮点版本：1.5_kHz
constexpr Frequency operator""_kHz(long double value) {
    return Frequency{static_cast<std::uint32_t>(value * 1000.0)};
}

void example() {
    auto f1 = 100_Hz;    // 整型版本，f1.hz = 100
    auto f2 = 1.5_kHz;   // 浮点版本，f2.hz = 1500
}
```

### String Literals

String literal operators receive a pointer to the string and its length, which can be used for compile-time string processing:

```cpp
#include <cstdint>

/// FNV-1a 哈希（编译期）
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

In embedded development, we can use this to implement efficient event IDs, message type identifiers, and more—strings are converted to integers at compile time, achieving zero runtime overhead.

### Raw Integer Literals

Integer literals also have a raw form that accepts a `const char*`, allowing us to handle formats not natively supported by the compiler:

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

This raw form was especially useful before C++14, since binary literals (`0b...`) were only introduced in C++14. Although the standard now supports them, the raw form can still be used to implement custom base conversions.

------

## Standard Library Literals

C++14 introduced a batch of commonly used literal suffixes into the standard library. To use them, we need to bring the corresponding namespaces into scope with `using namespace`. These suffixes do not have an underscore prefix—because they reside within the `std::` namespace, they are reserved for the standard library.

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
    auto c2 = 1.0i;          // 虚数单位
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

Regarding the naming of UDL suffixes, the C++ standard has clear rules:

**Suffixes not starting with an underscore are reserved for the standard library**. Therefore, suffixes without underscores like `s`, `ms`, and `i` can only be defined by the standard library. User-defined suffixes **must start with an underscore (`_`)**, such as `_ms`, `_us`, `_hz`.

Additionally, identifiers starting with `__` (double underscore) or containing `__` are reserved for the implementation (compiler) and must not be used.

The recommended naming style is an underscore followed by a short but clear suffix: `_ms`, `_us`, `_hz`, `_baud`, `_bytes`, `_kb`, `_mv`, `_percent`. When defining them in header files, always place them inside a namespace to avoid polluting the global namespace:

```cpp
namespace mylib::literals {
    constexpr Milliseconds operator""_ms(unsigned long long v) {
        return Milliseconds{v};
    }
}

// 使用时
using namespace mylib::literals;
auto t = 500_ms;
```

------

## Compile-Time vs. Runtime

UDLs combined with `constexpr` enable pure compile-time unit conversion, which is one of their most powerful features. Always mark literal operators as `constexpr` so that `1000_ms` is optimized into a constant by the compiler, with zero runtime overhead:

```cpp
constexpr Milliseconds operator""_ms(unsigned long long v) {
    return Milliseconds{v};
}

constexpr auto startup_delay = 100_ms;
// startup_delay 在编译期就已经构造好了
// 生成的代码等价于直接写 Milliseconds{100}
```

If we don't mark it `constexpr`, the literal operator becomes a normal function call. Although the overhead is minimal after inlining, we lose the ability to perform compile-time calculations and can no longer use it in `static_assert` or as template arguments.

C++20 introduced `consteval`, which forces the literal operator to execute only at compile time:

```cpp
consteval Milliseconds operator""_ms(unsigned long long v) {
    return Milliseconds{v};
}

constexpr auto t1 = 100_ms;   // OK，编译期执行
// 注意：consteval 要求字面量必须是编译期常量
// 例如：std::stoi("123")_ms 会编译失败，因为 stoi 不是 constexpr
```

------

## Common Pitfalls

### Suffix Naming Conflicts

If we define a `_ms` suffix in a header file, and another library defines a similarly named `_ms` with a different implementation, we will encounter ambiguity at link time. The solution is to use a unique prefix for suffixes, or always use fully qualified namespace specifiers.

### Floating-Point Precision

Floating-point UDLs can have precision issues. `0.1f` might not equal `0.1` in floating-point arithmetic. The solution is to use integers for representation—for example, storing millivolts instead of volts:

```cpp
struct Voltage {
    std::int64_t millivolts;  // 用整数存储
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

Literal operators have the same precedence as normal operators, associating from left to right. We need to be careful to add parentheses when writing complex expressions.

### Integer Overflow

Unit conversions involving large numbers might overflow. If a UDL involves multiplication (such as multiplying by 1,000,000 in `_us`), we need to consider the upper limit of `uint64_t` (approximately 1.8 * 10^19) and document the range limits. Note that integer overflow is **undefined behavior (UB)** in C++, and the compiler might not emit a warning.

------

## General Examples

Finally, let's look at a few commonly used literal definitions that we can directly drop into our projects:

```cpp
#include <cstdint>

namespace mylib::literals {

// ===== 时间单位 =====
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

// ===== 频率单位 =====
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

// ===== 内存单位 =====
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

// ===== 温度单位 =====
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

// ===== 角度单位 =====
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

Every number carries its unit right beside it, making the code almost self-documenting (it really is satisfying to read!)


## Reference Resources

- [cppreference: User-defined literals](https://en.cppreference.com/w/cpp/language/user_literal)
- [cppreference: std::literals](https://en.cppreference.com/w/cpp/symbol_index/literals)
