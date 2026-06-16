---
chapter: 11
cpp_standard:
- 14
- 17
description: Implement a type-safe physical unit system using user-defined literals
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 11: 用户自定义字面量基础'
- 'Chapter 4: 强类型 typedef'
reading_time_minutes: 11
related:
- constexpr 基础
tags:
- host
- cpp-modern
- intermediate
- 字面量
- 类型安全
title: 'UDL in Action: A Type-Safe Unit System'
translation:
  source: documents/vol2-modern-features/ch11-user-defined-literals/02-udl-practice.md
  source_hash: 8be75b11b86a99c9f61e3fce9e66c83545fb5ecc769f764b97fc8a45b49090c2
  translated_at: '2026-06-16T03:59:58.309652+00:00'
  engine: anthropic
  token_count: 3058
---
# UDL in Practice: A Type-Safe Unit System

In the previous post, we covered the basic syntax of user-defined literals—the various forms of `operator""`, standard library literals, and naming rules. In this post, we will put this knowledge into practice by building a truly practical **type-safe unit system**.

Our goal is to make `10_m` return a length, `100_km_h` return a velocity, and cause `10_m + 5_s` to fail compilation directly. All conversions happen at compile time, with zero runtime overhead.

------

## Step 1: Length Unit System

Let's start with the simplest length units. We use a template to define a generic "value with unit," and then define literals for different length units:

```cpp
// Tag types for different units
struct MeterTag {};
struct SecondTag {};

// Generic value wrapper
template<typename UnitTag, typename ValueType = double>
class PhysicalQuantity {
public:
    constexpr explicit PhysicalQuantity(ValueType value) : value_(value) {}

    constexpr ValueType value() const { return value_; }

private:
    ValueType value_;
};

// Literals
constexpr PhysicalQuantity<MeterTag> operator""_m(long double v) {
    return PhysicalQuantity<MeterTag>{static_cast<double>(v)};
}

constexpr PhysicalQuantity<MeterTag> operator""_km(long double v) {
    return PhysicalQuantity<MeterTag>{static_cast<double>(v * 1000.0)};
}
```

`PhysicalQuantity` is a template, and `MeterTag` is an empty tag type whose sole purpose is to make physical quantities of different units into different types. There is no inheritance relationship between `MeterTag` and `SecondTag`, so `PhysicalQuantity<MeterTag>` and `PhysicalQuantity<SecondTag>` are completely distinct types—you cannot assign one to the other.

Now, let's define length type aliases and literals:

```cpp
using Length = PhysicalQuantity<MeterTag>;
using Time = PhysicalQuantity<SecondTag>;

constexpr Length operator""_m(long double v) {
    return Length{static_cast<double>(v)};
}

constexpr Length operator""_km(long double v) {
    return Length{static_cast<double>(v * 1000.0)};
}

constexpr Time operator""_s(long double v) {
    return Time{static_cast<double>(v)};
}

constexpr Time operator""_ms(long double v) {
    return Time{static_cast<double>(v / 1000.0)};
}
```

Let's test it:

```cpp
constexpr auto d1 = 10.0_m;      // 10 meters
constexpr auto d2 = 1.0_km;      // 1000 meters
constexpr auto total = d1 + d2;  // 1010 meters
```

`d1 + d2` is calculated at compile time as `1010.0`. If you try to add a length and a time, the compiler will directly report an error—because `Length` and `Time` are different types.

------

## Step 2: Time and Velocity Units

The length system can work independently, but the charm of physical calculation lies in combining different units. Length divided by time yields velocity—we need to make `PhysicalQuantity` support this cross-unit arithmetic:

```cpp
template<typename U1, typename U2>
auto operator+(const PhysicalQuantity<U1>& a, const PhysicalQuantity<U2>& b)
    -> PhysicalQuantity<U1> {
    static_assert(std::is_same_v<U1, U2>, "Unit mismatch");
    return PhysicalQuantity<U1>(a.value() + b.value());
}

template<typename U1, typename U2>
auto operator/(const PhysicalQuantity<U1>& a, const PhysicalQuantity<U2>& b) {
    using ResultTag = /* ... tag logic ... */;
    return PhysicalQuantity<ResultTag>(a.value() / b.value());
}
```

Now we can perform physical calculations:

```cpp
constexpr auto distance = 100.0_km;
constexpr auto duration = 2.0_h;
constexpr auto speed = distance / duration;  // Result type: Velocity
```

The beauty of this code is that the compiler performs the unit check for you—you cannot accidentally use milliseconds as seconds, nor can you add velocity to distance.

------

## Step 3: Temperature Conversion Literals

Temperature is a special physical quantity because conversions between different scales are not simple linear scaling—conversion between Celsius and Fahrenheit involves an offset. This is a perfect use case for UDLs:

```cpp
struct KelvinTag {};

class Temperature {
public:
    constexpr Temperature(double kelvin) : kelvin_(kelvin) {}

    constexpr double toCelsius() const { return kelvin_ - 273.15; }
    constexpr double toFahrenheit() const { return kelvin_ * 9/5 - 459.67; }

private:
    double kelvin_;
};

constexpr Temperature operator""_C(long double c) {
    return Temperature(static_cast<double>(c + 273.15));
}

constexpr Temperature operator""_F(long double f) {
    return Temperature(static_cast<double>((f + 459.67) * 5/9));
}
```

Usage:

```cpp
constexpr auto room_temp = 25.0_C;
constexpr auto boiling = 212.0_F;
constexpr auto diff = boiling.toCelsius() - room_temp.toCelsius();
```

Here we use Kelvin as the internal storage; all literals are converted to Kelvin upon construction. This ensures temperature differences can be added and subtracted correctly.

------

## Step 4: String Processing Literals

UDLs are not limited to physical units. In general C++ development, string processing literals are also quite common:

```cpp
constexpr std::size_t operator""_hash(const char* str, std::size_t len) {
    std::size_t hash = 5381;
    for (std::size_t i = 0; i < len; ++i) {
        hash = ((hash << 5) + hash) + str[i];  // hash * 33 + c
    }
    return hash;
}

// Usage
switch (event_type) {
    case "start"_hash: /* ... */ break;
    case "stop"_hash:  /* ... */ break;
}
```

String hash literals are particularly useful in embedded scenarios—you can replace runtime string comparisons with integers generated at compile time, saving Flash (no need to store strings) and improving performance (integer comparison vs string comparison).

------

## Embedded Practice

In embedded development, the most practical scenarios for UDLs are frequency/baud rate literals and register address literals. Let's look at specific examples.

### Frequency and Baud Rate

```cpp
struct HertzTag {};
using Frequency = PhysicalQuantity<HertzTag, uint32_t>;

constexpr Frequency operator""_Hz(unsigned long long v) {
    return Frequency{static_cast<uint32_t>(v)};
}

constexpr Frequency operator""_kHz(unsigned long long v) {
    return Frequency{static_cast<uint32_t>(v * 1000)};
}

constexpr Frequency operator""_MHz(unsigned long long v) {
    return Frequency{static_cast<uint32_t>(v * 1000 * 1000)};
}

// Usage
UART_Init(115200_Hz);
I2C_Init(400_kHz);
```

### Memory Size and Static Assertions

```cpp
struct ByteTag {};
using Bytes = PhysicalQuantity<ByteTag, std::size_t>;

constexpr Bytes operator""_B(unsigned long long v) {
    return Bytes{v};
}

constexpr Bytes operator""_KB(unsigned long long v) {
    return Bytes{v * 1024};
}

// Compile-time check
static_assert(32_KB.value() < FLASH_SIZE, "Exceeds Flash size");
```

These `static_assert` statements can catch resource allocation issues at compile time, rather than waiting for runtime to discover insufficient RAM.

### Register Address Literals

In embedded bare-metal development, register operations are very frequent. While CMSIS-provided macros are typically used to access registers, if you need to customize peripherals or quickly check addresses during debugging, an address literal can improve readability:

```cpp
struct AddressTag {
    constexpr explicit AddressTag(uintptr_t addr) : addr_(addr) {}
    constexpr uintptr_t addr() const { return addr_; }
private:
    uintptr_t addr_;
};

constexpr AddressTag operator""_addr(unsigned long long v) {
    return AddressTag{static_cast<uintptr_t>(v)};
}

// Usage
volatile uint32_t& gpio_base = *reinterpret_cast<uint32_t*>(0x40020000_addr);
```

------

## Exercise: Implement a Length Unit System

As an exercise for this post, try to implement a complete length unit system yourself, including the following features:

1. Define `_m`, `_cm`, `_mile` (mile) literals, using meters as the base unit
2. Support addition/subtraction and scalar multiplication
3. Support dividing length by time to get velocity
4. Use `static_assert` to verify the correctness of compile-time calculations

Reference framework:

```cpp
// TODO: Define tag types
struct MeterTag {};
// ...

// TODO: Define Quantity template
template<typename Tag>
class Quantity { /* ... */ };

// TODO: Implement literals
constexpr Quantity<MeterTag> operator""_m(long double v);
// ...

// TODO: Implement operators
template<typename T>
auto operator*(const Quantity<T>& q, double scalar) { /* ... */ }
```

This exercise will help you consolidate the combination of templates, operator overloading, `constexpr`, and UDLs. Once completed, you will have a lightweight unit system ready for use in your projects.

------

## Summary

In this post, we applied the basics of UDLs to a practical scenario. Through the combination of a `PhysicalQuantity` template, operator overloading, and literal operators, we built a type-safe physical unit system: lengths can be added to lengths, length divided by time yields velocity, but length and time cannot be added directly—all these checks happen at compile time with zero runtime overhead.

In embedded scenarios, UDLs are particularly suitable for frequency/baud rate literals (`100_Hz`, `115200_baud`), memory size literals (`4_KB`, `32_MB`), and register address literals. These literals significantly improve the readability of bare-metal code, and combined with `static_assert`, can catch resource allocation errors at compile time.

This concludes chapter 11 on user-defined literals. UDL is a concise yet practical language feature—its syntax is not complex, but when used in the right context, it can dramatically improve code clarity and safety.

## Reference Resources

- [cppreference: User-defined literals](https://en.cppreference.com/w/cpp/language/user_literal)
- [Bjarne Stroustrup: The C++ Programming Language, Chapter 18.6](https://www.stroustrup.com/)
