---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand alignment rules and `sizeof` calculation methods, and master
  the usage of `alignas`/`alignof`.
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 动态内存管理
reading_time_minutes: 15
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Memory Alignment and Padding
translation:
  source: documents/vol1-fundamentals/ch12/03-alignment-padding.md
  source_hash: f5779c0df5ea4bac139d11868f2e85136d50e6fb26821880b9f7ae7cbba12c37
  translated_at: '2026-06-16T03:48:22.485506+00:00'
  engine: anthropic
  token_count: 2716
---
# Memory Alignment and Padding

In the previous chapter, we divided the program's memory space into four major areas: the stack, heap, static area, and code segment, clarifying where data "lives" and how long it "survives." Now, let's look one layer deeper—even if data resides in the same memory area, it cannot be arranged arbitrarily. If you have written C++ for a while, you have likely encountered this confusion: a struct clearly has only three members, but the result of `sizeof` is significantly larger than the sum of the sizes of those three members. What on earth happened to those extra bytes?

Ta-da! The answer is the theme of this chapter: **alignment and padding**. To satisfy CPU memory access efficiency requirements, the compiler inserts "blank" bytes between struct members to align each member to specific address boundaries. These blank bytes store no valid data, but they genuinely occupy memory space. Understanding alignment rules not only allows you to accurately predict `sizeof` results but also enables you to reduce struct size in performance-sensitive scenarios by adjusting member order—this optimization requires no changes to logic code, simply reordering member declarations can save considerable memory.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Explain why CPUs need memory alignment and what happens when data is misaligned.
> - [ ] Manually calculate the `sizeof` result for any struct.
> - [ ] Use `alignas` and `alignof` (or `alignof`) to control and query alignment requirements.
> - [ ] Optimize struct memory layout by adjusting member order.
> - [ ] Understand the purpose and potential risks of `#pragma pack`.

## Alignment—The Secret Agreement Between CPU and Memory

To understand alignment, we must first look at how the CPU accesses memory. Many people assume the CPU can freely read and write data at any address on a byte-by-byte basis—from a programmer's perspective, this seems true, but the underlying hardware doesn't actually work that way. When modern CPUs access memory via the bus, they typically perform transfers in units of words. A 32-bit CPU can read or write 4 bytes at a time, and a 64-bit CPU can read or write 8 bytes at a time. Furthermore, hardware often requires that the starting address of this read/write operation be an integer multiple of the word size.

You can imagine memory as a row of lockers, each 4 slots wide. If you want to retrieve an item occupying 4 slots (a 4-byte `int`), the fastest way is to have it start exactly at the beginning of a locker, so you can get it all in one go. But if this `int` straddles the boundary of two lockers—the first two slots in the first locker, the last two in the second—the CPU has to open two lockers, extract parts separately, and then stitch them together before returning them to you. Some architectures (like ARM) will simply refuse such cross-boundary access and throw a hardware exception.

This is the underlying reason for alignment: **CPU access to aligned data is most efficient; accessing misaligned addresses is either slower or results in a direct error**. Therefore, when arranging a struct's memory layout, the compiler actively places each member at a position satisfying its alignment requirements. The extra space in between is padding bytes.

## Alignment Rules—How the Compiler Fills the Blanks

Every fundamental type has a **natural alignment requirement**, which usually equals the size of that type. `char` is 1-byte aligned (can go anywhere), `int` is 4-byte aligned (address must be a multiple of 4), and `double` is 8-byte aligned (address must be a multiple of 8). Pointers are 8-byte aligned on 64-bit systems and 4-byte aligned on 32-bit systems.

For a given struct, the compiler follows three rules:

First, each member of the struct must be placed at an address that is an integer multiple of its own natural alignment requirement. If the position where the previous member ends does not satisfy the next member's alignment requirement, the compiler inserts padding bytes between them until the address satisfies the condition.

Second, the total size of the struct itself must be an integer multiple of the alignment requirement of its largest member. In other words, if the struct contains a `double` (8-byte alignment), the total size of the struct must be a multiple of 8—even if there is empty space after the last member, padding bytes must be added to fill it.

Third, the struct's own alignment requirement equals the alignment requirement of its largest member. This rule affects "where this struct should be placed when it becomes a member of another struct."

This sounds a bit abstract, so let's look at the code directly.

## The Truth About sizeof—Where Padding Bytes Hide

Let's look at a classic example, the kind you might see in an interview:

```cpp
struct Bad {
    char a;    // 1 byte
    int b;     // 4 bytes
    char c;    // 1 byte
};
```

The three members add up to 6 bytes, but `sizeof(Bad)` is **12** on most platforms. The extra 6 bytes are all padding. Let's analyze member by member to see exactly what the compiler did.

`a` is `char`, 1-byte aligned, placed at offset 0, occupying 1 byte. Next comes `b`. It is `int`, requiring 4-byte alignment—meaning its starting offset must be a multiple of 4. But `a` only occupies offset 1, so the compiler inserts 3 padding bytes at offsets 1, 2, and 3, placing `b` at offset 4, occupying offsets 4, 5, 6, and 7. Then comes `c`. `char` only needs 1-byte alignment, so following `b` is fine. It is placed at offset 8, occupying 1 byte.

So far, 9 bytes are used. But don't forget the second rule—the total size of the struct must be an integer multiple of the largest member's alignment requirement. Here, the maximum alignment is the 4 bytes of `int`, so the struct size must be a multiple of 4. 9 is not a multiple of 4, so the compiler adds 3 more bytes of padding at the end to round up to 12. If drawn as a diagram, it looks like this:

```mermaid
flowchart LR
    subgraph Struct [Struct Bad (12 bytes)]
        direction LR
        A["a (1 byte)"]
        Pad1["Padding (3 bytes)"]
        B["b (4 bytes)"]
        C["c (1 byte)"]
        Pad2["Padding (3 bytes)"]
    end
```

> **Warning**: Member declaration order directly impacts padding amount and struct size. This is a common interview topic and an even more common pitfall in practice—especially in scenarios like network protocols or file formats where precise control over memory layout is required. Not paying attention to member order can lead to data misalignment. Crucially, if you `memcpy` this struct directly for transmission, and the receiving end parses it with a different compiler, the padding rules might differ, causing data to be misaligned immediately.

Now let's adjust the member order, putting the large ones first:

```cpp
struct Good {
    int b;     // 4 bytes
    char a;    // 1 byte
    char c;    // 1 byte
};
```

`b` is at offset 0, occupying 4 bytes. `a` is at offset 4, 1-byte aligned, no problem. `c` follows immediately at offset 5. So far, 6 bytes are used. The total size needs to be a multiple of 4—pad 2 bytes to reach 8. `sizeof(Good)` is **8**, one-third less than the previous 12.

```mermaid
flowchart LR
    subgraph Struct [Struct Good (8 bytes)]
        direction LR
        B["b (4 bytes)"]
        A["a (1 byte)"]
        C["c (1 byte)"]
        Pad1["Padding (2 bytes)"]
    end
```

Just by changing the member declaration order, without modifying any logic, the struct lost 4 bytes. If your program has millions of such objects, that saves 4 MB of memory. Therefore, a practical rule of thumb is: **Arrange members in descending order of alignment requirements**—put `double` and `long long` at the front, then `int` and pointers, and finally `char` and `bool`.

## alignas and alignof—Manual Alignment Control

The compiler's default alignment rules are sufficient in the vast majority of cases, but some scenarios require manual intervention. C++11 introduced the keywords `alignas` and `alignof` (or `alignof` in C++11 syntax) to specify and query alignment requirements respectively.

The usage of `alignof` is simple—give it a type, and it returns that type's alignment requirement (in bytes). `alignof(int)` is 4, `alignof(double)` is 8, `alignof(char)` is 1. You can even use it on structs: `alignof(Bad)` returns 4, because its largest member `int` is 4-byte aligned.

`alignas` is used to force a specific alignment. It can be used on variable declarations or type definitions:

```cpp
struct alignas(16) AlignedStruct {
    int a;
    char b;
};

alignas(64) char cache_line_buffer[64];
```

`alignas` has three typical application scenarios. The first is SIMD instructions—SSE requires operands to be 16-byte aligned, AVX requires 32-byte alignment, and AVX-512 requires 64-byte alignment. If your data is not aligned to the required boundary, SIMD load instructions will throw a hardware exception, crashing the program immediately. The second is cache line optimization—modern CPU cache lines are typically 64 bytes. If your data structure spans two cache lines, a single read triggers two cache misses. Aligning hot data to cache line boundaries avoids this "false sharing." The third is hardware interaction—certain DMA controllers or peripherals require the physical address of a buffer to be specifically aligned, necessitating the use of `alignas` to guarantee this.

> **Warning**: `alignas` can only increase alignment requirements, not decrease them. `alignas(1) int` won't actually make `int` 1-byte aligned—the compiler will ignore this request because `int`'s natural alignment is 4. If you try to write a value like `alignas(3)` that isn't a power of two, the compiler will error directly.

Additionally, C++17 introduced `std::align` (deprecated since C++23, recommend using `std::assume_aligned` instead), and the `std::align` function in `<memory>` is used to find an address satisfying alignment requirements within a given buffer at runtime. These tools are very useful when implementing custom allocators or type-erased containers (like the underlying storage of `std::any`).

## Packing Structs—The Double-Edged Sword of pragma pack

Sometimes you truly want no padding—such as in network protocol headers, binary file formats, or structs that map one-to-one to hardware registers. In these cases, you can use `#pragma pack` to tell the compiler: don't add padding.

```cpp
#pragma pack(push, 1)
struct PackedStruct {
    char a;
    int b;
    char c;
};
#pragma pack(pop)
```

`sizeof(PackedStruct)` is now `6`, with absolutely no padding. Every member sits immediately next to the previous one, and the memory layout is completely compact. This is very common in network programming and binary file parsing.

But `#pragma pack` is a true double-edged sword, and the cost of using it poorly can be steep.

> **Warning**: Taking a reference to a member of a packed struct is undefined behavior. Consider `PackedStruct`—`b` is at offset 3, not a multiple of 4, yet `int&` requires the address it points to be 4-byte aligned. The compiler might generate SIMD instructions assuming the address is aligned, causing the program to crash on some architectures or silently return incorrect data on others. If you need to read a member from a packed struct, copy its value to a local variable first; do not bind a reference directly.
>
> **Warning**: Accessing misaligned members in packed structs can trigger bus errors on some platforms. While x86 hardware handles misaligned access, performance suffers. If you just want to reduce struct size, prioritize adjusting member order over using `#pragma pack`. `#pragma pack` should only be used for scenarios where "the memory layout must strictly match an external format."

## Hands-on Verification—alignment.cpp

Now let's synthesize the knowledge above and write a complete program to verify various alignment behaviors. This program defines multiple structs, prints their `sizeof` and member offsets, giving you an intuitive view of where padding bytes are located, while demonstrating how to optimize layout by reordering members.

```cpp
#include <iostream>
#include <cstddef>

// Standard layout: likely to have padding
struct Standard {
    char a;     // 1 byte
    // 3 bytes padding
    int b;      // 4 bytes
    char c;     // 1 byte
    // 3 bytes padding
};

// Optimized layout: minimal padding
struct Optimized {
    int b;      // 4 bytes
    char a;     // 1 byte
    char c;     // 1 byte
    // 2 bytes padding
};

// Extreme case: double + char
struct Extreme {
    char a;     // 1 byte
    // 7 bytes padding
    double d;   // 8 bytes
    char c;     // 1 byte
    // 7 bytes padding
};

// Optimized extreme case
struct ExtremeOptimized {
    double d;   // 8 bytes
    char a;     // 1 byte
    char c;     // 1 byte
    // 6 bytes padding
};

#pragma pack(push, 1)
struct Packed {
    char a;
    int b;
    char c;
};
#pragma pack(pop)

struct alignas(16) OverAligned {
    int a;
    int b;
    int c;
};

int main() {
    std::cout << "Standard: " << sizeof(Standard) << " bytes\n";
    std::cout << "  a: " << offsetof(Standard, a) << "\n";
    std::cout << "  b: " << offsetof(Standard, b) << "\n";
    std::cout << "  c: " << offsetof(Standard, c) << "\n\n";

    std::cout << "Optimized: " << sizeof(Optimized) << " bytes\n";
    std::cout << "  b: " << offsetof(Optimized, b) << "\n";
    std::cout << "  a: " << offsetof(Optimized, a) << "\n";
    std::cout << "  c: " << offsetof(Optimized, c) << "\n\n";

    std::cout << "Extreme: " << sizeof(Extreme) << " bytes\n";
    std::cout << "  a: " << offsetof(Extreme, a) << "\n";
    std::cout << "  d: " << offsetof(Extreme, d) << "\n";
    std::cout << "  c: " << offsetof(Extreme, c) << "\n\n";

    std::cout << "ExtremeOptimized: " << sizeof(ExtremeOptimized) << " bytes\n";
    std::cout << "  d: " << offsetof(ExtremeOptimized, d) << "\n";
    std::cout << "  a: " << offsetof(ExtremeOptimized, a) << "\n";
    std::cout << "  c: " << offsetof(ExtremeOptimized, c) << "\n\n";

    std::cout << "Packed: " << sizeof(Packed) << " bytes\n";
    std::cout << "  a: " << offsetof(Packed, a) << "\n";
    std::cout << "  b: " << offsetof(Packed, b) << "\n";
    std::cout << "  c: " << offsetof(Packed, c) << "\n\n";

    std::cout << "OverAligned: " << sizeof(OverAligned) << " bytes\n";
    std::cout << "  alignof: " << alignof(OverAligned) << "\n";
}
```

After compiling and running, you will see output similar to this:

```text
Standard: 12 bytes
  a: 0
  b: 4
  c: 8

Optimized: 8 bytes
  b: 0
  a: 4
  c: 5

Extreme: 24 bytes
  a: 0
  d: 8
  c: 16

ExtremeOptimized: 16 bytes
  d: 0
  a: 8
  c: 9

Packed: 6 bytes
  a: 0
  b: 1
  c: 5

OverAligned: 16 bytes
  alignof: 16
```

`Standard` has 6 bytes of padding (3 bytes after `a`, 3 bytes after `c`), while `Optimized` has only 2 bytes of tail padding. The `Extreme` case is even more dramatic—7 bytes of padding are stuffed between a `char` and a `double`, inflating the total size to 24 bytes, whereas `ExtremeOptimized` only needs 16 bytes. This is the power of member ordering: the same data, different arrangements, can result in a memory footprint difference of 33% or more.

`Packed` demonstrates the effect of packing: no padding, size exactly equal to the sum of all members, but note its alignment requirement became 1—meaning if it appears inside another struct, it can be placed anywhere. `OverAligned` shows the effect of `alignas(16)`: although the data is only 12 bytes, the entire struct is forced to align to a 16-byte boundary, and the size is also 16.

## Exercises

### Exercise 1: Manual sizeof Calculation

Without compiling, predict the `sizeof` and offset of each member for the following structs:

```cpp
struct A {
    char a;
    short b;
    int c;
};

struct B {
    double a;
    char b;
    int c;
    short d;
};

struct C {
    char a;
    double b;
    char c[5];
    int d;
};
```

Then verify your predictions with code.

### Exercise 2: Optimize Struct Layout

What is the `sizeof` of the following struct on a 64-bit system? Reorder the members to make it as small as possible:

```cpp
struct Heavy {
    char a;
    void* ptr;
    int b;
    char c;
    double d;
    short e;
};
```

### Exercise 3: Allocate Aligned Buffers for SIMD

Write a function that allocates a 32-byte aligned `double` array (at least 8 elements), loads data using AVX's `_mm256_load_pd`, and prints the result. Hint: you can use `alignas(32)` to declare a stack array, or use `aligned_alloc` to allocate on the heap.

## Summary

In this chapter, we revealed the secrets behind `sizeof`. CPUs access data most efficiently at aligned addresses, so compilers insert padding bytes between struct members to satisfy alignment requirements. Every type has a natural alignment value (usually equal to its size), a struct's alignment equals that of its largest member, and its total size must be a multiple of that alignment value. Member declaration order directly impacts padding amount—placing members with larger alignment requirements first and those with smaller requirements later can significantly reduce struct size. `alignas` allows us to manually specify stricter alignment requirements, which is indispensable for SIMD, cache line optimization, and hardware interaction. `#pragma pack` can eliminate padding for compact layouts, but at the cost of potential unaligned access risks.

With this, the content of Volume 1 is fully concluded. We have journeyed from C++ basic types, control flow, and functions to pointers, arrays, memory layout, and alignment, covering the foundation of C++ programming. This knowledge will recur repeatedly in subsequent studies—understanding memory layout and alignment allows you to grasp why `std::unique_ptr` overhead is almost zero when learning move semantics and smart pointers in Volume 2; understanding the difference between stack and heap allows you to immediately appreciate how RAII can cure memory leaks. In Volume 2, we will enter the core features of Modern C++: RAII, move semantics, smart pointers, lambdas, constexpr—these are the key forces that transform C++ from "C with Classes" into a modern system programming language. See you in Volume 2.
