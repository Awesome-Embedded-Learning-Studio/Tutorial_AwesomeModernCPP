---
chapter: 11
cpp_standard:
- 11
- 14
- 17
- 20
description: Introduces empty base optimization (EBO) and C++20 [[no_unique_address]]
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'Chapter 2: 零开销抽象'
reading_time_minutes: 5
tags:
- host
- cpp-modern
- intermediate
- 零开销抽象
title: EBO (Empty Base Optimization)
translation:
  engine: anthropic
  source: documents/vol4-advanced/03-empty-base-optimization.md
  source_hash: 3489c25ee12064211c70c3b43127eeb31d5a3080a8648c62ff6c3f9258fe0ee1
  token_count: 840
  translated_at: '2026-06-13T11:50:22.052740+00:00'
---
# Empty Base Optimization (EBO): A C++ Slimming Technique

There is a low-profile yet efficient memory optimization that silently saves bytes for you in places you rarely notice—**Empty Base Optimization (EBO)**. When writing libraries, we often use empty classes as "policies, tags, or stateless behavior objects." EBO allows these stateless base classes to be squeezed out of the object layout, saving space and improving locality.

------

## TL;DR

- **EBO allows the compiler to omit the storage for empty base class subobjects (i.e., they take up no extra bytes), thereby reducing the size of the derived class.**
- **Empty member variables cannot be compressed by EBO by default, but C++20 introduced `[[no_unique_address]]` to achieve similar compression effects for members.**
- **Do not rely on object address uniqueness to identify empty subobjects—their addresses might be identical (a permitted side effect of this optimization), and assumptions about addresses can lead to bugs.**
- In practice: library implementations often use "inheriting from empty policy classes" or "compressed pair" tricks. C++20 makes things cleaner, but understanding traditional EBO is still very useful.

------

## Concepts: Starting with a Real-World Analogy

Imagine a container object with two members: one is a warehouse that actually holds things (like `std::vector` or a pointer), and the other is an empty "tag"—representing behavior only, with no data. Intuitively, you might allocate space for each member, but the language standard allows the compiler to place the "empty tag" base class subobject in a location that takes up no extra space (for example, reusing the first byte of the derived object). This makes the derived object smaller and more cache-friendly—this is the core of EBO.

The standard imposes the requirement that "the most derived object must have non-zero size" on the most derived object itself, but **base class subobjects are not subject to this restriction**. The compiler can treat the size of an empty base class subobject as zero (i.e., occupying no extra bytes). This is the legal basis for EBO.

------

## Simple Example

```cpp
struct Empty {};

struct DataMember {
    Empty e;
    int x;
};

struct BaseInherit : Empty {
    int x;
};

static_assert(sizeof(DataMember) > sizeof(int));
static_assert(sizeof(BaseInherit) == sizeof(int)); // EBO usually applies here
```

In the example above, `e` in `DataMember` is a data member. According to language rules, it must occupy non-zero bytes (to ensure semantics like array indexing work). However, `BaseInherit` inherits from `Empty` as a base class. The compiler can "compress" it into `BaseInherit`'s layout, so `sizeof(BaseInherit)` typically equals `sizeof(int)` (details may vary by compiler/ABI).

------

## Why Do We Often See the "Inherit from Empty Class" Pattern in the STL/Libraries?

In the standard library, types like allocators, comparators, and deleters are often stateless empty classes. If used as members, they waste space. If used as base classes (usually **private inheritance**), EBO is enabled, saving object size. Many implementations wrap scenarios like "pointer + empty deleter" into "compressed pair" or similar utilities to achieve minimal object size. Microsoft's STL blog and other implementations demonstrate the prevalence of this approach.

------

## C++20: `[[no_unique_address]]` Makes "Empty Member Optimization" Formal and Safe

Traditional EBO can only be achieved through inheritance (members cannot be compressed). The `[[no_unique_address]]` attribute introduced in C++20 allows **members** to share addresses with other subobjects (i.e., allowing zero-size semantics), achieving EBO-like effects with member syntax. This makes the code more intuitive and semantically clearer. For example:

```cpp
struct Modern {
    [[no_unique_address]] Empty e;
    int x;
};
// sizeof(Modern) is likely equal to sizeof(int)
```

This looks better than private inheritance and avoids potential interface exposure brought by inheritance. cppreference and some implementation articles summarize the semantics and constraints of `[[no_unique_address]]`. It is highly recommended to prioritize this when C++20 is available.

------

## Common Misconceptions and Pitfalls (Must Read)

- **"Empty class subobjects definitely don't have an address"—Wrong.** The standard allows base class subobjects to share the starting address with the most derived object. This means the address of a base class subobject might be the same as another subobject (or the object as a whole). Do not write code that relies on the uniqueness of subobject addresses.
- **Why can't `std::unique_ptr` directly utilize EBO?** Because `std::unique_ptr` uses the deleter and pointer as **members**, not empty base classes. Traditional EBO cannot apply to members (unless using `[[no_unique_address]]` or changing the implementation to a compressed-pair style). This is why internal implementation tricks like "compressed pair" exist.
- **Multiple empty base classes can sometimes interfere with each other**: If you inherit from multiple empty types, the compiler will try to apply EBO for them. However, in certain cases (such as duplicate base types, or identical types caused by ABI or nested templates), the optimization may be restricted. A common practice is to make each empty base class type "unique" to the compiler (e.g., via template parameterization) to ensure compression takes effect. Some people call this "making base class types distinct."

------

## Practical Advice

1. **Don't optimize prematurely by default**: It's fine to write policy classes as empty classes using members or inheritance; prioritize readability.
2. **If minimal memory is required or you are implementing libraries (like smart pointers, containers), prioritize `[[no_unique_address]]` (C++20) or controlled private inheritance EBO tricks.** C++20 makes the code more intuitive.
3. **Don't rely on object or subobject address uniqueness**: When writing debugging, serialization, or comparison logic, avoid using addresses to distinguish empty subobjects. Addresses might be identical, and the standard permits this reuse.

------

## Online Demo

Run the EBO example online to compare the `sizeof` changes when an empty class is used as a member versus a base class:

<OnlineCompilerDemo
  title="Empty Base Optimization and C++20 [[no_unique_address]]"
  source-path="code/examples/compiler_explorer/ebo_host.cpp"
  arm-source-path="code/examples/compiler_explorer/ebo_arm.cpp"
  description="Run online and observe how EBO eliminates the overhead of empty classes. Switch to ARM assembly to see the effect on Cortex-M."
  allow-run
  allow-x86-asm
  allow-arm-asm
/>

## Summary

EBO is a "visible yet subtle" micro-optimization in C++ that stops empty policy classes from wasting bytes. Historically, we implemented EBO using private inheritance. Modern C++ (C++20) uses `[[no_unique_address]]` to compress empty members as well, making code more intuitive and safe. In actual engineering, prioritize writing clear, maintainable code: when object size is sensitive, use tricks like EBO, `[[no_unique_address]]`, or compressed-pair to manually optimize, and verify the behavior on the target compiler.
