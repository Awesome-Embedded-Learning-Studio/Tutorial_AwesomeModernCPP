---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: We dive deep into `sizeof`/`alignof` and memory padding, the precise
  distinctions between `trivial`/`trivially_copyable`/`standard-layout`, the decomposition
  of POD (Plain Old Data), when `memcpy` is safe, and aggregate initialization vs.
  C++20 designated initializers.
difficulty: intermediate
order: 12
platform: host
reading_time_minutes: 8
related:
- array：编译期固定大小的聚合容器
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- 容器
title: Object Size, Alignment, and Trivial Types
translation:
  source: documents/vol3-standard-library/containers/12-object-size-and-trivial-types.md
  source_hash: 3ca268f268e28766e688401c6a11c4f3cb581927bdf8432bf2e7689d9afe9c7e
  translated_at: '2026-06-16T06:14:07.042774+00:00'
  engine: anthropic
  token_count: 1631
---
# Object Size, Alignment, and Trivial Types

When writing low-level code, interacting with C interfaces, or optimizing memory usage, we often get confused by a string of obscure terms: `sizeof`, `alignof`, `alignas`, `trivial`, `standard-layout`, `trivially_copyable`, aggregate... These concepts may seem fragmented, but they are actually part of an interconnected map: they determine an object's memory representation, copy semantics, whether it is safe to use `memcpy`, ABI compatibility with C structs, and initialization flexibility. In this article, we will sort them out.

## Size and Alignment: Why sizeof Is Not Always the Sum of Members

`sizeof(T)` reports the number of bytes an object **occupies in memory** (the complete object representation, including necessary padding), while `alignof(T)` reports the type's **alignment constraint**—the starting address of the object must be a multiple of `alignof(T)`. To ensure each member lands on its required alignment boundary, padding may be inserted between members and at the end of the structure.

Let's look at a common example:

```cpp
struct A {
    char c;   // 1 字节，offset 0
    int  i;   // 4 字节，对齐 4，offset 4
};
// offset 0: c，offset 1..3: 填充，offset 4..7: i
// sizeof(A) == 8
```

If we swap the order, the padding increases:

```cpp
struct B {
    char a;   // offset 0
    int  i;   // offset 4（前面填 3 字节）
    char b;   // offset 8
};
// 尾部还要填 3 字节，让 sizeof 是 alignof(B)=4 的倍数
// sizeof(B) == 12
```

Placing two `char` variables together saves padding:

```cpp
struct C {
    char a;   // offset 0
    char b;   // offset 1
    int  i;   // offset 4（前面填 2 字节）
};
// sizeof(C) == 8
```

The members are identical, but the declaration order differs: `B` occupies 12 bytes, while `C` occupies only 8 bytes. This demonstrates how "arranging member order saves memory". The alignment of a struct is the **maximum alignment** among its members. The compiler also adds padding at the end to ensure that `sizeof(T)` is a multiple of `alignof(T)` (this affects the spacing of elements in an array).

We can use `alignas` to forcibly change alignment, for example, specifying 16-byte alignment for a SIMD buffer:

```cpp
struct alignas(16) Vec4 {
    float x, y, z, w;   // sizeof == 16，alignof == 16
};
```

Be careful with `alignas`: increasing alignment can change `sizeof` and the ABI. Placing an object at an unaligned address on hardware that requires aligned access may cause an immediate crash.

## trivial / trivially_copyable / standard-layout: Three easily confused concepts

The C++ standard breaks down a set of "type properties" to precisely express "how objects of this type behave in memory." This design was introduced in C++11 (splitting the historical concept of POD into several distinct aspects). Let's clarify a few terms that are often confused:

- **trivial type**: Special member functions (default constructor, copy/move constructors, assignment, destructor) are all compiler-generated without custom logic. In other words, construction, copying, or destruction produces no runtime code—the object's bit pattern is its entirety, with no hidden actions.
- **trivially_copyable type**: Can be safely copied byte-by-byte using `memcpy` (the resulting copy has the same object representation and can be properly destroyed). **This is the criterion for whether `memcpy` can be used.**
- **standard-layout type**: Has predictable memory layout rules (members are arranged in declaration order, without complex access control, virtual inheritance, or multiple base classes causing layout ambiguity). **This is the criterion for compatibility with C struct layout.**

A key fact: the old concept `POD` (Plain Old Data) was split in C++11 into `trivial` and `standard-layout`. Semantically, `POD` simply means "both trivial and standard-layout." Therefore, for safety assumptions related to ABI and C interoperability, we now use `std::is_standard_layout_v<T>` and `std::is_trivially_copyable_v<T>` for separate checks.

Here is an example that ties these concepts together:

```cpp
struct S {
    int    x;
    double y;
    // 没有用户定义构造/析构/拷贝、没有虚函数、没有基类
};
// S 通常是 trivial、trivially_copyable、standard-layout -> POD
static_assert(std::is_trivially_copyable_v<S>);
static_assert(std::is_standard_layout_v<S>);
```

Let's compare a non-trivial example:

```cpp
struct T {
    T() { /* 自定义构造 */ }
    int x;
};
// T 不是 trivial（用户定义了构造），通常也不是 trivially_copyable
static_assert(!std::is_trivial_v<T>);
```

Let's reiterate one common pitfall: **trivial ≠ trivially_copyable**. The former emphasizes the "triviality" of special member functions (especially the default constructor), while the latter emphasizes whether copying by bytes is safe. To determine if we can use `memcpy`, we use `std::is_trivially_copyable_v<T>`, not `is_trivial`.

## Let's Run It: Testing Layout and Type Properties

Simply stating that `sizeof(B)==12` and `sizeof(C)==8` is too abstract. Let's use `static_assert` to pin these assumptions into the compile phase, and then run the code to see the results:

```cpp
#include <type_traits>
#include <cstdint>
#include <iostream>

struct A { char c; int i; };
struct B { char a; int i; char b; };
struct C { char a; char b; int i; };
struct alignas(16) Vec4 { float x, y, z, w; };
struct S { int x; double y; };
struct T { T() {} int x; };

static_assert(sizeof(A) == 8);
static_assert(sizeof(B) == 12);
static_assert(sizeof(C) == 8);
static_assert(sizeof(Vec4) == 16 && alignof(Vec4) == 16);
static_assert(std::is_trivially_copyable_v<S> && std::is_standard_layout_v<S>);
static_assert(!std::is_trivial_v<T>);

int main()
{
    std::cout << "sizeof(A)=" << sizeof(A) << " sizeof(B)=" << sizeof(B)
              << " sizeof(C)=" << sizeof(C) << " sizeof(Vec4)=" << sizeof(Vec4) << '\n';
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/object_size_test /tmp/object_size_test.cpp && /tmp/object_size_test
```

```text
sizeof(A)=8 sizeof(B)=12 sizeof(C)=8 sizeof(Vec4)=16
```

All `static_assert` checks pass (compilation success confirms that A=8, B=12, C=8, Vec4=16, S is both trivially copyable and standard layout, and T is non-trivial—meaning all these assumptions are correct). This demonstrates the proper way to apply this knowledge: **encode your assumptions about layout or types using `static_assert`**. If an assumption changes, the compiler will stop you, which is far more reliable than a comment.

## Aggregates and Designated Initialization: From Braces to C++20

An aggregate is a convenient category of type: it allows direct member initialization using braces (aggregate initialization). This is extremely intuitive when writing data descriptors (configuration structures, register maps), and it is naturally suited for `constexpr`. Intuitively, an aggregate is a type that "has no user-defined constructors, no virtual functions, all non-static members are public, and no base classes (or satisfies standard layout restrictions)"—the compiler can simply copy initialization values into the object representation in member order.

```cpp
struct Point { int x, y; };
Point p1{1, 2};    // 聚合初始化，成员按声明顺序赋值

struct Config { int baud; int parity; int stop_bits; };
constexpr Config default_cfg{115200, 0, 1};   // 还能 constexpr
```

C++20 introduced **designated initializers** (a feature long present in C, but only officially adopted in C++20), making aggregate initialization more readable and insensitive to member order:

```cpp
struct S { int a, b, c; };
S s1{.b = 2, .a = 1, .c = 3};   // 成员顺序无所谓
S s2{.a = 1};                   // 只初始化 a，其余默认/零初始化
```

Nested structures and array subscripts can also be specified, which is particularly handy when initializing complex layouts like register maps or protocol headers:

```cpp
struct Header { uint16_t id; uint16_t flags; };
struct Packet { Header hdr; uint8_t payload[8]; };

Packet pkt{
    .hdr     = {.id = 0x1234, .flags = 0x1},
    .payload = {[0] = 0xAA, [3] = 0x55}   // 只给第 0、3 个元素赋值
};
```

> **Note:** Designated initializers only apply to **aggregate types**. Classes with user-defined constructors cannot use this syntax.

## Putting It into Practice: Practical Principles for Type Properties

Let's synthesize these points into actionable principles. First, when defining data structures that interact with C or use DMA (register maps, protocol headers, serialization formats), ensure they are **standard-layout** (layout is predictable) and preferably **trivially_copyable** (can be used with `memcpy` or `reinterpret_cast` on a block of memory). Avoid virtual functions, avoid private non-static members, and do not write custom constructors, destructors, or copy operations. Finally, use `static_assert` at the interface to pin down these invariants:

```cpp
static_assert(std::is_standard_layout_v<MyRegs>);
static_assert(std::is_trivially_copyable_v<MyRegs>);
```

Second, alignment affects `sizeof` and array layout. If hardware or DMA requires specific alignment (e.g., 16-byte cache lines or SIMD), use `alignas` to specify it explicitly, and remember that it changes `sizeof` and the ABI.

Third, prefer brace initialization and designated initializers. They are readable, robust against member reordering, and often `constexpr`.

Fourth, copy semantics: **Only types that are `trivially_copyable` can be safely `memcpy`'d (`memcpy(&dst, &src, sizeof(T))`)**. For classes with virtual functions, non-trivial destructors, or special member functions, do not perform binary copies; use constructors, copy constructors, or assignment operators properly.

## Summary

- `alignof` determines alignment requirements, while `sizeof` reports the actual size occupied (including padding); arranging member order wisely can save padding.
- `trivial`, `trivially_copyable`, and `standard-layout` are the standard's fine-grained classifications of type properties: check `trivially_copyable` for `memcpy`, check `standard-layout` for C layout compatibility, and `POD` means both trivial and standard-layout.
- Aggregate initialization is convenient; C++20 designated initializers are more readable and independent of member order.
- Encode assumptions about layout and types using `static_assert` to let the compiler enforce these invariants for you.

Want to see the results in action immediately? Open the online example below (you can run it and view the assembly):

<OnlineCompilerDemo
  title="Object Size and Trivial Types: trivial / trivially_copyable / standard-layout"
  source-path="code/examples/vol3/12_object_size.cpp"
  description="Compile-time type_traits for checking type properties, static_assert for constraints, and sizeof costs of vptr and alignment"
  allow-run
/>

## References

- [Type traits — cppreference](https://en.cppreference.com/w/cpp/header/type_traits)
- [Standard layout type — cppreference](https://en.cppreference.com/w/cpp/language/data_members#Standard_layout)
- [Designated initializers (C++20) — cppreference](https://en.cppreference.com/w/cpp/language/aggregate_initialization#Designated_initializers)
