---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: 'A thorough look at sizeof/alignof and memory padding, the precise distinctions
  among trivial/trivially_copyable/standard-layout, how POD was split apart, when memcpy
  is safe, aggregate initialization, and C++20 designated initializers'
difficulty: intermediate
order: 12
platform: host
reading_time_minutes: 8
related:
- 'array: An Aggregate Container with a Compile-Time Fixed Size'
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- 容器
title: Object Size, Alignment, and Trivial Types
translation:
  source: documents/vol3-standard-library/containers/12-object-size-and-trivial-types.md
  source_hash: 1390202af18e72b82da3ed11a81c8e9c7228cdc1fe5fc43c5fcf036ec67a657a
  translated_at: '2026-09-26T02:42:41+00:00'
  engine: anthropic
  token_count: 5000
---
# Object Size, Alignment, and Trivial Types

When we write low-level code, talk to C interfaces, or optimize memory footprint, a string of seemingly obscure terms tends to leave us spinning: `sizeof`, `alignof`, `alignas`, `trivial`, `standard-layout`, `trivially_copyable`, aggregate… These concepts look scattered, but they actually form one interlocking map: they decide an object's memory representation, its copy semantics, whether you can safely `memcpy` it, whether it is ABI-compatible with a C struct, and how flexible its initialization can be. This chapter untangles them.

## Size and Alignment: Why sizeof Is Not Always the Sum of Members

`sizeof(T)` reports the number of bytes an object **occupies in memory** (the complete object representation, necessary padding included), while `alignof(T)` reports the type's **alignment constraint** — the object's starting address must be a multiple of `alignof(T)`. To land every member on the alignment it demands, padding may be needed between members and at the end of the struct.

Take the most common example:

```cpp
struct A {
    char c;   // 1 byte, offset 0
    int  i;   // 4 bytes, alignment 4, offset 4
};
// offset 0: c, offsets 1..3: padding, offsets 4..7: i
// sizeof(A) == 8
```

Shuffle the declaration order and the padding grows:

```cpp
struct B {
    char a;   // offset 0
    int  i;   // offset 4 (3 bytes of padding before it)
    char b;   // offset 8
};
// 3 more bytes of tail padding, so that sizeof is a multiple of alignof(B)=4
// sizeof(B) == 12
```

Put the two `char`s together and the padding is saved:

```cpp
struct C {
    char a;   // offset 0
    char b;   // offset 1
    int  i;   // offset 4 (2 bytes of padding before it)
};
// sizeof(C) == 8
```

The very same members, only declared in a different order, and `B` takes 12 bytes while `C` takes just 8 — this is where the advice to "arrange members sensibly to save memory" comes from. A struct's overall alignment is the **largest alignment** among its members, and the compiler also adds tail padding so that `sizeof(T)` is a multiple of `alignof(T)` (this is what keeps elements evenly spaced in an array).

You can force a different alignment with `alignas`, for example to give a SIMD buffer a 16-byte alignment:

```cpp
struct alignas(16) Vec4 {
    float x, y, z, w;   // sizeof == 16, alignof == 16
};
```

Handle `alignas` with care: raising alignment changes `sizeof` and the ABI, and on hardware that requires aligned accesses, placing an object at a misaligned address can simply crash.

## trivial / trivially_copyable / standard-layout: Three Easily Confused Concepts

The C++ standard splits a set of "type properties" apart to state precisely how objects of a type behave in memory. That is a C++11 design (it broke the historical POD into several separate concerns). Let's line up the commonly confused terms first:

- **trivial type**: all of the special members (default construction, copy/move construction, assignment, destruction) are compiler-generated with no custom logic. In other words, construction/copy/destruction produce no runtime code at all — the object's bits are all there is, with no hidden actions.
- **trivially_copyable type**: it can be safely byte-copied with `memcpy` (the destination ends up with the same object representation and destructs normally). **This is the criterion for whether `memcpy` is allowed.** Per the C++23 draft:
  <https://eel.is/c++draft/class.prop#11>, there are roughly three requirements:
  1. At least one copy/move constructor or assignment operation is not deleted
  2. Every eligible copy/move constructor and copy/move assignment operator is trivial.
  3. The destructor is trivial.
- **standard-layout type**: it follows predictable memory layout rules (members laid out in declaration order, with no indeterminate layout caused by complicated access control / virtual inheritance / multiple base classes). **This is the criterion for layout compatibility with a C struct.**

One key fact: the old `POD` (Plain Old Data) concept was split in C++11 into `trivial` and `standard-layout`; semantically, `POD` means "both trivial and standard-layout". So the safety assumptions related to ABI and C interop are now checked separately with `std::is_standard_layout_v<T>` and `std::is_trivially_copyable_v<T>`.

An example that ties them together:

```cpp
struct S {
    int    x;
    double y;
    // no user-defined constructor/destructor/copy, no virtual functions, no base classes
};
// S is typically trivial, trivially_copyable, standard-layout -> POD
static_assert(std::is_trivially_copyable_v<S>);
static_assert(std::is_standard_layout_v<S>);
```

Now contrast that with a non-trivial one:

```cpp
struct T_0 {
    int x;
};
// T_0 is trivial and trivially_copyable
static_assert(std::is_trivial_v<T_0>);
static_assert(std::is_trivially_copyable_v<T_0>);

struct T_1 {
    T_1() { /* custom constructor */ }
    int x;
};
// T_1 is not trivial (a user-defined constructor exists), but in this example it is trivially_copyable
// Note: the default constructor is entirely outside the scope of that check.
static_assert(!std::is_trivial_v<T_1>);
static_assert(std::is_trivially_copyable_v<T_1>);

struct T_2 {
    T_2() {}
    T_2(const T_2&) { /* custom copy constructor */ }
    int x;
};
static_assert(!std::is_trivial_v<T_2>);
static_assert(!std::is_trivially_copyable_v<T_2>);
```

One more easily-missed point worth hammering: **trivial ≠ trivially_copyable**. The former is about the "trivialness" of the special members (the default constructor in particular), the latter about whether byte-wise copying is safe. To decide whether a type can be `memcpy`'d, use `std::is_trivially_copyable_v<T>`, not `is_trivial`.

## Run It: Layout and Type Properties in Practice

Just quoting `sizeof(B)==12` and `sizeof(C)==8` is too abstract, so let's nail these assumptions into compile time with `static_assert`, and then run it for a look:

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

Every `static_assert` holds (the fact that it compiles proves A=8, B=12, C=8, Vec4=16, S both trivially copyable and standard-layout, and T non-trivial — all the assumptions check out). This is the right way to use this kind of knowledge: **write your layout/type assumptions into the code as `static_assert`s**. The moment an assumption breaks, the compiler stops you — far more reliable than a comment.

## Aggregates and Designated Initializers: From Braces to C++20

Aggregates are a convenient class of types: they can be initialized by simply listing their members in braces (aggregate initialization), which is extremely intuitive when writing data descriptions (config structs, register maps), and they are naturally `constexpr`-friendly. Intuitively, an aggregate is a type with "no user-defined constructors, no virtual functions, all non-static members public, no base classes (or ones satisfying the standard-layout restrictions)" — the compiler can simply copy the initializer values into the object representation member by member, in order.

```cpp
struct Point { int x, y; };
Point p1{1, 2};    // aggregate initialization, members assigned in declaration order

struct Config { int baud; int parity; int stop_bits; };
constexpr Config default_cfg{115200, 0, 1};   // works as constexpr too
```

C++20 introduced **designated initializers** (designated initializer — C had them long ago; C++20 finally adopted them officially), making aggregate initialization more readable and insensitive to member order:

```cpp
struct S { int a, b, c; };
S s1{.b = 2, .a = 1, .c = 3};   // member order doesn't matter
S s2{.a = 1};                   // initializes only a; the rest is default/zero-initialized
```

Nested structs and array indices can be designated too, which is especially handy when initializing complicated layouts (register tables, protocol headers):

```cpp
struct Header { uint16_t id; uint16_t flags; };
struct Packet { Header hdr; uint8_t payload[8]; };

Packet pkt{
    .hdr     = {.id = 0x1234, .flags = 0x1},
    .payload = {[0] = 0xAA, [3] = 0x55}   // assigns only elements 0 and 3
};
```

Note: designated initializers apply only to **aggregate types**; a class with user-defined constructors cannot use this syntax.

## Putting Them to Work: Practical Rules for Type Properties

Let's distill the points above into a few actionable rules. First, when defining data structures that talk to C or travel over DMA (register maps, protocol headers, serialization formats), make sure the type is **standard-layout** (predictable layout) and preferably **trivially_copyable** (so you can memcpy it, or reinterpret a block of memory directly as it) — avoid virtual functions, avoid private non-static members, don't write custom constructors/destructors/copies, and pin these invariants down with `static_assert` at the interface:

```cpp
static_assert(std::is_standard_layout_v<MyRegs>);
static_assert(std::is_trivially_copyable_v<MyRegs>);
```

Second, alignment affects `sizeof` and array layout. When hardware or DMA demands a special alignment (16-byte cache lines, SIMD), spell it out explicitly with `alignas`, and remember that it changes `sizeof` and the ABI.

Third, prefer braces and designated initializers for initialization: readable, robust against member reordering, and often `constexpr`-capable.

Fourth, copy semantics: **only `trivially_copyable` types can be safely copied with `memcpy(&dst, &src, sizeof(T))`**. For classes with virtual functions, non-trivial destructors, or non-trivial special members, don't do binary copies — dutifully use construction/copy/assignment.

## Summary

- `alignof` sets the alignment requirement, and `sizeof` reports the real footprint (padding included); sensible member ordering saves padding.
- `trivial`, `trivially_copyable`, and `standard-layout` are the standard's fine-grained split of type properties: for `memcpy`, check `trivially_copyable`; for C layout compatibility, check `standard-layout`; `POD` = both trivial and standard-layout.
- Aggregate initialization is convenient; C++20 designated initializers are more readable and don't depend on member order.
- Write your layout and type assumptions into the code as `static_assert`s, and let the compiler guard those invariants for you.

Want to run it and see for yourself? Open the online demo below (it runs, and you can inspect the assembly):

<OnlineCompilerDemo
  title="Object Size and Trivial Types: trivial / trivially_copyable / standard-layout"
  source-path="code/examples/vol3/12_object_size.cpp"
  description="Querying type properties with type_traits at compile time, pinning constraints with static_assert, and the sizeof cost of vptr and alignment"
  allow-run
/>

## References

- [Type traits — cppreference](https://en.cppreference.com/w/cpp/header/type_traits)
- [Standard-layout types — cppreference](https://en.cppreference.com/w/cpp/language/data_members#Standard_layout)
- [Designated initializers (C++20) — cppreference](https://en.cppreference.com/w/cpp/language/aggregate_initialization#Designated_initializers)
