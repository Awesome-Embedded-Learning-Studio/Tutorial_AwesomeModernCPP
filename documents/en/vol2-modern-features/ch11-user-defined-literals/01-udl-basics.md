---
chapter: 11
cpp_standard:
- 11
- 14
- 17
description: Operator "" Raw/Cooked Forms and Standard Library Literals
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
  source_hash: 81be9869727ffd1f7c21abc8e4aa0794b66b3b7a3c6d8188a0c9e2fe16295976
  translated_at: '2026-06-24T01:19:22.886253+00:00'
  engine: anthropic
  token_count: 2450
---
# User-Defined Literals Basics

When writing embedded code, we often run into frustrating scenarios: does the `1000` in `TIM1->ARR = (1000 - 1)` represent milliseconds or microseconds? Is `USART1->BRR = 0x271` meant for 9600 or 115200? Does `#define BUFFER_SIZE 1024` refer to bytes or words? These "magic numbers" are not only hard to understand but also error-prone. Even worse, conversions between different units rely entirely on manual calculation by the programmer, where a single slip-up can cause problems.

**User-defined literals** (UDL), introduced in C++11, are designed to solve this. They allow us to define custom literal suffixes, such as `100_ms`, `72_MHz`, or `4_KiB`, making code more intuitive and safer. Furthermore, all conversions can be performed at compile time, resulting in zero runtime overhead.

------

## Four Forms of `operator""`

We define user-defined literals using the `operator""` suffix operator. Based on the parameter types, there are four main definition forms, corresponding to integer literals, floating-point literals, string literals, and character literals:

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

We need to distinguish between two pairs of concepts: **cooked** and **raw**. Cooked literals refer to literals that the compiler has already parsed and converted—for integer and floating-point types, the compiler parses them into numeric types before passing them to `operator""`. Raw literals receive the raw character sequence, and the compiler performs no parsing. String literals only support the raw form, while integer literals support both cooked (`unsigned long long`) and raw (`const char*`) forms.

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

After the compiler parses `500_ms`, it calls `operator""_ms(500)` and returns a `Milliseconds` object. The function signature `delay(Milliseconds)` only accepts parameters with units—we cannot pass a bare integer, or the compiler will raise an error directly. This is the source of type safety.

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

String literal operators take a pointer to the string and its length, which we can use for compile-time string processing:

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

This can be used in embedded systems to implement efficient event IDs, message type identifiers, and more—strings are converted to integers at compile time with zero runtime overhead.

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

This raw form was quite useful before C++14, as `0b1010` binary literals were not introduced until C++14. Although the standard now supports them, the raw form can still be used to implement custom base conversions.

------

## Standard Library Literals

C++14 introduced a batch of commonly used literal suffixes in the standard library. To use them, we must introduce the corresponding namespaces via `using namespace`. These suffixes do not have an underscore prefix—because they reside within the `std::literals` namespace, they are literals reserved for the standard library.

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

### String Literals (C++14)

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

## Naming Rules

Regarding the naming of UDL suffixes, the C++ standard has clear rules:

**Suffixes not starting with `_` are reserved for the standard library**. Therefore, suffixes without underscores, such as `1ms` or `3.14s`, can only be defined by the standard library. User-defined suffixes **must start with `_`**, for example, `_ms`, `_Hz`, or `_V`.

Additionally, identifiers starting with `__` (double underscore) or containing `__` are reserved for the implementation (compiler) and must not be used.

We recommend using a naming style with `_` followed by a short but clear suffix: `_ms`, `_us`, `_Hz`, `_kHz`, `_MHz`, `_V`, `_mV`, `_KiB`. When defining these in a header file, we must place them within a namespace to avoid polluting the global namespace:

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

## Compile-Time vs. Run-Time

User-defined literals combined with `constexpr` allow for purely compile-time unit conversion. This is one of their most powerful features. We must mark the literal operator as `constexpr`. This way, `500_ms` is optimized by the compiler into a constant, resulting in zero runtime overhead:

```cpp
constexpr Milliseconds operator""_ms(unsigned long long v) {
    return Milliseconds{v};
}

constexpr auto startup_delay = 100_ms;
// startup_delay 在编译期就已经构造好了
// 生成的代码等价于直接写 Milliseconds{100}
```

If we do not mark it as `constexpr`, the literal operator becomes a normal function call. Although the overhead is minimal after inlining, we lose the ability to perform compile-time computation, and we cannot use it with `static_assert` or template arguments.

C++20 introduced `consteval`, which forces literal operators to execute only at compile time:

```cpp
consteval Milliseconds operator""_ms(unsigned long long v) {
    return Milliseconds{v};
}

constexpr auto t1 = 100_ms;   // OK，编译期执行
// 注意：consteval 要求字面量必须是编译期常量
// 例如：std::stoi("123")_ms 会编译失败，因为 stoi 不是 constexpr
```

## Common Pitfalls

### Suffix Naming Conflicts

If we define a `_deg` suffix in a header file, and another library defines a `_deg` suffix with the same name but a different implementation, ambiguity will arise when using `using namespace`. The solution is to use a unique prefix for the suffix, or to always use the full namespace qualification.

### Floating-Point Precision

Floating-point UDLs may have precision issues. `0.1_V + 0.2_V` might not equal `0.3_V` due to floating-point arithmetic. The solution is to use integer representations—for example, storing millivolts instead of volts:

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

Literal operators follow the same precedence and associativity (left-to-right) as standard operators. When writing complex expressions, we must ensure we use parentheses correctly.

### Integer Overflow

Unit conversions for large numbers can cause overflow. If our UDL involves multiplication (for example, multiplying by 1,000,000 in `operator""_ms`), we must consider the upper limit of `unsigned long long` (approximately $1.8 \times 10^{19}$) and document the range limitations. Note that integer overflow is **undefined behavior** in C++, and the compiler might not issue a warning.

------

## Common Examples

Finally, let's look at several common literal definitions that we can use directly in our projects:

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

**When using:**

```cpp
using namespace mylib::literals;

auto delay_time = 100_ms;
auto sys_clock = 72_MHz;
auto buffer_size = 4_KiB;
auto room_temp = 25.0_degC;
auto angle = 3.14159_rad;
```

Every number is accompanied by its unit, making the code almost self-explanatory (it's truly satisfying to read!)


## Summary

User-defined literals essentially use compile-time capabilities to dress "bare numbers" in units—`100_ms`, `72_MHz`, `4_KiB` are instantly readable, with all conversions happening at compile time and zero runtime overhead. Keep these key points in mind:

- `operator""` has four cooked forms (`unsigned long long`, `long double`, `const char*`, `char`) plus one raw form (string templates). For daily use, cooked forms are sufficient; only reach for raw forms when parsing custom numeric syntax (like binary or thousand separators).
- Suffixes must **start with an underscore** (`_ms`). Suffixes without underscores (`ms`) are reserved for the standard library; using them yourself will eventually lead to trouble.
- Use what is available in the standard library first (`chrono`'s `1h/1min/1s`, `"abc"s`, `"abc"sv`), and define your own only when those aren't enough.
- Literals are compile-time constants, so you can safely place them in `constexpr`, template parameters, and array sizes.

The cost is almost zero, and the benefit is completely eliminating the question "what unit is this number?" from code reviews. We'll save how to organize a complete literal library in a real-world project for the practical UDL chapter.

## References

- [cppreference: User-defined literals](https://en.cppreference.com/w/cpp/language/user_literal)
- [cppreference: std::literals](https://en.cppreference.com/w/cpp/symbol_index/literals)
