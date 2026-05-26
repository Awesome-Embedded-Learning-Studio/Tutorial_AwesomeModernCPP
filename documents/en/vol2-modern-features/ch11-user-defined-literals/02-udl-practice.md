---
title: 'UDL in Practice: A Type-Safe Unit System'
description: Implementing a type-safe physical unit system using user-defined literals
chapter: 11
order: 2
tags:
- host
- cpp-modern
- intermediate
- 字面量
- 类型安全
difficulty: intermediate
platform: host
cpp_standard:
- 14
- 17
reading_time_minutes: 15
prerequisites:
- 'Chapter 11: 用户自定义字面量基础'
- 'Chapter 4: 强类型 typedef'
related:
- constexpr 基础
translation:
  source: documents/vol2-modern-features/ch11-user-defined-literals/02-udl-practice.md
  source_hash: 9f70dc7cce796962a7a9bb3f7072a9d1e86793ef6dcbb9c6d15f3371aca82da2
  translated_at: '2026-05-26T11:36:20.077610+00:00'
  engine: anthropic
  token_count: 3063
---
# UDL in Practice: A Type-Safe Unit System

In the previous article, we covered the basic syntax of user-defined literals — `operator""` forms, standard library literals, and naming rules. Now, we will put that knowledge to use and build a truly practical **type-safe unit system**.

Our goal is to make `100_m + 500_m` return a length, `100_m / 2_s` return a velocity, and `100_m + 50_s` trigger a compile-time error. All conversions happen at compile time, with zero runtime overhead.

------

## Step 1: Length Unit System

Let's start with the simplest case: length units. We use a template to define a generic "value with a unit," and then define literals for different length units:

```cpp
#include <cstdint>
#include <type_traits>

/// 单位标签：用于区分不同类型的物理量
struct MeterTag {};
struct SecondTag {};

/// 带单位的值
template <typename T, typename UnitTag>
struct Quantity {
    T value;

    constexpr explicit Quantity(T v) : value(v) {}

    constexpr Quantity operator+(Quantity other) const {
        return Quantity{value + other.value};
    }

    constexpr Quantity operator-(Quantity other) const {
        return Quantity{value - other.value};
    }

    constexpr Quantity operator*(T scalar) const {
        return Quantity{value * scalar};
    }

    constexpr Quantity operator/(T scalar) const {
        return Quantity{value / scalar};
    }

    constexpr bool operator==(Quantity other) const {
        return value == other.value;
    }

    constexpr bool operator<(Quantity other) const {
        return value < other.value;
    }
};

/// 标量 × 单位（反向乘法）
/// 注意：这个模板要求标量类型 T 必须与 Quantity 的 T 完全匹配
/// 如果需要支持类型转换，需要提供额外的重载
template <typename T, typename UnitTag>
constexpr Quantity<T, UnitTag> operator*(
    T scalar, Quantity<T, UnitTag> q) {
    return q * scalar;
}

/// 支持整数标量 × long double Quantity 的重载
template <typename UnitTag>
constexpr Quantity<long double, UnitTag> operator*(
    int scalar, Quantity<long double, UnitTag> q) {
    return Quantity<long double, UnitTag>{q.value * scalar};
}
```

`Quantity<T, UnitTag>` is a template, and `UnitTag` is an empty tag type whose sole purpose is to make physical quantities with different units into different types. There is no inheritance relationship between `MeterTag` and `SecondTag`, so `Quantity<double, MeterTag>` and `Quantity<double, SecondTag>` are completely distinct types — you cannot assign one to the other.

Now, let's define the length type aliases and literals:

```cpp
using Length = Quantity<long double, MeterTag>;

// 字面量：以米为基准单位
constexpr Length operator""_m(long double v) {
    return Length{v};
}

constexpr Length operator""_km(long double v) {
    return Length{v * 1000.0L};
}

constexpr Length operator""_cm(long double v) {
    return Length{v / 100.0L};
}

constexpr Length operator""_mm(long double v) {
    return Length{v / 1000.0L};
}

// 整数版本
constexpr Length operator""_m(unsigned long long v) {
    return Length{static_cast<long double>(v)};
}

constexpr Length operator""_km(unsigned long long v) {
    return Length{static_cast<long double>(v) * 1000.0L};
}
```

Let's test it:

```cpp
void test_length() {
    constexpr auto d1 = 1.5_m;       // 1.5 米
    constexpr auto d2 = 2.0_km;      // 2000 米（注意：2_km 会失败，因为只定义了浮点重载）
    constexpr auto d3 = 100.0_cm;    // 1 米
    constexpr auto d4 = 500.0_mm;    // 0.5 米

    // 编译期计算
    constexpr auto total = 1.0_km + 500.0_m;  // 1500 米
    static_assert(total.value == 1500.0L);

    // 标量乘法（现在支持整数了）
    constexpr auto doubled = 2 * 100.0_m;  // 200 米
    static_assert(doubled.value == 200.0L);

    // 类型安全：不能把长度和时间相加
    // auto bad = 100_m + 50_s;  // 编译错误！
}
```

`1.0_km + 500.0_m` is evaluated at compile time as `1500.0_m`. If you try to add a length to a time, the compiler will immediately emit an error — because `Quantity<long double, MeterTag>` and `Quantity<long double, SecondTag>` are different types.

------

## Step 2: Time and Velocity Units

The length system can work independently, but the real beauty of physical calculations lies in combining different units. Dividing length by time yields velocity — we need `Quantity` to support this cross-unit operation:

```cpp
/// 速度标签
struct SpeedTag {};

using TimeDuration = Quantity<long double, SecondTag>;
using Speed = Quantity<long double, SpeedTag>;

// 时间字面量（以秒为基准）
constexpr TimeDuration operator""_s(long double v) {
    return TimeDuration{v};
}

constexpr TimeDuration operator""_ms(long double v) {
    return TimeDuration{v / 1000.0L};
}

constexpr TimeDuration operator""_min(long double v) {
    return TimeDuration{v * 60.0L};
}

constexpr TimeDuration operator""_h(long double v) {
    return TimeDuration{v * 3600.0L};
}

// 整数版本
constexpr TimeDuration operator""_s(unsigned long long v) {
    return TimeDuration{static_cast<long double>(v)};
}

constexpr TimeDuration operator""_ms(unsigned long long v) {
    return TimeDuration{static_cast<long double>(v) / 1000.0L};
}

/// 长度 / 时间 = 速度
constexpr Speed operator/(Length len, TimeDuration time) {
    return Speed{len.value / time.value};
}

/// 速度 * 时间 = 长度
constexpr Length operator*(Speed spd, TimeDuration time) {
    return Length{spd.value * time.value};
}

constexpr Length operator*(TimeDuration time, Speed spd) {
    return Length{spd.value * time.value};
}
```

Now we can perform physics calculations:

```cpp
void test_physics() {
    // 速度 = 距离 / 时间
    constexpr auto speed = 100.0_m / 10.0_s;   // 10 m/s
    static_assert(speed.value == 10.0L);

    // 距离 = 速度 * 时间
    constexpr auto distance = speed * 60.0_s;   // 600 米
    static_assert(distance.value == 600.0L);

    // 换算：36 km/h = 10 m/s
    constexpr auto v1 = 36.0_km / 1.0_h;       // 36000 / 3600 = 10 m/s
    static_assert(v1.value == 10.0L);

    // 类型安全
    // auto bad = 100_m + 10_s;    // 编译错误：长度 + 时间
    // auto bad2 = 100_m * 10_s;   // 编译错误：长度 * 时间（未定义）
}
```

The beauty of this code is that the compiler handles the unit checking for you — you cannot accidentally treat milliseconds as seconds, nor can you add a velocity to a distance.

------

## Step 3: Temperature Conversion Literals

Temperature is a special physical quantity because the conversion between different scales is not a simple linear scaling — the conversion between Celsius and Fahrenheit includes an offset. This is a perfect use case for UDLs:

```cpp
struct TemperatureTag {};
using Temperature = Quantity<long double, TemperatureTag>;

// 摄氏度：以开尔文为基准存储
constexpr Temperature operator""_degC(long double v) {
    return Temperature{v + 273.15L};
}

// 华氏度 -> 开尔文
constexpr Temperature operator""_degF(long double v) {
    return Temperature{(v - 32.0L) * 5.0L / 9.0L + 273.15L};
}

// 开尔文
constexpr Temperature operator""_degK(long double v) {
    return Temperature{v};
}

// 辅助函数：从开尔文转换到各温标
constexpr long double to_celsius(Temperature t) {
    return t.value - 273.15L;
}

constexpr long double to_fahrenheit(Temperature t) {
    return (t.value - 273.15L) * 9.0L / 5.0L + 32.0L;
}

constexpr long double to_kelvin(Temperature t) {
    return t.value;
}
```

Usage:

```cpp
void test_temperature() {
    constexpr auto t1 = 0.0_degC;     // 冰点：273.15 K
    constexpr auto t2 = 100.0_degC;   // 沸点：373.15 K
    constexpr auto t3 = 32.0_degF;    // 冰点（华氏）：273.15 K

    static_assert(to_kelvin(t1) == 273.15L);

    // 温度差可以相减（在开尔文空间中）
    constexpr auto delta = 10.0_degC - 0.0_degC;  // 10K
    static_assert(delta.value == 10.0L);

    // 摄氏 -> 华氏
    constexpr auto body_temp = 37.0_degC;
    // to_fahrenheit(body_temp) ≈ 98.6°F
}
```

Here, we use Kelvin as the internal storage, and all literals are converted to Kelvin upon construction. This way, temperature differences can be correctly added and subtracted.

------

## Step 4: String Processing Literals

UDLs are not limited to physical units. In general C++ development, string processing literals are also quite common:

```cpp
#include <string>
#include <string_view>
#include <algorithm>
#include <cctype>

/// 编译期字符串哈希——用于高效的字符串比较
constexpr std::uint32_t operator""_hash(
    const char* str, std::size_t len) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < len; ++i) {
        hash = (hash ^ static_cast<std::uint8_t>(str[i]))
             * 16777619u;
    }
    return hash;
}

/// 运行时转大写
std::string operator""_upper(const char* str, std::size_t len) {
    std::string result(str, len);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

/// 运行时 trim 空白
std::string operator""_trim(const char* str, std::size_t len) {
    std::string_view sv(str, len);
    while (!sv.empty() && std::isspace(sv.front())) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(sv.back())) sv.remove_suffix(1);
    return std::string(sv);
}

void test_string_literals() {
    constexpr auto id = "sensor_temp"_hash;   // 编译期整数
    auto upper = "hello world"_upper;          // "HELLO WORLD"
    auto trimmed = "  padded  "_trim;           // "padded"

    // 用于 switch-case（比字符串比较高效）
    constexpr auto cmd = "start"_hash;
    switch (cmd) {
        case "start"_hash:  /* 启动 */ break;
        case "stop"_hash:   /* 停止 */ break;
        default: break;
    }
}
```

String hash literals are particularly useful in embedded scenarios — you can replace runtime string comparisons with compile-time generated integers, which saves Flash (no need to store the strings) and improves performance (integer comparison vs. string comparison).

------

## Embedded Practice

In embedded development, the most practical scenarios for UDLs are frequency/baud rate literals and register address literals. Let's look at some specific examples.

### Frequency and Baud Rate

```cpp
#include <cstdint>

struct Frequency {
    std::uint32_t hz;

    constexpr std::uint32_t to_hz() const { return hz; }
    constexpr std::uint32_t to_khz() const { return hz / 1000; }

    /// 频率转周期（纳秒）
    constexpr std::uint64_t period_ns() const {
        return 1000000000ULL / hz;
    }
};

constexpr Frequency operator""_Hz(unsigned long long v) {
    return Frequency{static_cast<std::uint32_t>(v)};
}
constexpr Frequency operator""_kHz(long double v) {
    return Frequency{static_cast<std::uint32_t>(v * 1000.0)};
}
constexpr Frequency operator""_MHz(long double v) {
    return Frequency{static_cast<std::uint32_t>(v * 1000000.0)};
}

/// 波特率寄存器计算（STM32 USART）
constexpr std::uint16_t compute_brr(
    Frequency periph_clock, Frequency baud) {
    return static_cast<std::uint16_t>(
        periph_clock.to_hz() / baud.to_hz());
}

void configure_uart() {
    constexpr auto sysclk = 72.0_MHz;  // 注意：必须用浮点字面量
    constexpr auto baud = 115200_Hz;

    // USART1->BRR = compute_brr(sysclk, baud);
    // 生成的代码等价于直接写 USART1->BRR = 625;

    constexpr auto brr = compute_brr(sysclk, baud);
    static_assert(brr == 625, "BRR calculation mismatch");
}
```

### Memory Size and Static Assertions

```cpp
struct Bytes {
    std::uint64_t value;
    constexpr std::uint64_t to_bytes() const { return value; }
};

constexpr Bytes operator""_KiB(unsigned long long v) {
    return Bytes{v * 1024};
}
constexpr Bytes operator""_MiB(unsigned long long v) {
    return Bytes{v * 1024 * 1024};
}

// 编译期资源检查
constexpr auto kFlashSize = 512_KiB;
constexpr auto kAppSize = 256_KiB;
constexpr auto kStackSize = 4_KiB;
constexpr auto kRamSize = 128_KiB;

static_assert(kAppSize.to_bytes() <= kFlashSize.to_bytes(),
    "Application too large for flash!");
static_assert(kStackSize.to_bytes() < kRamSize.to_bytes(),
    "Stack exceeds RAM!");
```

These `static_assert` catch resource allocation issues at compile time, rather than waiting until runtime to discover that RAM is insufficient.

### Register Address Literals

In bare-metal embedded development, register operations are very frequent. Although we typically use CMSIS-provided macros to access registers, a register address literal can improve readability when you need to define custom peripherals or quickly inspect addresses during debugging:

```cpp
struct RegisterAddress {
    std::uintptr_t addr;
};

constexpr RegisterAddress operator""_reg(unsigned long long v) {
    return RegisterAddress{static_cast<std::uintptr_t>(v)};
}

// 使用
void debug_example() {
    // STM32F103 USART1 基地址 = 0x40013800
    constexpr auto usart1_base = 0x40013800_reg;
    constexpr auto gpioa_base = 0x40010800_reg;

    // volatile auto* usart1_sr =
    //     reinterpret_cast<volatile std::uint32_t*>(usart1_base.addr);
}
```

------

## Exercise: Implement a Length Unit System

As an exercise for this article, try implementing a complete length unit system with the following features:

1. Define `_m`, `_km`, and `_mi` (miles) literals, using meters as the base unit
2. Support addition, subtraction, and scalar multiplication
3. Support dividing length by time to get velocity
4. Use `static_assert` to verify the correctness of compile-time calculations

Reference framework:

```cpp
#include <cstdint>

struct MeterTag {};
struct SecondTag {};
struct SpeedTag {};

template <typename T, typename Tag>
struct Quantity {
    T value;
    constexpr explicit Quantity(T v) : value(v) {}

    // TODO: 实现加、减、标量乘法、比较运算
};

using Length = Quantity<long double, MeterTag>;
using Duration = Quantity<long double, SecondTag>;
using Speed = Quantity<long double, SpeedTag>;

// TODO: 定义 _m, _km, _mi 字面量
// TODO: 定义 _s 字面量
// TODO: 实现 Length / Duration -> Speed

// 验证
void test() {
    constexpr auto marathon = 26.2_mi;     // 英里转米
    // constexpr auto pace = marathon / 4.0_h;  // 配速（米/小时）
    // 注意：需要先定义 _h 字面量才能使用

    // 提示：1 英里 = 1609.344 米
    static_assert(marathon.value > 42000.0);
}
```

This exercise will help you solidify the combined use of templates, operator overloading, `constexpr`, and UDLs. Once completed, you will have a lightweight unit system ready to use in your projects.

------

## Summary

In this article, we put the foundational knowledge of UDLs into practice. Through the combination of the `Quantity<T, UnitTag>` template, operator overloading, and literal operators, we built a type-safe physical unit system: lengths can be added to lengths, dividing length by time yields velocity, but lengths and times cannot be directly added — all of these checks happen at compile time, with zero runtime overhead.

In embedded scenarios, UDLs are particularly well-suited for frequency/baud rate literals (`72_MHz`, `115200_Hz`), memory size literals (`4_KiB`, `512_KiB`), and register address literals. These literals significantly improve the readability of bare-metal code, and when combined with `static_assert`, they can catch resource allocation errors at compile time.

This concludes chapter 11 on user-defined literals. UDL is a concise yet practical language feature — its syntax is not complex, but when used in the right scenarios, it can cause a qualitative leap in code clarity and safety.

## References

- [cppreference: User-defined literals](https://en.cppreference.com/w/cpp/language/user_literal)
- [Bjarne Stroustrup: The C++ Programming Language, Chapter 18.6](https://www.stroustrup.com/)
