---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: Introduces EBO (Empty Base Optimization) techniques
difficulty: intermediate
order: 4
platform: stm32f1
prerequisites:
- 'Chapter 2: 零开支抽象'
reading_time_minutes: 6
tags:
- cpp-modern
- intermediate
- stm32f1
title: EBO (Empty Base Optimization)
translation:
  source: documents/vol8-domains/embedded/04-empty-base-optimization.md
  source_hash: 8ff16133f7a3f52bbcfc18715640e359882b20a288565d2effcb35913a22a32d
  translated_at: '2026-05-26T12:19:04.497351+00:00'
  engine: anthropic
  token_count: 831
---
# EBO (Empty Base Optimization): C++'s Slimming Trick

There is a low-profile yet highly effective memory optimization that silently saves you bytes behind the scenes—**EBO (Empty Base Optimization)**. When writing libraries, we often use empty classes as "policies, tags, or stateless behavior objects." EBO allows these stateless base classes to be squeezed out of the object layout, saving space and improving locality.

------

## TL;DR

- **EBO allows the compiler to omit the storage of empty base class subobjects (i.e., they occupy no extra bytes), thereby reducing the `sizeof` of the derived class.**
- **Empty member variables cannot be compressed by EBO by default, but `[[no_unique_address]]` introduced in C++20 achieves a similar compression effect for members.**
- **Do not rely on object address uniqueness to identify empty subobjects—their addresses might be identical (which is an allowed side effect of this optimization), and making assumptions about addresses will lead to bugs.**
- In practice: library implementations commonly use the "inheriting from an empty policy class" or "compressed pair" trick; C++20 makes things cleaner, but understanding traditional EBO remains highly useful.

------

## Explaining the Concept with an Everyday Analogy

Imagine a container object with two members: one is an actual storage warehouse (like an `int` or a pointer), and the other is an empty "tag" that merely represents behavior and holds no data. Intuitively, you might allocate space for each member, but the language standard allows the compiler to place the "empty tag" base class subobject in a location that requires no extra space (such as reusing the first byte of the derived object). This makes the derived object smaller overall and more cache-friendly—which is the core of EBO.

The standard applies the "most derived object must have a non-zero size" requirement to the most derived object, but **base class subobjects are exempt from this restriction**. The compiler can treat the size of an empty base class subobject as 0 (i.e., occupying no extra bytes). This is the exact legal basis for EBO.

------

## A Simple Example

```cpp
struct Empty {}; // 空类

struct A {
    Empty e;     // 成员，通常会占 1 字节
    int x;
};

struct B : Empty { // 继承 Empty —— EBO 有机会发生
    int x;
};

static_assert(sizeof(A) >= sizeof(int) + 1);
static_assert(sizeof(B) == sizeof(int)); // 在支持 EBO 的编译器上通常成立

```

In the example above, `Empty e` in `A` is a data member, which by language rules must occupy a non-zero byte (to guarantee semantics like array indexing). In contrast, `B` takes `Empty` as a base class, allowing the compiler to "compress" it into `B`'s layout. As a result, `sizeof(B)` typically equals `sizeof(int)` (though details may vary across different compilers/ABIs).

------

## Why Do We Often See the "Inheriting from Empty Classes" Pattern in the STL and Libraries?

In the standard library, types like allocators, comparators, and deleters are often stateless empty classes. If we use them as members, they waste space; using them as base classes (typically via **private inheritance**) enables EBO and reduces object size. Many implementations wrap the "pointer + empty deleter" scenario into a "compressed pair" or similar utility to achieve minimal object size. Microsoft's STL blog and other implementations demonstrate the ubiquity of this approach.

------

## C++20: `[[no_unique_address]]` Makes "Empty Member Optimization" Formal and Safe

Traditional EBO can only be achieved through inheritance (members cannot be compressed). The `[[no_unique_address]]` attribute introduced in C++20 allows **members** to share addresses with other subobjects (i.e., allowing zero-size semantics), achieving an EBO-like effect using member syntax. This makes the code more intuitive and the semantics clearer. For example:

```cpp
struct Empty {};
struct Holder {
    [[no_unique_address]] Empty e; // 现在可以和其它成员共享地址
    int x;
};

```

This looks much better in practice than private inheritance and avoids the potential exposure of interfaces that inheritance brings. cppreference and various implementation articles summarize the semantics and limitations of `[[no_unique_address]]`, and we strongly recommend prioritizing it wherever C++20 is available.

------

## Common Misconceptions and Pitfalls (Pay Close Attention)

- **"Empty class subobjects definitely don't have an address"—Wrong.** The standard allows a base class subobject to share the starting address of the most derived object. This means the address of the base class subobject might be identical to that of another subobject (or the object as a whole). Do not write code that relies on subobject address uniqueness.
- **Why can't `std::pair` directly leverage EBO?** Because `std::pair` treats `first` and `second` as **members** rather than empty base classes, traditional EBO cannot apply to members (unless using `[[no_unique_address]]` or refactoring the implementation into a compressed-pair style). This is exactly why internal implementation tricks like "compressed pair" exist.
- **Multiple empty base classes can sometimes interfere with each other**: If you inherit from multiple empty types, the compiler will attempt to apply EBO to all of them. However, in certain situations (such as duplicate base types, or identical types caused by ABI or nested templates), the optimization is restricted. A common practice is to make each empty base class type "unique" to the compiler (e.g., by parameterizing with templates) to ensure the compression takes effect. Some refer to this issue as "needing to differentiate base class types."

------

## Practical Advice

1. **Don't prematurely optimize by default**: Writing policy classes as empty classes using either members or inheritance is fine; prioritize readability.
2. **If you need minimal memory or are implementing a library (like smart pointers or containers), prioritize `[[no_unique_address]]` (C++20) or controlled private inheritance EBO tricks.** C++20 makes the code more intuitive.
3. **Don't rely on object or subobject address uniqueness**: When writing debugging, serialization, or comparison logic, avoid using addresses to distinguish empty subobjects. Addresses might be identical, and the standard permits this reuse.

------

## Run Online

Run the EBO example online to compare the `sizeof` changes when an empty class is used as a member versus a base class:

<OnlineCompilerDemo
  title="EBO (Empty Base Optimization) and C++20 [[no_unique_address]]"
  source-path="code/examples/compiler_explorer/ebo_host.cpp"
  arm-source-path="code/examples/compiler_explorer/ebo_arm.cpp"
  description="Run online and observe how EBO eliminates the overhead of empty classes. Switch to ARM assembly to see the effects on Cortex-M."
  allow-run
  allow-x86-asm
  allow-arm-asm
/>

## Summary

EBO is a micro-optimization in C++ that "delivers visible results without showing off": it prevents empty policy classes from wasting bytes. Historically, we implemented EBO using private inheritance, but modern C++ (C++20) uses `[[no_unique_address]]` to allow empty members to be compressed as well, making the code more intuitive and safer. In real-world engineering, prioritize writing clear, maintainable code: when object size becomes sensitive, then apply EBO, `[[no_unique_address]]`, or compressed-pair tricks to manually optimize, and verify the behavior on your target compiler.
