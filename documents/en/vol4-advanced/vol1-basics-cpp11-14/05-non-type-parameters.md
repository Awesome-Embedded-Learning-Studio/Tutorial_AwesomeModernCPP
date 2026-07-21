---
title: "Non-Type Template Parameters: From Integers to C++20 Floats and Class Types"
description: "A non-type template parameter parameterizes a value, not a type. The N in array<T, N> is one. What types it accepts, the C++17 auto placeholder, how C++20 opened it up to floating-point and structural class types, and which arguments count as the same instantiation."
chapter: 12
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 模板
  - 泛型
  - 编译期计算
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 18
prerequisites:
  - "Template Specialization and Partial Specialization: The Art of Pattern Matching"
  - "Class Templates: Members, Dependent Names, and Lazy Instantiation"
related:
  - "Name Lookup and ADL: How Two-Phase Lookup Works"
  - "Template Friends and Barton-Nackman: The Hidden Friends Trick"
---

# Non-Type Template Parameters: From Integers to C++20 Floats and Class Types

The previous pieces parameterized types. `typename T` stands in for a type. A template can also parameterize something else: **a value known at compile time**. The `N` in `std::array<T, N>` and `std::bitset<N>` is exactly this kind of parameter, called a non-type template parameter. This piece covers what types of values it can take, how C++17 `auto` made it more flexible, how C++20 opened it up from "integers and pointers only" to "floating-point and even class types," and which arguments count as the "same" instantiation. The C++20 relaxation is the biggest upgrade non-type parameters have had since they were invented, and it enables fixed_string, compile-time constant objects, and other new tricks.

## What a Non-Type Parameter Is: Parameterizing a Value

A type parameter `typename T` stands in for a type. A non-type parameter stands in for **a concrete value**. The most classic form is an integer.

```cpp
template <typename T, std::size_t N>
struct array {
    T data[N];   // N is a size known at compile time
};
```

Here `N` is a non-type parameter. Its "type" is `std::size_t`, and at instantiation you give it a concrete value, say `array<int, 8>`, so `N` is 8. `N` must be known at compile time, because the compiler uses it to produce the array type `int data[8]`, which is part of the type itself and cannot change at runtime.

What types can a non-type parameter take? Before C++17, the rules were narrow: integer types (the various `int`, `char`, `bool`), enumerations, pointers, references, and pointers to members. Floating-point was out. Ordinary class objects were out. The restrictions spawned a pile of workarounds, and C++20 finally legitimized them.

## Integers and Pointers: The Classic Uses

Integer non-type parameters are the most common. Fixed-size containers, bitsets, and compile-time constants all rely on them.

```cpp
template <int Lower, int Upper>
struct Range {
    static constexpr int lo = Lower;
    static constexpr int hi = Upper;
};

// arguments must be compile-time constants
constexpr int kMin = 10;
Range<kMin, kMin + 100> r;   // OK: both arguments are constant expressions
// Range<some_runtime_value, 100> r2;   // compile error: argument not constant
```

Pointers and references can also be non-type parameters, but the argument must be an object whose address is known at compile time, such as the address of a static variable or a function.

```cpp
template <int* P>
struct PtrHolder {
    static int* get() { return P; }
};

int global_var = 42;
PtrHolder<&global_var> ph;   // OK: the global's address is known at compile time
```

This "argument must be a constant expression" requirement is the fundamental difference between a non-type parameter and an ordinary function parameter. A function parameter takes a value at runtime; a non-type parameter has to be fixed at compile time.

## C++17 auto: Letting the Type Be Deduced

C++17 added something practical for non-type parameters: the `auto` placeholder. You used to have to spell out the type, `template <int N>`. Now you can write `template <auto N>` and let the compiler deduce it from the argument.

```cpp
template <auto N>
struct Constant {
    static constexpr auto value = N;
};
```

Run it to see the flexibility.

```bash
$ g++ -Wall -Wextra -std=c++17 ntp_auto.cpp -o ntp_auto && ./ntp_auto
Constant<42>::value = 42
Constant<true>::value = 1
Constant<'a'>::value = a
```

`Constant<42>` deduces `N` as `int`, `Constant<true>` as `bool`, `Constant<'a'>` as `char`. One template parameter holds values of different types. This removes a lot of `template <typename T, T N>` boilerplate when writing generic metaprogramming. You used to need two parameters (a type plus a value); now one `auto` does it.

## C++20's Two Big Openings: Floating-Point and Class Types

C++20 did major surgery on non-type parameters and opened two areas that were off-limits before.

**Floating-point.** Before C++20, floating-point could not be a non-type parameter, because equivalence was hard to define cleanly (whether two compile-time floats are "equal" is tricky). C++20 nailed the rule down and opened it up.

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

`CircleArea<3.14159265>` bakes pi into the type as a compile-time constant. You used to need an ordinary `constexpr double pi = ...` variable for this. Now it can go straight into a template parameter, which means different values of `Pi` produce different types, so the type itself can carry precision information.

**Class types (structural class).** This is the more imaginative half of the C++20 relaxation. Class objects used to be barred from non-type parameters because they have constructors, addresses, and lifetimes, and cannot be compared for equivalence easily. C++20 introduced the notion of a **structural type**: a class type that meets a few conditions can be used as a non-type parameter. The conditions are that it is a **literal class type** (a literal class — having a constexpr constructor is enough, it does **not** have to be an aggregate), that all bases and non-static data members are `public` and non-`mutable`, and that the bases and members are themselves structural. In short, an "all-public, member-immutable value type" whose equivalence the compiler can decide by comparing members one by one.

One easy point to confuse: a literal class is not the same thing as an aggregate. An aggregate forbids user-declared constructors; a literal class only requires a constexpr constructor (and may have user-provided constructors). What structural asks for is a literal class, so **a class with a constexpr constructor — even if it is not an aggregate — can be a non-type parameter**. The `fixed_string` in the next paragraph is exactly this case: it has a constexpr constructor that copies a `char` array, it is not an aggregate, but it is structural.

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

`Pixel<Point{3, 4}>` bakes a 2D coordinate into the type. The flagship application of this is **fixed_string**: using a structural string class (with a constexpr constructor and a `char` array inside), you can wrap a string into a type and get "a type that carries a string." Logging libraries that tag each level with a fixed_string, or networking libraries that put URL paths into types, build on this. Note that string literals can **never** be a non-type parameter directly (this is still true in C++20): the type of a literal is `const char[N]`, which decays to a pointer, and different literals have different addresses, so equivalence cannot be handled. The C++20 breakthrough is not "literals as NTTP" — it is "wrap the literal in a structural fixed_string class and use that as the NTTP."

## Equivalence: Which Arguments Count as "the Same"

Non-type parameters come with an unavoidable question: when do two arguments count as the "same instantiation"? The rule is called template-argument-equivalent. For integers, it is value equality: `1 + 1` and `2` are equivalent, so `Tag<1 + 1>` and `Tag<2>` are the same type.

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

The program prints `true`, which confirms `Tag<1+1>` and `Tag<2>` are the same type. The practical effect: whether you write `1+1` or `2`, the compiler does not instantiate `Tag<2>` twice, because it recognizes them as the same thing.

For pointers and references, equivalence is "points to the same object or function." For floating-point (C++20) and structural class types (C++20), equivalence is a bitwise or member-wise comparison. One thing to watch with floating-point: two float arguments count as equivalent only when they are bitwise identical, not when they are numerically equal. The classic counterexample is `Circle<0.0>` versus `Circle<-0.0>`: `0.0 == -0.0` holds numerically, but the bit patterns differ (the sign bit differs), so they are **two different types**. Conversely, `Circle<3.14>` and `Circle<3.14000>` end up bitwise identical after lexical folding, so they are the same type. This avoids ambiguity from precision fuzz, and it also means when you pass a floating-point template argument, the literal has to be exact.

## Typical Uses of Non-Type Parameters

Grouping the common uses, so you know which to reach for.

Fixed-size containers. `std::array<T, N>`, `std::bitset<N>`, and the `fixed_vector<T, N>` project at the end of this volume all use integer non-type parameters to bake the size into the type. The payoff is stack storage, no dynamic allocation, and type-safe sizes (different `N` give different `array` types, and the compiler catches misuse).

Compile-time constants. Bake physical constants, configuration values, or version numbers into types as non-type parameters. `CircleArea<3.14>` in this piece is that kind. The type carries the value, which enables compile-time dispatch.

Strings and objects after C++20. fixed_string, compile-time coordinates, compile-time configuration objects all rely on structural class non-type parameters. This is a new window C++20 opened for template metaprogramming, and part three of this volume expands on it.

## A Few Traps

The argument must be a constant expression. This is the rule; runtime values do not compile.

String literals can never be a non-type parameter directly (this is still true in C++20). `Foo<"hello">` does not work, because after decay the equivalence cannot be handled. The C++20 fix is to wrap the literal in a structural fixed_string class, covered in part three.

Floating-point equivalence is bitwise. `Circle<0.0>` and `Circle<-0.0>` are numerically equal but bitwise different, so they are two types; in the other direction, `Circle<3.14>` and `Circle<3.14000>` are bitwise identical after lexical folding, so they are the same type. The criterion is bitwise, not numerical, not syntactic.

Next we move into two-phase name lookup and ADL. Name lookup inside templates works nothing like ordinary code; it happens in two phases, and that mechanism is exactly what explains why the `typename`, `this->`, and hidden-friends rules from the previous pieces have to exist.
