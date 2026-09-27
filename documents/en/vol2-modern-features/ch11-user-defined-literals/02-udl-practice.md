---
chapter: 11
cpp_standard:
- 14
- 17
description: Build a type-safe physical unit system with user-defined literals
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 11: User-Defined Literals: The Basics'
- 'Chapter 4: Strong Typedefs: Type Safety That Prevents Mix-Ups'
reading_time_minutes: 11
related:
- 'constexpr Basics: The Art of Compile-Time Evaluation'
tags:
- host
- cpp-modern
- intermediate
- 字面量
- 类型安全
title: 'UDL in Practice: A Type-Safe Unit System'
translation:
  source: documents/vol2-modern-features/ch11-user-defined-literals/02-udl-practice.md
  source_hash: ea08ccd6ae9b07bbb08409ab0736966da0b9250c4e00cfc6dac5658b5801691c
  translated_at: '2026-09-25T16:56:29+00:00'
  engine: anthropic
  token_count: 4400
---
# UDL in Practice: A Type-Safe Unit System

In the previous article we learned the basic syntax of user-defined literals — the various forms of `operator""`, the standard-library literals, and the naming rules. In this one we put that knowledge to work and build a genuinely useful **type-safe unit system**.

Our goal: `100_m + 500_m` returns a length, `100_m / 2_s` returns a speed, and `100_m + 50_s` fails to compile on the spot. All conversions happen at compile time, with zero runtime overhead.

Here is a diagram of how the whole system flows:

![UDL unit system: literals fold into strongly typed values; matching types pass, mismatched units are blocked](./02-udl-units.drawio)

------

## Step 1: The Length Unit System

Start with the simplest case: length. We define a generic “value with a unit” template, then define literals for the individual length units:

```cpp
#include <cstdint>
#include <type_traits>

/// Unit tag: distinguishes physical quantities of different kinds
struct MeterTag {};
struct SecondTag {};

/// A value carrying a unit
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

/// Scalar × unit (multiplication the other way around)
/// Note: this template requires the scalar type T to match Quantity's T exactly
/// To support type conversions as well, provide additional overloads
template <typename T, typename UnitTag>
constexpr Quantity<T, UnitTag> operator*(
    T scalar, Quantity<T, UnitTag> q) {
    return q * scalar;
}

/// Overload for integer scalar × long double Quantity
template <typename UnitTag>
constexpr Quantity<long double, UnitTag> operator*(
    int scalar, Quantity<long double, UnitTag> q) {
    return Quantity<long double, UnitTag>{q.value * scalar};
}
```

`Quantity<T, UnitTag>` is a template, and `UnitTag` is an empty tag type whose only job is to make quantities of different units into different types. `MeterTag` and `SecondTag` have no inheritance relationship at all, so `Quantity<double, MeterTag>` and `Quantity<double, SecondTag>` are entirely different types — there is no way to assign one to the other.

Now define the length type alias and its literals:

```cpp
using Length = Quantity<long double, MeterTag>;

// Literals: the meter is the base unit
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

// Integer versions
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
    constexpr auto d1 = 1.5_m;       // 1.5 meters
    constexpr auto d2 = 2.0_km;      // 2000 meters (note: 2_km would fail, because only the floating-point overload is defined)
    constexpr auto d3 = 100.0_cm;    // 1 meter
    constexpr auto d4 = 500.0_mm;    // 0.5 meters

    // Compile-time computation
    constexpr auto total = 1.0_km + 500.0_m;  // 1500 meters
    static_assert(total.value == 1500.0L);

    // Scalar multiplication (integers are now supported too)
    constexpr auto doubled = 2 * 100.0_m;  // 200 meters
    static_assert(doubled.value == 200.0L);

    // Type safety: you cannot add a length and a time
    // auto bad = 100_m + 50_s;  // compile error!
}
```

`1.0_km + 500.0_m` is computed at compile time as `1500.0_m`. Try to add a length to a time and the compiler rejects it on the spot — because `Quantity<long double, MeterTag>` and `Quantity<long double, SecondTag>` are different types.

------

## Step 2: Time and Speed Units

The length system works on its own, but the charm of physical computation lies in combining different units. Length divided by time gives speed — so we need `Quantity` to support this kind of cross-unit arithmetic:

```cpp
/// Speed tag
struct SpeedTag {};

using TimeDuration = Quantity<long double, SecondTag>;
using Speed = Quantity<long double, SpeedTag>;

// Time literals (the second is the base unit)
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

// Integer versions
constexpr TimeDuration operator""_s(unsigned long long v) {
    return TimeDuration{static_cast<long double>(v)};
}

constexpr TimeDuration operator""_ms(unsigned long long v) {
    return TimeDuration{static_cast<long double>(v) / 1000.0L};
}

/// Length / time = speed
constexpr Speed operator/(Length len, TimeDuration time) {
    return Speed{len.value / time.value};
}

/// Speed * time = length
constexpr Length operator*(Speed spd, TimeDuration time) {
    return Length{spd.value * time.value};
}

constexpr Length operator*(TimeDuration time, Speed spd) {
    return Length{spd.value * time.value};
}
```

Now we can do real physics:

```cpp
void test_physics() {
    // Speed = distance / time
    constexpr auto speed = 100.0_m / 10.0_s;   // 10 m/s
    static_assert(speed.value == 10.0L);

    // Distance = speed * time
    constexpr auto distance = speed * 60.0_s;   // 600 meters
    static_assert(distance.value == 600.0L);

    // Conversion: 36 km/h = 10 m/s
    constexpr auto v1 = 36.0_km / 1.0_h;       // 36000 / 3600 = 10 m/s
    static_assert(v1.value == 10.0L);

    // Type safety
    // auto bad = 100_m + 10_s;    // compile error: length + time
    // auto bad2 = 100_m * 10_s;   // compile error: length * time (undefined)
}
```

The beauty of this code is that the compiler does the unit checking for you — you cannot accidentally treat milliseconds as seconds, and you cannot add a speed to a distance.

------

## Step 3: Temperature Conversion Literals

Temperature is a special kind of physical quantity: different scales are not related by simple linear scaling — converting between Celsius and Fahrenheit involves an offset. That makes it a great use case for UDLs:

```cpp
struct TemperatureTag {};
using Temperature = Quantity<long double, TemperatureTag>;

// Celsius: stored with kelvin as the base
constexpr Temperature operator""_degC(long double v) {
    return Temperature{v + 273.15L};
}

// Fahrenheit -> kelvin
constexpr Temperature operator""_degF(long double v) {
    return Temperature{(v - 32.0L) * 5.0L / 9.0L + 273.15L};
}

// Kelvin
constexpr Temperature operator""_degK(long double v) {
    return Temperature{v};
}

// Helpers: convert from kelvin back to each scale
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
    constexpr auto t1 = 0.0_degC;     // freezing point: 273.15 K
    constexpr auto t2 = 100.0_degC;   // boiling point: 373.15 K
    constexpr auto t3 = 32.0_degF;    // freezing point (Fahrenheit): 273.15 K

    static_assert(to_kelvin(t1) == 273.15L);

    // Temperature differences can be subtracted (in kelvin space)
    constexpr auto delta = 10.0_degC - 0.0_degC;  // 10K
    static_assert(delta.value == 10.0L);

    // Celsius -> Fahrenheit
    constexpr auto body_temp = 37.0_degC;
    // to_fahrenheit(body_temp) ≈ 98.6°F
}
```

Here we use kelvin as the internal storage, and every literal converts to kelvin at construction. That is what lets temperature differences add and subtract correctly.

------

## Step 4: String-Processing Literals

UDLs are not limited to physical units. In general-purpose C++ development, string-processing literals are common as well:

```cpp
#include <string>
#include <string_view>
#include <algorithm>
#include <cctype>

/// Compile-time string hash — for efficient string comparison
constexpr std::uint32_t operator""_hash(
    const char* str, std::size_t len) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < len; ++i) {
        hash = (hash ^ static_cast<std::uint8_t>(str[i]))
             * 16777619u;
    }
    return hash;
}

/// Runtime uppercase conversion
std::string operator""_upper(const char* str, std::size_t len) {
    std::string result(str, len);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

/// Runtime whitespace trim
std::string operator""_trim(const char* str, std::size_t len) {
    std::string_view sv(str, len);
    while (!sv.empty() && std::isspace(sv.front())) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(sv.back())) sv.remove_suffix(1);
    return std::string(sv);
}

void test_string_literals() {
    constexpr auto id = "sensor_temp"_hash;   // compile-time integer
    auto upper = "hello world"_upper;          // "HELLO WORLD"
    auto trimmed = "  padded  "_trim;           // "padded"

    // For switch-case (more efficient than string comparison)
    constexpr auto cmd = "start"_hash;
    switch (cmd) {
        case "start"_hash:  /* start */ break;
        case "stop"_hash:   /* stop */ break;
        default: break;
    }
}
```

The string-hash literal is especially useful in embedded settings — you replace runtime string comparisons with integers generated at compile time, saving flash (no strings to store) and gaining speed (integer comparison vs. string comparison).

------

## Embedded in Practice

In embedded development, the most practical UDL use cases are frequency/baud-rate literals and register-address literals. Let's look at concrete examples.

### Frequency and Baud Rate

```cpp
#include <cstdint>

struct Frequency {
    std::uint32_t hz;

    constexpr std::uint32_t to_hz() const { return hz; }
    constexpr std::uint32_t to_khz() const { return hz / 1000; }

    /// Frequency to period (nanoseconds)
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

/// Baud-rate register calculation (STM32 USART)
constexpr std::uint16_t compute_brr(
    Frequency periph_clock, Frequency baud) {
    return static_cast<std::uint16_t>(
        periph_clock.to_hz() / baud.to_hz());
}

void configure_uart() {
    constexpr auto sysclk = 72.0_MHz;  // note: must use a floating-point literal
    constexpr auto baud = 115200_Hz;

    // USART1->BRR = compute_brr(sysclk, baud);
    // The generated code is equivalent to writing USART1->BRR = 625; directly

    constexpr auto brr = compute_brr(sysclk, baud);
    static_assert(brr == 625, "BRR calculation mismatch");
}
```

### Memory Sizes and Static Assertions

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

// Compile-time resource checks
constexpr auto kFlashSize = 512_KiB;
constexpr auto kAppSize = 256_KiB;
constexpr auto kStackSize = 4_KiB;
constexpr auto kRamSize = 128_KiB;

static_assert(kAppSize.to_bytes() <= kFlashSize.to_bytes(),
    "Application too large for flash!");
static_assert(kStackSize.to_bytes() < kRamSize.to_bytes(),
    "Stack exceeds RAM!");
```

These `static_assert`s catch resource-allocation problems at compile time, instead of finding out at runtime that you don't have enough RAM.

### Register Address Literals

In bare-metal embedded development you touch registers constantly. Registers are usually accessed through the macros CMSIS provides, but if you are writing a custom peripheral or want to inspect an address quickly while debugging, an address literal can improve readability:

```cpp
struct RegisterAddress {
    std::uintptr_t addr;
};

constexpr RegisterAddress operator""_reg(unsigned long long v) {
    return RegisterAddress{static_cast<std::uintptr_t>(v)};
}

// Usage
void debug_example() {
    // STM32F103 USART1 base address = 0x40013800
    constexpr auto usart1_base = 0x40013800_reg;
    constexpr auto gpioa_base = 0x40010800_reg;

    // volatile auto* usart1_sr =
    //     reinterpret_cast<volatile std::uint32_t*>(usart1_base.addr);
}
```

------

## Exercise: Implement a Length Unit System

As an exercise for this article, try implementing a complete length unit system yourself, with the following features:

1. Define three literals — `_m`, `_km`, and `_mi` (miles) — with the meter as the base unit
2. Support addition, subtraction, and scalar multiplication
3. Support dividing a length by a time to get a speed
4. Use `static_assert` to verify the correctness of compile-time computation

A skeleton to start from:

```cpp
#include <cstdint>

struct MeterTag {};
struct SecondTag {};
struct SpeedTag {};

template <typename T, typename Tag>
struct Quantity {
    T value;
    constexpr explicit Quantity(T v) : value(v) {}

    // TODO: implement addition, subtraction, scalar multiplication, and comparisons
};

using Length = Quantity<long double, MeterTag>;
using Duration = Quantity<long double, SecondTag>;
using Speed = Quantity<long double, SpeedTag>;

// TODO: define the _m, _km, _mi literals
// TODO: define the _s literal
// TODO: implement Length / Duration -> Speed

// Verification
void test() {
    constexpr auto marathon = 26.2_mi;     // miles to meters
    // constexpr auto pace = marathon / 4.0_h;  // pace (meters/hour)
    // note: you must define the _h literal before this works

    // hint: 1 mile = 1609.344 meters
    static_assert(marathon.value > 42000.0);
}
```

This exercise drills the combination of templates, operator overloading, `constexpr`, and UDLs. Once you finish it, you will have a lightweight unit system you can drop straight into a project.

------

## References

- [cppreference: User-defined literals](https://en.cppreference.com/w/cpp/language/user_literal)
- [Bjarne Stroustrup: The C++ Programming Language, Chapter 18.6](https://www.stroustrup.com/)
