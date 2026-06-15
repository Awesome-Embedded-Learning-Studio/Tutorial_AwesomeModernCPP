---
title: Object Size, Alignment, and Trivial Types
description: We explain `sizeof`/`alignof` and memory padding, the precise distinctions
  between trivial/trivially copyable/standard-layout, the decomposition of POD (Plain
  Old Data), when `memcpy` is safe, and aggregate initialization vs. C++20 designated
  initializers.
chapter: 7
order: 12
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- 容器
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
- 20
reading_time_minutes: 15
related:
- array：编译期固定大小的聚合容器
translation:
  source: documents/vol3-standard-library/12-object-size-and-trivial-types.md
  source_hash: 152da35221b5197e7ef3a825583be934ee6291a0739678f081a9e81d195efbd6
  translated_at: '2026-06-15T09:20:18.710978+00:00'
  engine: anthropic
  token_count: 1635
---
# Object Size, Alignment, and Trivial Types

When writing low-level code, interfacing with C APIs, or optimizing memory usage, we often get tangled in a string of obscure terms: `alignof`, `sizeof`, `trivial`, `trivially copyable`, `standard-layout`, aggregates... These concepts seem fragmented, but they are actually an interconnected map: they determine an object's memory representation, copy semantics, whether it can be safely `memcpy`-ed, ABI compatibility with C structs, and initialization flexibility. In this post, we will straighten them out.

## Size and Alignment: Why `sizeof` Isn't Always the Sum of Members

`sizeof` reports the number of bytes an object **occupies in memory** (complete object representation, including necessary padding), while `alignof` reports the type's **alignment constraint** — the starting address of the object must be an integer multiple of `alignof`. To ensure every member lands on its required alignment boundary, padding may be inserted between members, as well as at the end of the structure.

Let's look at a common example:

```cpp
struct Bad {
    char a;    // 1 byte
    // 3 bytes padding
    int b;     // 4 bytes
    char c;    // 1 byte
    // 3 bytes padding
};
```

If we swap the order, the padding increases:

```cpp
struct Worse {
    char a;    // 1 byte
    // 3 bytes padding
    char c;    // 1 byte
    // 3 bytes padding
    int b;     // 4 bytes
};
```

If we put the two `char`s together, we save padding:

```cpp
struct Good {
    char a;    // 1 byte
    char c;    // 1 byte
    // 2 bytes padding
    int b;     // 4 bytes
};
```

The same members, just reordered: `Bad` takes 12 bytes, `Good` takes only 8 bytes — this is where the "arrange member order to save memory" rule comes from. The overall alignment of a structure is the **maximum alignment** among its members. The compiler also adds padding at the end to ensure `sizeof` is a multiple of `alignof` (this affects the spacing of elements in an array).

We can use `alignas` to force a specific alignment, for example, specifying 16-byte alignment for a SIMD buffer:

```cpp
struct alignas(16) Vec4 {
    float x, y, z, w;
};
```

Be careful with `alignas`: increasing alignment changes `sizeof` and the ABI. Placing an object at an unaligned address on hardware that requires aligned access can cause an immediate crash.

## trivial / trivially_copyable / standard-layout: Three Confusing Concepts

The C++ standard breaks down a set of "type properties" to precisely express "how objects of this type behave in memory." This is a design aspect of C++11 (splitting the historical POD concept into several distinct concerns). Let's first clarify the terms that are often confused:

- **trivial type**: Special member functions (default constructor, copy/move constructors, assignment, destructor) are all compiler-generated; there is no custom logic. In other words, construction/copy/destruction generates no runtime code — the object's bits are its entirety, with no hidden actions.
- **trivially_copyable type**: Can be safely copied byte-by-byte via `memcpy` (after copying, the destination has the same object representation and can be properly destroyed). **This is the criterion for whether `memcpy` can be used.**
- **standard-layout type**: Has predictable memory layout rules (members arranged in declaration order, no complex access control / virtual inheritance / multiple base classes causing uncertain layout). **This is the criterion for layout compatibility with C structs.**

A key fact: the old concept `POD` (Plain Old Data) was split in C++11 into `trivial` and `standard-layout`. `std::is_pod` is semantically just "both trivial and standard-layout." Therefore, safety assumptions related to ABI and C interoperability are now checked using `std::is_trivially_copyable` and `std::is_standard_layout` respectively.

Here is an example connecting them:

```cpp
struct S {
    int x;
    float y;
};
static_assert(std::is_trivial_v<S>);              // true
static_assert(std::is_trivially_copyable_v<S>);   // true
static_assert(std::is_standard_layout_v<S>);      // true
```

Compare this with a non-trivial one:

```cpp
struct T {
    int x;
    T(int v) : x(v) {} // User-defined constructor -> non-trivial
};
static_assert(!std::is_trivial_v<T>);             // true
static_assert(std::is_trivially_copyable_v<T>);   // true (can still memcpy)
static_assert(std::is_standard_layout_v<T>);      // true
```

To emphasize an easy mistake: **trivial ≠ trivially_copyable**. The former emphasizes the "triviality" of special members (especially the default constructor), while the latter emphasizes whether byte-wise copying is safe. To judge if you can `memcpy`, use `std::is_trivially_copyable`, not `std::is_trivial`.

## Let's Run: Testing Layout and Type Properties

Just talking about `alignof` and `sizeof` is too abstract. Let's use `static_assert` to nail these assumptions into compile-time, and then run it to see:

```cpp
struct A { char a; int b; char c; };
struct B { char a; char c; int b; };
struct C { char a; char c; char _pad[2]; int b; };
struct Vec4 { float x, y, z, w; };
struct S { int x; float y; };
struct T { int x; T(int v) : x(v) {} };

int main() {
    static_assert(sizeof(A) == 12);
    static_assert(sizeof(B) == 8);
    static_assert(sizeof(C) == 8);
    static_assert(sizeof(Vec4) == 16);
    static_assert(std::is_trivially_copyable_v<S>);
    static_assert(std::is_standard_layout_v<S>);
    static_assert(!std::is_trivial_v<T>);
}
```

All `static_assert`s pass (compilation success implies A=12, B=8, C=8, Vec4=16, S is both trivially copyable and standard-layout, T is non-trivial — all assumptions are correct). This is the correct way to use this knowledge: **write your assumptions about layout/types into code using `static_assert`**. If an assumption changes, the compiler stops you, which is much more reliable than comments.

## Aggregates and Designated Initializers: From Braces to C++20

An aggregate is a convenient type category: it allows direct initialization of members using braces (aggregate initialization), which is extremely intuitive when writing data descriptions (configuration structures, register maps), and naturally suitable for `constexpr`. Intuitively, an aggregate is a type with "no user-defined constructors, no virtual functions, all non-static members are public, and no base classes (or base classes meet standard-layout restrictions)" — the compiler can simply copy initialization values into the object representation in member order.

```cpp
struct Config {
    int baudrate = 115200;
    int timeout_ms = 1000;
};

Config cfg = { 9600, 500 }; // Aggregate initialization
```

C++20 introduced **designated initializers** (C had this long ago, C++20 finally adopted it formally), making aggregate initialization more readable and insensitive to member order:

```cpp
Config cfg2 = { .baudrate = 9600, .timeout_ms = 500 };
Config cfg3 = { .timeout_ms = 2000 }; // baudrate uses default
```

Nested structures and array indices can also be specified, which is particularly handy when initializing complex layouts (register tables, protocol headers):

```cpp
struct Mode { int mode; int flags; };
struct Regs { Mode mode; int prescaler[2]; };

Regs r = {
    .mode = { .mode = 1, .flags = 0 },
    .prescaler = { [0] = 10, [1] = 20 }
};
```

Note: Designated initializers only apply to **aggregate types**. Classes with user-defined constructors cannot use this syntax.

## Putting It All Together: Practical Principles for Type Properties

Let's string these points into a few actionable principles.

First, when defining data structures to interact with C or go through DMA (register maps, protocol headers, serialization formats), ensure they are **standard-layout** (predictable layout) and preferably **trivially_copyable** (can be `memcpy`-ed or `reinterpret_cast`-ed from a block of memory). Avoid virtual functions, private non-static members, and custom constructors/destructors/copies. Use `static_assert` at the interface to nail down these invariants:

```cpp
struct PacketHeader {
    uint32_t len;
    uint32_t seq;
    uint8_t  type;
};
static_assert(std::is_standard_layout_v<PacketHeader>);
static_assert(std::is_trivially_copyable_v<PacketHeader>);
```

Second, alignment affects `sizeof` and array layout. If hardware or DMA requires special alignment (16-byte cache line, SIMD), use `alignas` to specify it explicitly, and remember that it changes `sizeof` and the ABI.

Third, prefer braces and designated initializers for initialization. They are readable, resilient to member reordering, and often `constexpr`.

Fourth, copy semantics: **only types that are `trivially_copyable` can be safely `memcpy`-ed**. For classes with virtual functions, non-trivial destructors, or special members, do not perform binary copies; strictly use construction/copy/assignment.

## Summary

- `alignof` determines alignment requirements, `sizeof` reports actual occupation (including padding); arranging member order wisely saves padding.
- `trivial`, `trivially_copyable`, and `standard-layout` are the standard's fine-grained divisions of type properties: to `memcpy` check `trivially_copyable`, for C layout compatibility check `standard-layout`, `POD` = both trivial and standard-layout.
- Aggregate initialization is convenient; C++20 designated initializers are more readable and order-independent.
- Write assumptions about layout and types into code using `static_assert`, letting the compiler guard these invariants for you.

Want to try it out right now? Open the online example below (you can run it and view the assembly):

<OnlineCompilerDemo
  title="Object Size and Trivial Types: trivial / trivially_copyable / standard-layout"
  source-path="code/examples/vol3/12_object_size.cpp"
  description="Compile-time type traits queries, static_assert constraints, and sizeof costs of vptr and alignment"
  allow-run
/>

## Reference Resources

- [Type traits — cppreference](https://en.cppreference.com/w/cpp/header/type_traits)
- [Standard layout types — cppreference](https://en.cppreference.com/w/cpp/language/data_members#Standard_layout)
- [Designated initializers (C++20) — cppreference](https://en.cppreference.com/w/cpp/language/aggregate_initialization#Designated_initializers)
