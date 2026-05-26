---
title: Memory Alignment and Padding
description: Understand alignment rules and `sizeof` calculation methods, and master
  the usage of `alignas`/`alignof`.
chapter: 12
order: 3
difficulty: intermediate
reading_time_minutes: 12
platform: host
prerequisites:
- 动态内存管理
tags:
- cpp-modern
- host
- intermediate
- 进阶
cpp_standard:
- 11
- 14
- 17
- 20
translation:
  source: documents/vol1-fundamentals/ch12/03-alignment-padding.md
  source_hash: 6f1478d6dc0607248ffff63fc6dc72bd6c98609b6de76c8d8e3888474775e17c
  translated_at: '2026-05-26T11:00:46.630169+00:00'
  engine: anthropic
  token_count: 2720
---
# Memory Alignment and Padding

In the previous chapter, we divided a program's memory space into four major regions: the stack, the heap, the static storage, and the code segment, clarifying where data "lives" and how long it "survives." Now let's look one level deeper—even when data resides in the same memory region, it can't just be arranged arbitrarily. If you've written C++ for a while, you've probably encountered this puzzle: a struct clearly has only three members, but the `sizeof` result is significantly larger than the sum of their sizes. What in the world happened to those extra bytes?

Ta-da! The answer is the theme of this chapter: **alignment and padding**. To satisfy the CPU's memory access efficiency requirements, the compiler inserts "blank" bytes between struct members, aligning each member to a specific address boundary. These blank bytes store no valid data, but they genuinely occupy memory space. Understanding alignment rules not only allows you to accurately predict `sizeof` results, but also enables you to reduce struct sizes by adjusting member order in performance-sensitive scenarios. This optimization requires no changes to your logic code—simply rearranging the member declarations can save a considerable amount of memory.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Explain why CPUs require memory alignment, and what happens when data is unaligned
> - [ ] Manually calculate the `sizeof` result for any struct
> - [ ] Use `alignas` and `alignof` to control and query alignment requirements
> - [ ] Optimize struct memory layout by adjusting member order
> - [ ] Understand the purpose and potential risks of `#pragma pack`

## Alignment — The Unspoken Contract Between CPU and Memory

To understand alignment, we first need to look at how the CPU accesses memory. Many people assume the CPU can freely read and write data at any address on a byte-by-byte basis—from a programmer's perspective, this seems true, but the underlying hardware doesn't actually work this way. When a modern CPU accesses memory via a bus, it typically transfers data in units of a word. A 32-bit CPU can read or write 4 bytes at a time, and a 64-bit CPU can read or write 8 bytes at a time. Furthermore, the hardware often requires the starting address of such an access to be an integer multiple of the word size.

You can think of memory as a row of storage lockers, each 4 compartments wide. If you need to retrieve an item that takes up 4 compartments (i.e., a `int`), the fastest way is to have it start exactly at the beginning of a locker, so you can get it all in one open. But if this `int` straddles the boundary between two lockers—the first two compartments in the first locker, the last two in the second—the CPU has to open two lockers, extract parts from each, and stitch them together before returning the result. Certain architectures (like ARM) will even outright refuse such cross-boundary accesses and throw a hardware exception.

This is the underlying reason for alignment: **CPUs access data most efficiently at aligned addresses; accessing unaligned addresses either slows things down or triggers an immediate error**. Therefore, when arranging a struct's memory layout, the compiler proactively places each member at a position that satisfies its alignment requirement, and the extra space in between becomes padding bytes.

## Alignment Rules — How the Compiler Fills in the Blanks

Every fundamental type has a **natural alignment requirement**, which usually equals its size. `char` has 1-byte alignment (can go anywhere), `int` has 4-byte alignment (the address must be a multiple of 4), and `double` has 8-byte alignment (the address must be a multiple of 8). Pointers have 8-byte alignment on 64-bit systems and 4-byte alignment on 32-bit systems.

For a struct, the compiler follows three rules:

First, each member of the struct must be placed at an address that is an integer multiple of its natural alignment requirement. If the end position of the previous member doesn't satisfy the next member's alignment requirement, the compiler inserts padding bytes between them until the address meets the condition.

Second, the overall size of the struct itself must be an integer multiple of its largest member's alignment requirement. In other words, if a struct contains a `double` (8-byte alignment), the total size of the struct must be a multiple of 8—even if there is leftover space after the last member, padding bytes must be added to fill it.

Third, the struct's own alignment requirement equals the alignment requirement of its largest member. This rule affects "where this struct should be placed when it acts as a member of another struct."

This sounds a bit abstract, so let's jump straight into the code.

## The Truth About sizeof — Where Padding Bytes Hide

Let's look at a classic example, the kind you might have seen in interview questions:

```cpp
struct BadLayout {
    char a;   // 1 字节
    int  b;   // 4 字节
    char c;   // 1 字节
};
```

The three members add up to `1 + 4 + 1 = 6` bytes, but `sizeof(BadLayout)` is **12** on most platforms. The extra 6 bytes are all padding. Let's analyze member by member to see exactly what the compiler did.

`a` is a `char` with 1-byte alignment, placed at offset 0, taking up 1 byte. Next comes `b`, which is a `int` requiring 4-byte alignment—meaning its starting offset must be a multiple of 4. But `a` only reaches offset 1, so the compiler inserts 3 padding bytes at offsets 1, 2, and 3, placing `b` at offset 4, where it occupies offsets 4, 5, 6, and 7. Then comes `c`; `char` only needs 1-byte alignment, so following right after `b` is fine, placing it at offset 8 and taking up 1 byte.

So far, 9 bytes have been used. But don't forget the second rule—the overall size of the struct must be an integer multiple of the largest member's alignment requirement. Here, the maximum alignment is the 4-byte requirement of `int`, so the struct size must be a multiple of 4. Since 9 is not a multiple of 4, the compiler adds 3 more bytes of padding at the end to round it up to 12. If we draw it as a diagram, it looks like this:

```text
偏移量:  0   1   2   3   4   5   6   7   8   9  10  11
         +---+---+---+---+---+---+---+---+---+---+---+---+
BadLayout| a | pad   pad   pad |   b (4 bytes)   | c | pad   pad   pad |
         +---+---+---+---+---+---+---+---+---+---+---+---+
```

> **Pitfall Warning**: Member declaration order directly affects the amount of padding and the struct's size. This is a common interview topic, and an even more common trap in practice—especially in scenarios like network protocols and file formats where precise control over memory layout is required. Failing to pay attention to member order can cause data to be misaligned. More critically, if you `memcpy` a struct directly and send it out, the receiving end might parse it with a different compiler where the padding rules differ, causing the data to be completely out of sync.

Now let's rearrange the member order, putting the larger ones first:

```cpp
struct GoodLayout {
    int  b;   // 4 字节
    char a;   // 1 字节
    char c;   // 1 字节
};
```

`b` is at offset 0, taking 4 bytes. `a` is at offset 4, with 1-byte alignment, no problem. `c` follows immediately at offset 5. That's 6 bytes used so far, and the overall size needs to be a multiple of 4—so we pad 2 bytes to reach 8. `sizeof(GoodLayout)` is **8**, a third less than the previous 12.

```text
偏移量:  0   1   2   3   4   5   6   7
         +---+---+---+---+---+---+---+---+
GoodLayout|   b (4 bytes)   | a | c | pad  pad |
         +---+---+---+---+---+---+---+---+
```

Simply by changing the member declaration order, without altering any logic, the struct shed 4 bytes. If your program has a million such objects, that saves 4 MB of memory. So a practical rule of thumb is: **arrange members in descending order of alignment requirements**—put `double` and `int64_t` first, then `int` and `float`, and finally `char` and `bool`.

## alignas and alignof — Manually Controlling Alignment

The compiler's default alignment rules are sufficient in the vast majority of cases, but some scenarios require manual intervention. C++11 introduced two keywords, `alignas` and `alignof`, used to specify and query alignment requirements, respectively.

The usage of `alignof` is simple—give it a type, and it returns that type's alignment requirement (in bytes). `alignof(int)` is 4, `alignof(double)` is 8, and `alignof(char)` is 1. You can even use it on structs: `alignof(GoodLayout)` returns 4, because its largest member, `int`, has 4-byte alignment.

`alignas`, on the other hand, is used to forcefully specify alignment. It can be applied to variable declarations or type definitions:

```cpp
// 强制单个变量按 16 字节对齐
alignas(16) char buffer[1024];

// 强制结构体类型按 64 字节对齐（一个缓存行的大小）
struct alignas(64) CacheLine {
    int data[14];  // 56 字节 + 编译器自动补齐到 64
};
```

There are three typical use cases for `alignas`. The first is SIMD instructions—SSE requires 16-byte aligned operands, AVX requires 32-byte alignment, and AVX-512 requires 64-byte alignment. If your data isn't aligned to the required boundary, SIMD load instructions will throw a hardware exception, crashing the program on the spot. The second is cache line optimization—modern CPU cache lines are typically 64 bytes. If your data structure spans two cache lines, a single read will trigger two cache misses. Aligning hot data to cache line boundaries avoids this "false sharing." The third is hardware interaction—certain DMA (Direct Memory Access) controllers or peripherals require the physical address of a buffer to have specific alignment, which is where `alignas` comes in.

> **Pitfall Warning**: `alignas` can only increase alignment requirements, not decrease them. `alignas(1) int x;` won't actually make `int` 1-byte aligned—the compiler will ignore this request because the natural alignment of `int` is 4. If you try to write a value that isn't a power of two, like `alignas(3)`, the compiler will throw an error directly.

Additionally, C++17 introduced `std::aligned_storage` (deprecated as of C++23; it's recommended to use `alignas` directly), as well as the `std::align` function in `<memory>`, which is used to find an address within a given buffer at runtime that satisfies an alignment requirement. These tools are extremely useful when implementing custom allocators or type-erased containers (like the underlying storage of `std::any`).

## Packing Structs — The Double-Edged Sword of pragma pack

Sometimes you genuinely don't want any padding—such as for network protocol header structs, binary file formats, or structs that map one-to-one with hardware registers. In these cases, you can use `#pragma pack` to tell the compiler: don't add any padding.

```cpp
#pragma pack(push, 1)  // 保存当前对齐设置，然后设为 1 字节对齐
struct RawHeader {
    uint8_t  version;   // 偏移 0
    uint16_t length;    // 偏移 1（不再是 2 的倍数！）
    uint32_t checksum;  // 偏移 3（不再是 4 的倍数！）
};
#pragma pack(pop)       // 恢复之前的对齐设置
```

`sizeof(RawHeader)` is now `1 + 2 + 4 = 7`, with absolutely no padding. Each member sits snugly against the previous one, resulting in a completely compact memory layout. This pattern is extremely common in network programming and binary file parsing.

But `#pragma pack` is a true double-edged sword, and the cost of using it improperly can be quite severe.

> **Pitfall Warning**: Taking a reference to a member of a packed struct is undefined behavior (UB). Consider `uint32_t& ref = header.checksum;`—`checksum` is at offset 3, which is not a multiple of 4, yet `uint32_t&` requires the address it points to to be 4-byte aligned. The compiler might generate SIMD instructions assuming the address is aligned, causing the program to crash on certain architectures or silently return incorrect data on others. If you need to read a member from a packed struct, copy its value to a local variable first before using it; do not bind a reference directly.
>
> **Pitfall Warning**: Accessing unaligned members in a packed struct can trigger a bus error on certain platforms. Although x86 hardware handles unaligned accesses, there is a performance penalty. If your goal is simply to reduce struct size, prioritize adjusting member order over using `#pragma pack`. `#pragma pack` should only be used in scenarios where "the memory layout must precisely match an external format."

## Hands-on Verification — alignment.cpp

Now let's combine the knowledge above and write a complete program to verify various alignment behaviors. This program defines multiple structs, prints their `sizeof` and member offsets, lets you visually see where the padding bytes are, and demonstrates how to optimize layout by rearranging members.

```cpp
// alignment.cpp
// 编译: g++ -std=c++17 -O0 alignment.cpp -o alignment && ./alignment

#include <cstddef>
#include <cstdint>
#include <iostream>

// --- 结构体定义 ---

struct BadLayout {
    char  a;
    int   b;
    char  c;
};

struct GoodLayout {
    int   b;
    char  a;
    char  c;
};

struct alignas(16) AlignedBuffer {
    int data[3];  // 12 字节，补齐到 16
};

#pragma pack(push, 1)
struct PackedHeader {
    uint8_t  version;
    uint16_t length;
    uint32_t crc;
};
#pragma pack(pop)

struct MixedTypes {
    char    flag;
    double  value;
    int     count;
    short   id;
};

struct ReorderedMixed {
    double  value;
    int     count;
    short   id;
    char    flag;
};

// --- 工具函数 ---

/// 打印结构体信息和成员偏移量
template <typename T>
void print_struct_info(const char* name)
{
    std::cout << name << ":\n";
    std::cout << "  sizeof = " << sizeof(T)
              << ", alignof = " << alignof(T) << "\n";
}

int main()
{
    std::cout << "=== sizeof 和 alignof 对比 ===\n\n";

    print_struct_info<BadLayout>("BadLayout");
    std::cout << "  偏移量: a=" << offsetof(BadLayout, a)
              << ", b=" << offsetof(BadLayout, b)
              << ", c=" << offsetof(BadLayout, c) << "\n\n";

    print_struct_info<GoodLayout>("GoodLayout");
    std::cout << "  偏移量: b=" << offsetof(GoodLayout, b)
              << ", a=" << offsetof(GoodLayout, a)
              << ", c=" << offsetof(GoodLayout, c) << "\n\n";

    print_struct_info<AlignedBuffer>("AlignedBuffer");
    std::cout << "  偏移量: data=" << offsetof(AlignedBuffer, data) << "\n\n";

    print_struct_info<PackedHeader>("PackedHeader");
    std::cout << "  偏移量: version=" << offsetof(PackedHeader, version)
              << ", length=" << offsetof(PackedHeader, length)
              << ", crc=" << offsetof(PackedHeader, crc) << "\n\n";

    print_struct_info<MixedTypes>("MixedTypes");
    std::cout << "  偏移量: flag=" << offsetof(MixedTypes, flag)
              << ", value=" << offsetof(MixedTypes, value)
              << ", count=" << offsetof(MixedTypes, count)
              << ", id=" << offsetof(MixedTypes, id) << "\n\n";

    print_struct_info<ReorderedMixed>("ReorderedMixed");
    std::cout << "  偏移量: value=" << offsetof(ReorderedMixed, value)
              << ", count=" << offsetof(ReorderedMixed, count)
              << ", id=" << offsetof(ReorderedMixed, id)
              << ", flag=" << offsetof(ReorderedMixed, flag) << "\n\n";

    std::cout << "=== 优化效果 ===\n";
    std::cout << "BadLayout  -> GoodLayout: "
              << sizeof(BadLayout) << " -> " << sizeof(GoodLayout)
              << " (节省 " << sizeof(BadLayout) - sizeof(GoodLayout)
              << " 字节)\n";
    std::cout << "MixedTypes -> ReorderedMixed: "
              << sizeof(MixedTypes) << " -> " << sizeof(ReorderedMixed)
              << " (节省 " << sizeof(MixedTypes) - sizeof(ReorderedMixed)
              << " 字节)\n";

    return 0;
}
```

After compiling and running, you'll see output similar to this:

```text
=== sizeof 和 alignof 对比 ===

BadLayout:
  sizeof = 12, alignof = 4
  偏移量: a=0, b=4, c=8

GoodLayout:
  sizeof = 8, alignof = 4
  偏移量: b=0, a=4, c=5

AlignedBuffer:
  sizeof = 16, alignof = 16
  偏移量: data=0

PackedHeader:
  sizeof = 7, alignof = 1
  偏移量: version=0, length=1, crc=3

MixedTypes:
  sizeof = 24, alignof = 8
  偏移量: flag=0, value=8, count=16, id=20

ReorderedMixed:
  sizeof = 16, alignof = 8
  偏移量: value=0, count=8, id=12, flag=14

=== 优化效果 ===
BadLayout  -> GoodLayout: 12 -> 8 (节省 4 字节)
MixedTypes -> ReorderedMixed: 24 -> 16 (节省 8 字节)
```

`BadLayout` has 6 bytes of padding (3 bytes after `a`, 3 bytes after `c`), while `GoodLayout` only has 2 bytes of tail padding. The situation with `MixedTypes` is even more extreme—7 bytes of padding are stuffed between a `char` and a `double`, bloating the total size to 24 bytes, whereas `ReorderedMixed` only needs 16 bytes. This is the power of member sorting: the same data, arranged differently, can lead to a memory footprint difference of 33% or more.

`PackedHeader` demonstrates the effect of packing: there is no padding at all, and the size is exactly the sum of all members. Note, however, that its alignment requirement becomes 1—meaning if it appears inside another struct, it can be placed anywhere. `AlignedBuffer` showcases the effect of `alignas(16)`: although the data is only 12 bytes, the entire struct is forcefully aligned to a 16-byte boundary, and its size is also 16.

## Exercises

### Exercise 1: Manually Calculate sizeof

Without compiling, predict the `sizeof` and the offset of each member for the following structs:

```cpp
struct X {
    char   a;
    double b;
    int    c;
};

struct Y {
    double a;
    int    b;
    char   c;
};

struct Z {
    char a;
    char b;
    int  c;
    int  d;
};
```

Then use code to verify your predictions.

### Exercise 2: Optimize Struct Layout

What is the `sizeof` of the following struct on a 64-bit system? Rearrange the members to make it as small as possible:

```cpp
struct Monster {
    bool     is_alive;
    double   health;
    char     name[16];
    int      level;
    float    speed;
    uint64_t experience;
};
```

### Exercise 3: Allocate an Aligned Buffer for SIMD

Write a function that allocates a 32-byte aligned `float` array (at least 8 elements), loads data using AVX's `_mm256_load_ps`, and prints the result. Hint: you can use `alignas(32)` to declare an array on the stack, or use `std::aligned_alloc` to allocate on the heap.

## Summary

In this chapter, we uncovered the secrets behind `sizeof`. CPUs access data most efficiently at aligned addresses, so the compiler inserts padding bytes between struct members to satisfy alignment requirements. Every type has a natural alignment value (usually equal to its size), a struct's alignment equals that of its largest member, and its overall size must be a multiple of this alignment value. Member declaration order directly affects the amount of padding—putting members with larger alignment requirements first and those with smaller requirements last can significantly reduce the struct's size. `alignas` allows us to manually specify stricter alignment requirements, making it indispensable for SIMD, cache line optimization, and hardware interaction scenarios. `#pragma pack` can eliminate padding to achieve a compact layout, but the trade-off is the potential risk of unaligned access.

With this, the content of Volume One is fully complete. We've journeyed from C++'s basic types, control flow, and functions all the way to pointers, arrays, memory layout, and alignment, covering the very foundation of C++ programming. This knowledge will recur repeatedly in later studies—by understanding memory layout and alignment, you'll grasp why the overhead of `unique_ptr` is nearly zero when you learn about move semantics and smart pointers in Volume Two. By understanding the difference between the stack and the heap, you'll immediately see why RAII can cure memory leaks when you study it. In Volume Two, we will dive into the core features of Modern C++: RAII, move semantics, smart pointers, lambda expressions, and constexpr—these are the key forces that transform C++ from "C with classes" into a modern systems programming language. See you in Volume Two.
