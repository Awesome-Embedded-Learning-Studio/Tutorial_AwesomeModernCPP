---
chapter: 11
cpp_standard:
- 11
- 14
- 17
- 20
description: An introduction to Empty Base Optimization (EBO) and C++20 [[no_unique_address]]
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'Chapter 2: Zero-Overhead Abstraction'
reading_time_minutes: 6
tags:
- host
- cpp-modern
- intermediate
- 零开销抽象
title: Empty Base Optimization (EBO)
translation:
  source: documents/vol4-advanced/03-empty-base-optimization.md
  source_hash: 3489c25ee12064211c70c3b43127eeb31d5a3080a8648c62ff6c3f9258fe0ee1
  translated_at: '2026-09-26T02:52:13+00:00'
  engine: anthropic
  token_count: 3400
---
# Empty Base Optimization (EBO): C++'s Slimming Trick

There is a quiet, remarkably effective memory optimization that keeps saving you a byte here and there in places you never see — **Empty Base Optimization (EBO)**. When writing libraries, we often use empty classes as "policies / tags / stateless behavior objects"; EBO can squeeze these stateless base classes out of the object layout, saving space and improving locality.

------

## Let's Try a TL;DR Anyway

- **EBO allows the compiler to omit the storage of an empty base-class subobject (it takes no extra bytes), shrinking the derived class's sizeof.**
- **Empty member variables cannot be compressed by EBO by default, but `[[no_unique_address]]`, introduced in C++20, achieves a similar compression effect for members.**
- **Do not rely on object-address uniqueness to identify empty subobjects — addresses may coincide (this is an allowed side effect of the optimization), and assumptions about addresses lead to bugs.**
- In practice: library implementations commonly use the "inherit from an empty policy class" or "compressed pair" tricks; C++20 makes things cleaner, but understanding traditional EBO is still very useful.

------

## The Concept, Starting From an Everyday Analogy

Imagine a container object with two members: one is a warehouse that actually holds things (say, an `int` or a pointer), the other is an empty "tag" — pure behavior, no data. Intuitively you might allocate space for every member, but the language standard allows the compiler to place the "empty tag" base-class subobject at a position that costs no extra space (for example, reusing the first byte of the derived object). The derived object as a whole becomes smaller and more cache-friendly — that is the core of EBO.

The standard imposes the "a most-derived object must have non-zero size" requirement on most-derived objects, but **base-class subobjects are exempt from it**: the compiler may treat the size of an empty base-class subobject as 0 (no extra bytes). This is exactly the legal foundation EBO rests on.

------

## A Simple Example

```cpp
struct Empty {}; // empty class

struct A {
    Empty e;     // a member; typically occupies 1 byte
    int x;
};

struct B : Empty { // inherit Empty — EBO gets a chance to kick in
    int x;
};

static_assert(sizeof(A) >= sizeof(int) + 1);
static_assert(sizeof(B) == sizeof(int)); // usually holds on compilers that support EBO

```

In the example above, `Empty e` in `A` is a data member, and by language rules it must occupy non-zero bytes (to preserve semantics such as arrays); `B`, by contrast, makes `Empty` a base class, so the compiler can "press" it into `B`'s layout, and `sizeof(B)` is therefore usually equal to `sizeof(int)` (details may differ across compilers/ABIs).

------

## Why the STL and Other Libraries Keep Using the "Inherit from Empty Classes" Trick

In the standard library, types such as allocators, comparators, and deleters are often stateless empty classes. Kept as members, they waste space; made base classes (usually via **private inheritance**), they enable EBO and save object size. Many implementations wrap the pointer-plus-empty-deleter case in a "compressed pair" or a similar tool to achieve a minimal object size. Microsoft's STL blog and other implementations illustrate how widespread this practice is.

------

## C++20: `[[no_unique_address]]` Makes "Empty Member Optimization" Official and Safe

Traditional EBO can only be achieved through inheritance (members cannot be compressed). The `[[no_unique_address]]` attribute introduced in C++20 also permits **members** to share their address with other subobjects (that is, zero-size semantics become permitted), so member syntax alone can achieve an effect similar to EBO, with more intuitive code and clearer semantics. For example:

```cpp
struct Empty {};
struct Holder {
    [[no_unique_address]] Empty e; // can now share its address with other members
    int x;
};

```

This looks better than private inheritance in the implementation, and it avoids the potential interface exposure that inheritance brings. cppreference and several implementation write-ups summarize the semantics and limitations of `[[no_unique_address]]`; it is strongly recommended as the first choice wherever C++20 is available.

------

## Common Misconceptions and Pitfalls (Be Careful Here)

- **"An empty-class subobject has no address" — wrong.** The standard allows a base-class subobject to share its starting address with the most derived object; as a consequence, a base-class subobject's address may be the same as another subobject's (or as the object as a whole). Do not write code that depends on subobject address uniqueness.
- **Why can't `std::pair` exploit EBO directly?** Because `std::pair` stores `first` and `second` as **members** rather than empty base classes, traditional EBO cannot be applied to them (unless `[[no_unique_address]]` is used, or the implementation is reworked in a compressed-pair style). This is also why internal implementation tricks like "compressed pair" exist.
- **Multiple empty base classes can sometimes interfere with each other**: if you inherit from several empty types, the compiler will attempt EBO for them, but in certain cases (such as duplicated base-class types, or identical types forced by the ABI or by nested templates) the optimization is restricted. The common practice is to make each empty base class's type "unique" as far as the compiler is concerned (for example, by parameterizing it through templates) so the compression takes effect. Some people call this problem "you need to differentiate the base-class types".

------

## Practical Advice

1. **Don't optimize prematurely by default**: write policy classes as empty classes and keep them as members or inherit from them — either works; readability comes first.
2. **If you need minimal memory or are implementing a library (such as smart pointers or containers), reach first for `[[no_unique_address]]` (C++20) or a controlled private-inheritance EBO trick.** C++20 makes the code more intuitive.
3. **Do not rely on object or subobject address uniqueness**: when writing debugging, serialization, or comparison logic, avoid using addresses to distinguish empty subobjects. Addresses may be identical — the standard permits this reuse.

------

## Run It Online

Run the EBO examples online and compare how sizeof changes when the empty class is a member vs. a base class:

<OnlineCompilerDemo
  title="Empty Base Optimization and C++20 [[no_unique_address]]"
  source-path="code/examples/compiler_explorer/ebo_host.cpp"
  arm-source-path="code/examples/compiler_explorer/ebo_arm.cpp"
  description="Run it online and watch EBO eliminate the extra overhead of empty classes. Switch to the ARM assembly to see the effect on Cortex-M."
  allow-run
  allow-x86-asm
  allow-arm-asm
/>

## Summary

EBO is a micro-optimization in C++ whose effect is visible yet never showy: it stops empty policy classes from wasting bytes. Historically we implemented EBO with private inheritance; modern C++ (C++20) lets empty members be compressed too, via `[[no_unique_address]]`, making the code more intuitive and safer. In real-world engineering, prioritize clear and maintainable code: when object size becomes sensitive, reach for techniques such as EBO / `[[no_unique_address]]` / compressed pairs to optimize by hand, and verify the behavior on your target compiler.
