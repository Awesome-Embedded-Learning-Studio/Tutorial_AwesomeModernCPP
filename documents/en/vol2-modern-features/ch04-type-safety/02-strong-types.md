---
chapter: 4
cpp_standard:
- 11
- 14
- 17
description: Implement a type-safe unit system using the phantom type pattern and
  C++17 argument deduction
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 4: enum class 与强类型枚举'
reading_time_minutes: 11
related:
- 用户自定义字面量
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- 类型别名
title: 'Strong Typedefs: Type Safety to Prevent Confusion'
translation:
  source: documents/vol2-modern-features/ch04-type-safety/02-strong-types.md
  source_hash: d3774e0b9e62180e6709b60347d88aa2aa28199efdc1c6a712a6daa9d1aef0e0
  translated_at: '2026-06-16T06:06:40.701659+00:00'
  engine: anthropic
  token_count: 2455
---
# Strong Typedefs: Type Safety to Prevent Confusion

## Introduction

During a code review, I once encountered a classic bug: a function signature was `void set_rect(int width, int height)`, but the caller wrote `set_rect(h, w)`—reversing the parameter order. The compiler issued no warnings because `width` and `height` are both `int`, so the types matched perfectly. However, the rectangle on the screen was distorted. This bug wasn't hard to fix, but it felt like a slap in the face.

The root cause of this bug is that `typedef` and `using` create **type aliases**, not new types. After `using Width = int;` and `using Height = int;`, `Width` and `Height` are still just `int`. The compiler does not distinguish between them. To create types that the compiler can truly distinguish, we need a technique called "strong typedef" (also known as opaque typedef or phantom type).

In this chapter, we start with the limitations of `typedef`, then implement a practical strong type wrapper, and finally use it to build a type-safe unit system.

## Step One — Understanding the Limitations of typedef / using

Let's look at a code snippet to see just how "fragile" ordinary aliases are:

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

The problem is clear: `using UserId = int` merely gives `int` a nickname. To the compiler, `UserId`, `OrderId`, and `int` are exactly the same thing. Any operation that accepts `int` can be performed with `UserId` or `OrderId`—even if it makes absolutely no sense semantically.

This poses a significant risk in large codebases. The longer the function parameter list and the more frequently the same underlying type is reused for parameters, the higher the probability of errors. Furthermore, the compiler cannot catch these bugs, and unit tests may not cover them, leaving them to be spotted only by human eyes during code review—yet humans are notoriously bad at catching these "looks correct" issues.

## Step 2 — The Phantom Type Pattern

The core idea behind the solution is called the **phantom type**: we use a template parameter that serves only as a tag and occupies no actual space to distinguish between different types.

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

Now `Width` and `Height` are two completely different types. The compiler will prevent us from assigning one to the other:

```cpp
Width w(100);
Height h(200);

// h = w;          // 编译错误！不能把 Width 赋给 Height
// Width bad = h;  // 编译错误！

void set_rect(Width w, Height h);
set_rect(h, w);    // 编译错误！参数类型不匹配
set_rect(Width(100), Height(200));  // OK
```

`WidthTag` and `HeightTag` are empty classes that occupy no storage space (thanks to C++ Empty Base Optimization, or EBO). When the compiler generates code, the runtime performance of `StrongInt<WidthTag>` and `StrongInt<HeightTag>` is identical to a raw `int`—zero overhead.

The essence of this pattern is: **trading compile-time type information for zero runtime overhead**. All type checking is performed during compilation, leaving only ordinary integer operations at runtime.

## Step 3 — Building a Practical Strong Type Wrapper

The `StrongInt` above is too basic. In real-world projects, we typically need to support arithmetic operations. Let's build a more practical version that supports common operations like addition, subtraction, comparison, and stream output.

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

This `StrongInt` template covers the most common requirements for daily use: construction, value retrieval, addition, subtraction, comparison, and stream output. Furthermore, all operations require operands to be **the same kind of `StrongInt` specialization**—we cannot add `Width` and `Height` because their `Tag` types differ.

## Step Four — A Type-Safe Unit System

Now, let's use strong type wrappers to build a type-safe system of physical units. This is one of the most classic application scenarios for strong typedefs—preventing values of different physical quantities from being mixed up via the type system.

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

Here is the translation:

**Usage:**

```cpp
Meters distance(5000.0);
Kilometers km = to_kilometers(distance);
// km = distance;  // 编译错误！不能直接赋值

Seconds duration(2.5);
Milliseconds ms = to_milliseconds(duration);
// auto bad = distance + duration;  // 编译错误！Meters 和 Seconds 不能相加
```

This demonstrates the power of a type-safe unit system: the compiler catches all "physical quantity mismatch" errors for you at compile time. You cannot accidentally add meters to seconds, nor mistake Celsius for Fahrenheit.

Of course, the unit system in this example is simplified—a real-world physical unit system would also need to handle dimensionless numbers, composite units (velocity = distance / time), and more. However, the core concept remains the same: use phantom types to distinguish between different physical quantities at compile time, with zero runtime overhead.

## Step 5 — Practical Case Study on Avoiding Parameter Confusion

Beyond physical units, strong types are also very useful for avoiding parameter confusion. Consider a common scenario: ID types are scattered throughout business logic systems.

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

In large-scale projects, primary keys, foreign keys, and various associated IDs in database tables are all `uint64_t`. Without strong type distinctions, it is easy for the caller to mistakenly pass a `user_id` where an `order_id` is expected. We have seen bugs of this nature cause incorrect deletion operations in production databases—the cost of fixing them is far higher than the cost of introducing strong types.

## Step 6 — Simplifying Usage with C++17 CTAD

C++17 introduced Class Template Argument Deduction (CTAD), which eliminates the need to explicitly specify template arguments. Although our `StrongInt` requires two template parameters (`Tag` and `Rep`), and `Tag` cannot be deduced, we can simplify construction by using deduction guides:

```cpp
// 对于 Rep 类型的推导指引
template <typename Tag>
StrongInt(Tag*) -> StrongInt<Tag, int>;

// 使用时只需要指定 Tag
struct ScoreTag {};
using Score = StrongInt<ScoreTag, int>;

Score s(100);  // 直接构造，不需要写 <ScoreTag, int>
```

However, in practice, we typically use strong types via `using` aliases, so CTAD isn't particularly useful in our usage pattern. What is truly useful is another C++17 feature—`if constexpr` and `auto` deduction make template code feel more natural:

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

In embedded development, we typically represent peripheral register addresses using raw `uint32_t` values. If register addresses from different peripherals are accidentally mixed up, the consequences could range from writing to the wrong register to causing abnormal hardware behavior. Strong typing can play a crucial role here:

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

This pattern is extremely valuable in large embedded projects. When your chip has dozens of peripherals and hundreds of register addresses, a type-safe address system prevents you from writing to the wrong register. Moreover, the runtime overhead is zero: the `get()` function of `StrongInt` will be inlined, and the generated code is identical to directly using `uint32_t`.

## Recommended Libraries

If you prefer not to maintain your own strong type framework, there are several mature open-source libraries in the community to consider. Jonathan Mueller's [NamedType](https://github.com/joboccara/NamedType) is the most well-known; it supports operator inheritance, functional operations, hashing, stream output, and more, offering a very comprehensive feature set. Boost also offers [Boost.StrongTypes](https://github.com/boostorg/strong_typedef) (the experimental `strong_typedef`).

However, my suggestion is: if your requirement is simply to "distinguish parameters of the same type but different semantics," hand-writing a simple `StrongInt` template is sufficient. It is less than one hundred lines of code, fully controllable, and has no external dependencies. You only need to introduce a third-party library when you require more complex features (such as operator inheritance or custom implicit conversion strategies).

## Summary

`typedef` and `using` only create type aliases; the compiler does not distinguish between them. The Phantom type pattern allows the compiler to distinguish values that are "semantically different but share an underlying type" at compile time by using a zero-size template tag parameter. The runtime overhead of strong type wrappers is zero—the empty tag class is optimized away by EBO (Empty Base Optimization), and all functions are inlined.

Type-safe unit systems and ID systems are the most typical application scenarios for strong types. The former prevents the mixing of different physical quantities, while the latter prevents confusing values that share the same underlying type but have different semantics. In the embedded field, strong types can also be used to distinguish register addresses of different peripherals, preventing accidental miswrites.

The next topic we will discuss, `std::variant`, solves a different problem (runtime polymorphism vs. compile-time type distinction), but it also falls under the broad theme of "using the type system to prevent errors."

## References

- [foonathan.net: Emulating strong/opaque typedefs in C++](https://www.foonathan.net/2016/10/strong-typedefs/)
- [Fluent C++: Strong types by struct](https://www.fluentcpp.com/2018/04/06/strong-types-by-struct/)
- [NamedType (GitHub)](https://github.com/joboccara/NamedType)
- [C++ Core Guidelines: Type safety](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#prosafety-type-safety-profile)
