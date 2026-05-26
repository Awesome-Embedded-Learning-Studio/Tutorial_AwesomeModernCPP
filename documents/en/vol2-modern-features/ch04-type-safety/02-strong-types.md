---
title: 'Strongly-Typed typedef: Type Safety to Prevent Confusion'
description: Implementing a type-safe unit system using the phantom type pattern and
  C++17 argument deduction
chapter: 4
order: 2
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- 类型别名
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
reading_time_minutes: 15
prerequisites:
- 'Chapter 4: enum class 与强类型枚举'
related:
- 用户自定义字面量
translation:
  source: documents/vol2-modern-features/ch04-type-safety/02-strong-types.md
  source_hash: 6711c85220cd71d1e56fe89eb2673231c1211165867dbf61a1252275ab56ac5e
  translated_at: '2026-05-26T11:27:29.716026+00:00'
  engine: anthropic
  token_count: 2459
---
# Strong Typedefs: Type Safety to Prevent Mix-ups

## Introduction

During a code review, we once came across a classic bug: a function signature was `void set_rect(int width, int height)`, but the caller wrote `set_rect(h, w)`—the parameter order was reversed. The compiler issued no warnings because both `width` and `height` were `int`, making the types a perfect match. But the rectangle on the screen was tilted. This bug wasn't hard to fix, but it felt like a massive slap in the face.

The root cause of this bug is that `typedef` and `using` only create **type aliases**, not new types. After `using Width = int;` and `using Height = int;`, `Width` and `Height` are still the exact same `int`, and the compiler won't distinguish between them. To truly create types that the compiler can differentiate, we need a technique called "strong typedef" (also known as opaque typedef or phantom type).

In this chapter, we start with the limitations of `typedef`, then implement a practical strong type wrapper, and finally use it to build a type-safe unit system.

## Step 1 — Understanding the Limitations of typedef / using

Let's look at some code to feel just how "fragile" a plain alias really is:

```cpp
using UserId = int;
using OrderId = int;

UserId uid = 42;
OrderId oid = 100;

// 以下全部编译通过，没有任何警告
uid = oid;           // OrderId 赋给 UserId？编译器觉得没问题
OrderId another = uid;  // 反过来也行

void process_order(OrderId id);
process_order(uid);   // 传了 UserId 进去？编译器不管

int total = uid + oid;  // 两个"不同语义"的 ID 相加？随便加
```

The problem is clear: `using UserId = int` is just giving `int` a nickname. In the compiler's eyes, `UserId`, `OrderId`, and `int` are the exact same thing. Any operation that accepts a `int` will also accept `UserId` and `OrderId`—even if it makes absolutely no sense semantically.

This is a massive hidden danger in large codebases. The longer a function's parameter list is, and the more parameters share the same underlying type, the higher the probability of an error. Furthermore, the compiler cannot catch these bugs, and unit tests might not cover them either. We can only rely on human eyes to spot them during code reviews—and human eyes are notoriously bad at catching issues that "look correct."

## Step 2 — The Phantom Type Pattern

The core idea behind the solution is called phantom type: we use a template parameter that serves only as a tag, taking up no actual space, to distinguish different types.

```cpp
// 标签结构体，只用来区分类型，不需要实现任何东西
struct WidthTag {};
struct HeightTag {};

// 强类型包装器
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

Now, `Width` and `Height` are two completely different types. The compiler will prevent you from assigning one to the other:

```cpp
Width w(100);
Height h(200);

// h = w;          // 编译错误！不能把 Width 赋给 Height
// Width bad = h;  // 编译错误！

void set_rect(Width w, Height h);
set_rect(h, w);    // 编译错误！参数类型不匹配
set_rect(Width(100), Height(200));  // OK
```

`WidthTag` and `HeightTag` are empty classes that occupy no storage space (thanks to C++'s EBO (Empty Base Optimization)). When generating code, the runtime behavior of `StrongInt<WidthTag>` and `StrongInt<HeightTag>` is exactly the same as a bare `int`—zero extra overhead.

The essence of this pattern is: **trading compile-time type information for zero runtime overhead**. All type checking is done at compile time, and at runtime, they are just plain integer operations.

## Step 3 — Building a Practical Strong Type Wrapper

The `StrongInt` above is too simplistic. In real projects, we usually need to support some arithmetic operations. Let's build a more practical version that supports common operations like addition, subtraction, comparison, and stream output.

```cpp
#include <cstdint>
#include <functional>
#include <iostream>
#include <type_traits>

/// @brief 强类型整数包装器
/// @tparam Tag   幽灵标签，用于区分不同类型
/// @tparam Rep   底层存储类型
template <typename Tag, typename Rep = int>
class StrongInt {
public:
    using ValueType = Rep;

    // 构造
    constexpr explicit StrongInt(Rep value = Rep{}) : value_(value) {}

    // 获取底层值
    constexpr Rep get() const noexcept { return value_; }

    // 自增/自减
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

    // 复合赋值（同类型）
    constexpr StrongInt& operator+=(const StrongInt& other) noexcept {
        value_ += other.value_;
        return *this;
    }
    constexpr StrongInt& operator-=(const StrongInt& other) noexcept {
        value_ -= other.value_;
        return *this;
    }

    // 算术运算（同类型）
    constexpr StrongInt operator+(const StrongInt& other) const noexcept {
        return StrongInt(value_ + other.value_);
    }
    constexpr StrongInt operator-(const StrongInt& other) const noexcept {
        return StrongInt(value_ - other.value_);
    }

    // 比较运算
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

// 流输出（方便调试）
template <typename Tag, typename Rep>
std::ostream& operator<<(std::ostream& os, const StrongInt<Tag, Rep>& v)
{
    os << v.get();
    return os;
}
```

This `StrongInt` template covers the most common daily needs: construction, value retrieval, addition, subtraction, comparison, and stream output. Moreover, all operations require the operands to be **the same StrongInt specialization**—you cannot add a `Width` and a `Height` because their `Tag` are different.

## Step 4 — A Type-Safe Unit System

Now let's use our strong type wrapper to build a type-safe physical unit system. This is one of the most classic use cases for strong typedefs—preventing values of different physical quantities from being mixed up through the type system.

```cpp
// 标签定义
struct MetersTag {};
struct KilometersTag {};
struct CelsiusTag {};
struct FahrenheitTag {};
struct SecondsTag {};
struct MillisecondsTag {};

// 类型别名
using Meters        = StrongInt<MetersTag, double>;
using Kilometers    = StrongInt<KilometersTag, double>;
using Celsius       = StrongInt<CelsiusTag, double>;
using Fahrenheit    = StrongInt<FahrenheitTag, double>;
using Seconds       = StrongInt<SecondsTag, double>;
using Milliseconds  = StrongInt<MillisecondsTag, int64_t>;

// 单位转换函数
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

Here is how we use it:

```cpp
Meters distance(5000.0);
Kilometers km = to_kilometers(distance);
// km = distance;  // 编译错误！不能直接赋值

Seconds duration(2.5);
Milliseconds ms = to_milliseconds(duration);
// auto bad = distance + duration;  // 编译错误！Meters 和 Seconds 不能相加
```

This demonstrates the power of a type-safe unit system: the compiler intercepts all "physical quantity mismatch" errors at compile time. You cannot accidentally add meters and seconds together, nor can you mistakenly use a Celsius value as a Fahrenheit one.

Of course, the unit system in this example is simplified—a real physical unit system would also need to handle dimensionless quantities, compound units (velocity = distance / time), and so on. But the core idea remains the same: use phantom types to distinguish different physical quantities at compile time, with zero runtime overhead.

## Step 5 — A Practical Case of Preventing Parameter Mix-ups

Beyond physical units, strong types are also extremely useful for preventing parameter mix-ups. Consider a common scenario: business systems are full of ID types.

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
        // 如果参数写反了，编译器会直接报错
        return OrderId(next_id_++);
    }

    void cancel_order(OrderId id)
    {
        // 只接受 OrderId，不接受 UserId 或 ProductId
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
// service.create_order(product, user, 3);  // 编译错误！
// service.cancel_order(user);              // 编译错误！UserId 不是 OrderId
```

In large projects, the primary keys, foreign keys, and various association IDs of database tables are all `uint64_t`. Without strong types to distinguish them, callers can easily pass a `user_id` where a `order_id` is expected. We have seen this kind of bug cause incorrect delete operations on production databases—the cost of fixing it is far higher than introducing strong types in the first place.

## Step 6 — Simplifying Usage with C++17 CTAD

C++17 introduced Class Template Argument Deduction (CTAD), which saves us the trouble of explicitly specifying template parameters. Although our `StrongInt` requires two template parameters (`Tag` and `Rep`), and `Tag` cannot be deduced, we can simplify construction through deduction guides:

```cpp
// 对于 Rep 类型的推导指引
template <typename Tag>
StrongInt(Tag*) -> StrongInt<Tag, int>;

// 使用时只需要指定 Tag
struct ScoreTag {};
using Score = StrongInt<ScoreTag, int>;

Score s(100);  // 直接构造，不需要写 <ScoreTag, int>
```

To be honest, in our usage pattern, strong types are typically used through `using` aliases, so CTAD doesn't actually help much. What is truly useful is another C++17 feature—`if constexpr` and `auto` deduction make template code feel more natural to write:

```cpp
template <typename Tag, typename Rep>
constexpr auto make_strong(Rep value)
{
    return StrongInt<Tag, Rep>(value);
}

// 使用
auto width = make_strong<WidthTag>(100);
// width 的类型是 StrongInt<WidthTag, int>，自动推导
```

## Embedded in Practice — Type Safety for Register Addresses

In embedded development, peripheral register addresses are usually represented as bare `uint32_t`s. If register addresses from different peripherals are accidentally mixed up, the consequence could be writing to the wrong register and causing abnormal hardware behavior. Strong types can be useful here:

```cpp
struct GpioRegTag {};
struct UartRegTag {};
struct SpiRegTag {};

using GpioRegAddr = StrongInt<GpioRegTag, uint32_t>;
using UartRegAddr = StrongInt<UartRegTag, uint32_t>;
using SpiRegAddr  = StrongInt<SpiRegTag, uint32_t>;

void gpio_write(GpioRegAddr addr, uint32_t value);
void uart_write(UartRegAddr addr, uint32_t value);

// gpio_write(UartRegAddr(0x40001000), 42);  // 编译错误！类型不匹配
```

This pattern is incredibly valuable in large embedded projects—when your chip has dozens of peripherals and hundreds of register addresses, a type-safe address system prevents you from writing to the wrong register. And the runtime overhead is zero: the `get()` function of `StrongInt` will be inlined, and the generated code is exactly the same as using a bare `uint32_t` directly.

## Recommended Existing Libraries

If you don't want to maintain your own strong type framework, there are a few mature open-source libraries in the community to consider. Jonathan Mueller's [NamedType](https://github.com/joboccara/NamedType) is the most well-known one; it supports operator inheritance, functional operations, hashing, stream output, and more, making it very comprehensive. Boost also has [Boost.StrongTypes](https://github.com/boostorg/strong_typedef) (an experimental strong_typedef).

However, our recommendation is: if your only need is to "distinguish same-type parameters with different semantics," hand-writing a simple `StrongInt` template is more than enough. The code is under one hundred lines, fully controllable, and has no external dependencies. You only need to introduce a third-party library when you require more complex features (such as operator inheritance or custom implicit conversion strategies).

## Summary

`typedef` and `using` only create type aliases, and the compiler won't distinguish between them. The phantom type pattern uses a zero-space template tag parameter to let the compiler distinguish values that are "semantically different but share the same underlying type" at compile time. The runtime overhead of a strong type wrapper is zero—empty tag classes are optimized away by EBO, and all functions are inlined.

Type-safe unit systems and ID systems are the most typical use cases for strong types. The former prevents different physical quantities from being mixed up, while the latter prevents values with the same underlying type but different semantics from being confused. In the embedded domain, strong types can also be used to distinguish register addresses of different peripherals, preventing accidental miswrites.

The `std::variant` we will discuss in the next article solves a different problem (runtime polymorphism vs. compile-time type distinction), but it同样 belongs to the broader theme of "using the type system to prevent errors."

## References

- [foonathan.net: Emulating strong/opaque typedefs in C++](https://www.foonathan.net/2016/10/strong-typedefs/)
- [Fluent C++: Strong types by struct](https://www.fluentcpp.com/2018/04/06/strong-types-by-struct/)
- [NamedType (GitHub)](https://github.com/joboccara/NamedType)
- [C++ Core Guidelines: Type safety](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#prosafety-type-safety-profile)
