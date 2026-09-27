---
chapter: 4
cpp_standard:
- 11
- 14
- 17
description: Implement a type-safe unit system with the phantom type pattern and C++17 argument deduction
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 4: enum class and Scoped Enums'
reading_time_minutes: 11
related:
- user-defined literal
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- 类型别名
title: 'Strong Typedefs: Type Safety That Prevents Mix-Ups'
translation:
  source: documents/vol2-modern-features/ch04-type-safety/02-strong-types.md
  source_hash: 394d14227ab939e80ee716d4d66dea81dbf4cd8fdad31dac585c1b9a70b56787
  translated_at: '2026-09-25T15:30:18+00:00'
  engine: anthropic
  token_count: 4700
---
# Strong Typedefs: Type Safety That Prevents Mix-Ups

We once saw a beautifully classic bug during a code review: a function with the signature `void set_rect(int width, int height)`, called as `set_rect(h, w)` — the arguments were swapped. The compiler raised not a single warning, because `width` and `height` are both `int` and the types match perfectly. And yet the rectangle on screen was crooked. The bug wasn't hard to fix, but it still felt like getting thoroughly screwed.

The root cause of this kind of bug: `typedef` and `using` create **type aliases**, not new types. After `using Width = int;` and `using Height = int;`, `Width` and `Height` are still the very same `int`, and the compiler won't help you tell them apart. To create types the compiler can genuinely distinguish, we need a technique known as the "strong typedef" (also called an opaque typedef or a phantom type).

In this chapter we start from the limitations of `typedef`, then implement a practical strong type wrapper, and finally use it to build a type-safe unit system.

## Step 1 — Understanding the Limitations of typedef / using

Let's start with a snippet to get a feel for just how "fragile" plain aliases are:

```cpp
using UserId = int;
using OrderId = int;

UserId uid = 42;
OrderId oid = 100;

// Everything below compiles without a single warning
uid = oid;           // Assigning an OrderId to a UserId? The compiler sees no problem
OrderId another = uid;  // Works the other way around, too

void process_order(OrderId id);
process_order(uid);   // Passing a UserId in? The compiler doesn't care

int total = uid + oid;  // Adding two IDs with "different semantics"? Go right ahead
```

The problem is plain: `using UserId = int` merely gives `int` a nickname. In the compiler's eyes, `UserId`, `OrderId`, and `int` are one and the same thing. Every operation that accepts `int` will happily take `UserId` or `OrderId` — even when it makes no semantic sense whatsoever.

In a large codebase this is a huge hazard. The longer a function's parameter list, and the more its parameters reuse the same underlying type, the higher the odds of a mistake. Worse, the compiler can't catch this kind of bug and unit tests may not cover it either — the only remaining line of defense is a human eyeball during code review, and the human eyeball happens to be worst of all at spotting problems that "look correct".

## Step 2 — The Phantom Type Pattern

The core idea of the solution is called the phantom type: use a template parameter that serves purely as a marker and takes up no actual space to distinguish different types.

```cpp
// Tag structs: they exist only to tell types apart; nothing needs implementing
struct WidthTag {};
struct HeightTag {};

// The strong type wrapper
template <typename Tag, typename Rep = int>
class StrongInt {
public:
    constexpr explicit StrongInt(Rep value) : value_(value) {}
    constexpr Rep get() const noexcept { return value_; }

private:
    Rep value_;
};

using Width  = StrongInt<WidthTag>;
using Height = StrongInt<HeightTag>;
```

Now `Width` and `Height` are two completely different types. The compiler will stop you from assigning one to the other:

```cpp
Width w(100);
Height h(200);

// h = w;          // Compile error! Can't assign a Width to a Height
// Width bad = h;  // Compile error!

void set_rect(Width w, Height h);
set_rect(h, w);    // Compile error! Argument types don't match
set_rect(Width(100), Height(200));  // OK
```

`WidthTag` and `HeightTag` are empty classes that occupy no storage (thanks to C++'s Empty Base Optimization, EBO). When the compiler generates code, `StrongInt<WidthTag>` and `StrongInt<HeightTag>` behave at runtime exactly like a raw `int` — zero extra overhead.

The essence of this pattern: **trading compile-time type information for zero runtime overhead**. All the type checking happens at compile time; at runtime it's just plain integer arithmetic.

Put the two styles side by side, and the same call `set_rect(h, w)` compiles like this:

![Compile results for the same set_rect(h, w) call: type aliases versus a strong type wrapper](./02-strong-types-wrapper.drawio)

## Step 3 — Building a Practical Strong Type Wrapper

The `StrongInt` above is too bare-bones. In real projects we usually need to support some arithmetic. Let's build a more practical version that supports the common operations: addition, subtraction, comparison, stream output, and the like.

```cpp
#include <cstdint>
#include <functional>
#include <iostream>
#include <type_traits>

/// @brief Strong integer wrapper
/// @tparam Tag   Phantom tag used to distinguish types
/// @tparam Rep   Underlying storage type
template <typename Tag, typename Rep = int>
class StrongInt {
public:
    using ValueType = Rep;

    // Construction
    constexpr explicit StrongInt(Rep value = Rep{}) : value_(value) {}

    // Get the underlying value
    constexpr Rep get() const noexcept { return value_; }

    // Increment / decrement
    constexpr StrongInt& operator++() noexcept { ++value_; return *this; }
    constexpr StrongInt operator++(int) noexcept {
        StrongInt tmp = *this;
        ++value_;
        return tmp;
    }
    constexpr StrongInt& operator--() noexcept { --value_; return *this; }
    constexpr StrongInt operator--(int) noexcept {
        StrongInt tmp = *this;
        --value_;
        return tmp;
    }

    // Compound assignment (same type)
    constexpr StrongInt& operator+=(const StrongInt& other) noexcept {
        value_ += other.value_;
        return *this;
    }
    constexpr StrongInt& operator-=(const StrongInt& other) noexcept {
        value_ -= other.value_;
        return *this;
    }

    // Arithmetic (same type)
    constexpr StrongInt operator+(const StrongInt& other) const noexcept {
        return StrongInt(value_ + other.value_);
    }
    constexpr StrongInt operator-(const StrongInt& other) const noexcept {
        return StrongInt(value_ - other.value_);
    }

    // Comparison
    constexpr bool operator==(const StrongInt& other) const noexcept {
        return value_ == other.value_;
    }
    constexpr bool operator!=(const StrongInt& other) const noexcept {
        return value_ != other.value_;
    }
    constexpr bool operator<(const StrongInt& other) const noexcept {
        return value_ < other.value_;
    }
    constexpr bool operator<=(const StrongInt& other) const noexcept {
        return value_ <= other.value_;
    }
    constexpr bool operator>(const StrongInt& other) const noexcept {
        return value_ > other.value_;
    }
    constexpr bool operator>=(const StrongInt& other) const noexcept {
        return value_ >= other.value_;
    }

private:
    Rep value_;
};

// Stream output (handy for debugging)
template <typename Tag, typename Rep>
std::ostream& operator<<(std::ostream& os, const StrongInt<Tag, Rep>& v)
{
    os << v.get();
    return os;
}
```

This `StrongInt` template covers the most common everyday needs: construction, extracting the value, addition and subtraction, comparison, and stream output. And every operation requires its operands to be **the same StrongInt specialization** — you can't add a `Width` to a `Height`, because their `Tag`s differ.

## Step 4 — A Type-Safe Unit System

Now let's use the strong type wrapper to build a type-safe system of physical units. This is one of the most classic applications of strong typedefs — using the type system to stop values of different physical quantities from being mixed up.

```cpp
// Tag definitions
struct MetersTag {};
struct KilometersTag {};
struct CelsiusTag {};
struct FahrenheitTag {};
struct SecondsTag {};
struct MillisecondsTag {};

// Type aliases
using Meters        = StrongInt<MetersTag, double>;
using Kilometers    = StrongInt<KilometersTag, double>;
using Celsius       = StrongInt<CelsiusTag, double>;
using Fahrenheit    = StrongInt<FahrenheitTag, double>;
using Seconds       = StrongInt<SecondsTag, double>;
using Milliseconds  = StrongInt<MillisecondsTag, int64_t>;

// Unit conversion functions
constexpr Kilometers to_kilometers(Meters m) noexcept
{
    return Kilometers(m.get() / 1000.0);
}

constexpr Meters to_meters(Kilometers km) noexcept
{
    return Meters(km.get() * 1000.0);
}

constexpr Milliseconds to_milliseconds(Seconds s) noexcept
{
    return Milliseconds(static_cast<int64_t>(s.get() * 1000.0));
}
```

In use:

```cpp
Meters distance(5000.0);
Kilometers km = to_kilometers(distance);
// km = distance;  // Compile error! No direct assignment

Seconds duration(2.5);
Milliseconds ms = to_milliseconds(duration);
// auto bad = distance + duration;  // Compile error! Meters and Seconds can't be added
```

That's the power of a type-safe unit system: the compiler intercepts every "physical quantity mismatch" error for you at compile time. You can't accidentally add meters to seconds, and you can't use a Celsius value as Fahrenheit.

Of course, the unit system in this example is still the simplified edition — a real physical unit system also has to handle dimensionless numbers, compound units (velocity = distance / time), and more. But the core idea is the same: use phantom types to separate different physical quantities at compile time, with zero runtime overhead.

## Step 5 — A Practical Case of Preventing Parameter Mix-Ups

Beyond physical units, strong types are also excellent at preventing parameter mix-ups. Consider a familiar scene: a business system littered with ID types.

```cpp
struct UserIdTag {};
struct OrderIdTag {};
struct ProductIdTag {};

using UserId    = StrongInt<UserIdTag, uint64_t>;
using OrderId   = StrongInt<OrderIdTag, uint64_t>;
using ProductId = StrongInt<ProductIdTag, uint64_t>;

class OrderService {
public:
    OrderId create_order(UserId user, ProductId product, int quantity)
    {
        // If the parameters get swapped, the compiler errors out right away
        return OrderId(next_id_++);
    }

    void cancel_order(OrderId id)
    {
        // Accepts only OrderId — not UserId or ProductId
    }

private:
    uint64_t next_id_ = 1;
};
```

```cpp
OrderService service;
UserId user(42);
ProductId product(100);
OrderId order(1);

service.create_order(user, product, 3);  // OK
// service.create_order(product, user, 3);  // Compile error!
// service.cancel_order(user);              // Compile error! UserId is not an OrderId
```

In large projects, primary keys, foreign keys, and all sorts of association IDs in database tables are `uint64_t`. Without strong types to separate them, it's easy for a caller to pass a `user_id` where an `order_id` belongs. We've seen this kind of bug make a production database execute the wrong delete operation — the cost of fixing it far exceeded the cost of introducing strong types.

## Step 6 — Simplifying Usage with C++17 CTAD

C++17 introduced Class Template Argument Deduction (CTAD), which spares us the trouble of spelling out template arguments explicitly. Our `StrongInt` takes two template parameters (`Tag` and `Rep`), and `Tag` can't be deduced — but we can still simplify construction with deduction guides:

```cpp
// Deduction guide for the Rep type
template <typename Tag>
StrongInt(Tag*) -> StrongInt<Tag, int>;

// In use, only the Tag needs to be spelled out
struct ScoreTag {};
using Score = StrongInt<ScoreTag, int>;

Score s(100);  // Direct construction — no need to write <ScoreTag, int>
```

Honestly, though, in our usage pattern strong types are nearly always consumed through `using` aliases, so CTAD doesn't buy much. What's genuinely useful is another C++17 feature — `if constexpr` and `auto` deduction make template code much more natural to write:

```cpp
template <typename Tag, typename Rep>
constexpr auto make_strong(Rep value)
{
    return StrongInt<Tag, Rep>(value);
}

// Usage
auto width = make_strong<WidthTag>(100);
// width has type StrongInt<WidthTag, int>, deduced automatically
```

## Embedded in Practice — Type Safety for Register Addresses

In embedded development, peripheral register addresses are usually represented as raw `uint32_t`. If register addresses from different peripherals get mixed up by accident, the consequence may be a write to the wrong register and erratic hardware behavior. Strong types can step in here:

```cpp
struct GpioRegTag {};
struct UartRegTag {};
struct SpiRegTag {};

using GpioRegAddr = StrongInt<GpioRegTag, uint32_t>;
using UartRegAddr = StrongInt<UartRegTag, uint32_t>;
using SpiRegAddr  = StrongInt<SpiRegTag, uint32_t>;

void gpio_write(GpioRegAddr addr, uint32_t value);
void uart_write(UartRegAddr addr, uint32_t value);

// gpio_write(UartRegAddr(0x40001000), 42);  // Compile error! Type mismatch
```

This pattern is extremely valuable in large embedded projects — when your chip has dozens of peripherals and hundreds of register addresses, a type-safe address system keeps you from writing to the wrong register. And the runtime overhead is zero: `StrongInt`'s `get()` gets inlined, and the generated code is identical to using a raw `uint32_t` directly.

## Recommended Existing Libraries

If you'd rather not maintain your own strong type framework, the community offers a few mature open-source libraries worth considering. Jonathan Müller's [NamedType](https://github.com/joboccara/NamedType) is the best known: it supports operator inheritance, functional-style operations, hashing, stream output, and more — a very comprehensive feature set. Boost also has [Boost.StrongTypes](https://github.com/boostorg/strong_typedef) (the experimental strong_typedef).

Our advice, though: if all you need is "distinguishing same-type parameters with different semantics", a hand-written `StrongInt` template is enough — under a hundred lines of code, fully under your control, no external dependencies. Only when you need more sophisticated features (operator inheritance, customized implicit conversion policies) is it worth pulling in a third-party library.

## References

- [foonathan.net: Emulating strong/opaque typedefs in C++](https://www.foonathan.net/2016/10/strong-typedefs/)
- [Fluent C++: Strong types by struct](https://www.fluentcpp.com/2018/04/06/strong-types-by-struct/)
- [NamedType (GitHub)](https://github.com/joboccara/NamedType)
- [C++ Core Guidelines: Type safety](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#prosafety-type-safety-profile)
