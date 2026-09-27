---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Non-type template parameters let a value, not just a type, be parameterized
  — the N in array<T, N> is one. This piece sorts out which types of values it accepts,
  the C++17 auto placeholder, the C++20 relaxation to floating-point and class types
  meeting the structural requirements, and which arguments count as the "same" instantiation.'
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'Template Specialization and Partial Specialization: The Art of Pattern Matching'
- 'Class Templates: Members, Dependent Names, and Lazy Instantiation'
reading_time_minutes: 9
related:
- 'Name Lookup and ADL: How Two-Phase Lookup Works'
- 'Template Friends and Barton-Nackman: The Hidden Friends Trick'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- 编译期计算
title: 'Non-Type Template Parameters: From Integers to C++20 Floats and Class Types'
translation:
  source: documents/vol4-advanced/vol1-basics-cpp11-14/05-non-type-parameters.md
  source_hash: 7f78cd483f1485e9089c95cf956703baab62276135b25eb2c8ad196ed2ce2563
  translated_at: '2026-09-26T04:14:13+00:00'
  engine: anthropic
  token_count: 1900
---
# Non-Type Template Parameters: From Integers to C++20 Floats and Class Types

In the previous few pieces, everything we parameterized was a type — `typename T` stands in for a type. But a template can parameterize one more thing: **a value known at compile time**. The `N` in `std::array<T, N>` and the `N` in `std::bitset<N>` are exactly this kind of parameter, called a non-type template parameter. This piece covers which types of values it can accept, how C++17 `auto` made it more flexible, how C++20 boldly widened it from "integers and pointers only" to "floating-point and even class types", and which arguments count as the "same" instantiation. The C++20 relaxation is the biggest upgrade non-type parameters have had since they were invented — it directly gave rise to fixed_string, compile-time constant objects, and other new tricks.

## What a Non-Type Parameter Is: Parameterizing a Value

A type parameter `typename T` stands in for a type; a non-type parameter stands in for a **specific value**. The classic form is an integer:

```cpp
template <typename T, std::size_t N>
struct array {
    T data[N];   // N is a size known at compile time
};
```

Here `N` is a non-type parameter whose "type" is `std::size_t`; at instantiation you have to give it a concrete value — for `array<int, 8>`, `N` is 8. `N` must be determinable at compile time, because the compiler needs it to generate the array type `int data[8]`; it is part of the type, and a type cannot change at runtime.

Which types can a non-type parameter use? Before C++17 the rules were quite narrow: integer types (the various `int`, `char`, `bool`), enumerations, pointers, references, and pointers to members. Floating-point numbers were out, and so were ordinary class objects. This restriction spawned a pile of workaround patterns, and it took C++20 to bring them into the fold.

## Integers and Pointers: The Most Traditional Use

Integer non-type parameters are the most common — fixed-size containers, bitsets, and compile-time constants all rest on them.

```cpp
template <int Lower, int Upper>
struct Range {
    static constexpr int lo = Lower;
    static constexpr int hi = Upper;
};

// the arguments must be compile-time constants
constexpr int kMin = 10;
Range<kMin, kMin + 100> r;   // OK: both arguments are constant expressions
// Range<some_runtime_value, 100> r2;   // compile error: the argument is not a constant
```

Pointers and references can serve as non-type parameters too, but the argument must be an object whose address is determinable at compile time — say, the address of a static variable or of a function:

```cpp
template <int* P>
struct PtrHolder {
    static int* get() { return P; }
};

int global_var = 42;
PtrHolder<&global_var> ph;   // OK: the global variable's address is known at compile time
```

This requirement that "the argument must be a constant expression" is the most fundamental difference between a non-type parameter and an ordinary function parameter. An ordinary function parameter takes its value at runtime; a non-type parameter must be a definite value by compile time.

## C++17 auto: Letting the Type Be Deduced

C++17 added something very practical for non-type parameters: the `auto` placeholder. Previously you had to spell out the non-type parameter's type — `template <int N>`; now you can write `template <auto N>` and let the compiler deduce it from the argument.

```cpp
template <auto N>
struct Constant {
    static constexpr auto value = N;
};
```

Run it to see the flexibility:

```bash
$ g++ -Wall -Wextra -std=c++17 ntp_auto.cpp -o ntp_auto && ./ntp_auto
Constant<42>::value = 42
Constant<true>::value = 1
Constant<'a'>::value = a
```

From `Constant<42>` the compiler deduces `N` as `int`; from `Constant<true>`, `bool`; from `Constant<'a'>`, `char`. A single template parameter holds values of different types. When you write generic metafunctions, this removes a pile of `template <typename T, T N>` boilerplate — previously two parameters (type + value), now one `auto` does the job.

## The Two Big C++20 Relaxations: Floating-Point and Class Types

C++20 performed major surgery on non-type parameters, opening up two forbidden zones nobody was allowed to touch before.

**Floating-point numbers**. Before C++20, floating-point numbers could not be non-type parameters, because equivalence checking for floats has precision pitfalls (defining cleanly whether two compile-time floating values are "equal" is genuinely hard). Once C++20 pinned the rules down, the door opened:

```cpp
template <double Pi>
struct CircleArea {
    static constexpr double compute(double r) { return Pi * r * r; }
};
```

```bash
$ g++ -Wall -Wextra -std=c++20 ntp_float.cpp -o ntp_float && ./ntp_float
area(2.0) = 12.5664
```

`CircleArea<3.14159265>` bakes pi into the type as a compile-time constant. Before, this sort of thing could only be done with an ordinary variable like `constexpr double pi = ...`; now it can go straight into a template parameter — meaning different `Pi` values produce different types, so the type itself can carry precision information.

**Class types (structural classes)**. This is the more imaginative half of the C++20 relaxation. Class objects previously could not be non-type parameters, because objects have constructors, addresses, and lifetimes, which leaves no simple way to compare equivalence. C++20 introduced the notion of a **structural type**: a class type satisfying a few conditions can be used as a non-type parameter. The conditions: it must be a **literal class type** (a literal class just needs a constexpr constructor — **being an aggregate is not required**), all base classes and non-static data members must be `public` and non-`mutable`, and the bases and members must themselves be structural. In short, an "all-public, members-immutable value type" whose equivalence the compiler can check member by member.

One point that is easily confused: a literal class and an aggregate are not the same thing. An aggregate forbids user-declared constructors; a literal class only requires a constexpr constructor (user-provided constructors are fine). What structural demands is a literal class, so **a class with a constexpr constructor (even one that is not an aggregate) can be a non-type parameter** — the fixed_string we meet just below survives on exactly this: it has a constexpr constructor copying a `char` array, is not an aggregate, yet qualifies as structural.

```cpp
struct Point {      // structural: public, non-mutable, members all structural (int); also a literal class
    int x;
    int y;
};

template <Point P>
struct Pixel {
    static constexpr Point pos = P;
};
```

```bash
$ g++ -Wall -Wextra -std=c++20 ntp_struct.cpp -o ntp_struct && ./ntp_struct
origin: (0,0)
corner: (3,4)
```

`Pixel<Point{3, 4}>` bakes a 2D coordinate into the type. The flagship application of this ability is **fixed_string**: with a structural string class (constexpr constructor, `char` array inside), you wrap the string into the type and get "a string inside a type". Logging libraries giving each log level a fixed_string tag, networking libraries treating URL paths as types — all of that is built on this. Note that string literals can **never** directly be non-type parameters (still true in C++20): their type is `const char[N]`, which decays to a pointer; different literals have different addresses, so equivalence cannot be handled. The C++20 breakthrough is allowing a structural fixed_string to **wrap** the string and serve as the NTTP — not letting literals in directly.

## Equivalence: Which Arguments Count as the Same Instantiation

Non-type parameters come with an unavoidable question: when do two arguments count as "the same instantiation"? The rule is called template-argument-equivalent. For integers, it is value equality: `1 + 1` and `2` are equivalent, so `Tag<1 + 1>` and `Tag<2>` are the same type.

```cpp
#include <iostream>
#include <type_traits>

template <int N>
struct Tag {};

int main() {
    std::cout << std::boolalpha;
    std::cout << "Tag<1+1> is Tag<2>? " << std::is_same_v<Tag<1 + 1>, Tag<2>> << "\n";
    std::cout << "Tag<2*3> is Tag<6>? " << std::is_same_v<Tag<2 * 3>, Tag<6>> << "\n";
}
```

```bash
$ g++ -Wall -Wextra -std=c++20 ntp_equiv.cpp -o ntp_equiv && ./ntp_equiv
Tag<1+1> is Tag<2>? true
Tag<2*3> is Tag<6>? true
```

The program prints `true`, which proves `Tag<1+1>` and `Tag<2>` are the same type. The practical upshot: whether you write `1+1` or `2`, the compiler does not instantiate two copies of `Tag<2>` — it recognizes them as the same thing.

For pointers and references, equivalence means "pointing to the same object or function". For floating-point (C++20) and structural class types (C++20), equivalence is a bit-level or member-level comparison of the value. A point to watch with floats: two floating arguments are equivalent only if they are **bit-identical**, not if they are numerically equal. The classic counterexample is `Circle<0.0>` versus `Circle<-0.0>`: `0.0 == -0.0` is numerically true, but their bit representations differ (the sign bit), so they are **two distinct types**. This dodges the ambiguity that fuzzy precision would create — and it also means that when passing floating template arguments, you must write the literal exactly.

## Typical Uses of Non-Type Parameters

Let's sort the common uses of non-type parameters into categories, so you know where to look when you run into one.

Fixed-size containers. `std::array<T, N>`, `std::bitset<N>`, and the `fixed_vector<T, N>` capstone project at the end of this volume all bake the size into the type with an integer non-type parameter. The benefits: storage on the stack, no dynamic allocation, and type-safe sizes (arrays with different `N` are different types, so the compiler guards you against misuse).

Compile-time constants. Baking physical constants, configuration values, or version numbers into the type as non-type parameters — this piece's `CircleArea<3.14>` is exactly that. The type carries the value, which enables compile-time polymorphic dispatch.

Strings and objects since C++20. fixed_string, compile-time coordinates, compile-time configuration objects — all rest on structural class non-type parameters. This is a new window C++20 opened for template metaprogramming; the vol3 metaprogramming part covers it in depth.

## A Few Pitfalls

The argument must be a constant expression. This is the iron law: a runtime value cannot get in; it will not compile.

String literals can never directly be non-type parameters (still true in C++20). `Foo<"hello">` does not work, because once the string literal decays to a pointer, equivalence cannot be handled. The C++20 solution is to wrap the string in a structural fixed_string class; vol3 covers it.

Equivalence of floating arguments is a bit-level comparison. `Circle<0.0>` and `Circle<-0.0>` are numerically equal but differ at the bit level — two types; conversely, `Circle<3.14>` and `Circle<3.14000>` are actually the same type, because after lexical folding they are bit-identical. The criterion is the bits — not the numeric value, and not the spelling.

In the next piece we move on to two-phase name lookup and ADL. Name lookup inside templates is a completely different game from ordinary code — it proceeds in two phases, and this mechanism directly explains why the seemingly odd rules from the previous pieces — `typename`, `this->`, hidden friends — have to exist.
