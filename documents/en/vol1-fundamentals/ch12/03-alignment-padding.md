---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand alignment rules and how sizeof is computed, and get comfortable with alignas/alignof
difficulty: intermediate
order: 3
platform: host
prerequisites:
- Dynamic Memory Management
reading_time_minutes: 15
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Memory Alignment and Padding
translation:
  source: documents/vol1-fundamentals/ch12/03-alignment-padding.md
  source_hash: bf08d799fcc7a5574426b6e4697b7b824776de0b3ba46553ca4c5c812bd163a6
  translated_at: '2026-09-25T12:09:34+00:00'
  engine: anthropic
  token_count: 8500
---
# Memory Alignment and Padding: Where Did the Extra Bytes Go

In the previous chapter we split a program's memory space into four major regions—stack, heap, static storage, and code segment—and figured out where data "lives" and how long it "survives". Now let's go one level deeper: even when data sits in the same memory region, it can't just be laid out any way it pleases. If you've written C++ for a while, you've most likely run into this puzzle: a struct has only three members, yet `sizeof` reports considerably more than the sum of their sizes. Spooky, isn't it—where did those extra bytes go?

Ta-da! The answer is the subject of this chapter: **alignment and padding**. To satisfy the CPU's efficiency requirements for memory access, the compiler inserts "blank" bytes between the members of a struct, aligning each member to specific address boundaries. These blank bytes store no useful data, yet they very much occupy memory space. Understanding alignment rules lets you predict `sizeof` results accurately, and in performance-sensitive scenarios it lets you shrink a struct by adjusting the order of its members. This optimization requires changing not a single line of logic—just swapping the order of member declarations can save a substantial amount of memory.

## Alignment—The Tacit Understanding Between CPU and Memory

To understand alignment, we first need to look at how the CPU accesses memory. Many people assume the CPU can freely read and write data at any address byte by byte—from the programmer's viewpoint that is indeed how it looks, but the understanding is not quite right; the underlying hardware doesn't work that way. When a modern CPU accesses memory over the bus, it usually transfers data in units of words. A 32-bit CPU reads or writes 4 bytes at a time, a 64-bit CPU 8 bytes at a time, and the hardware often requires the starting address of that access to be a multiple of the word size.

Picture memory as a row of lockers, each locker 4 slots wide. To grab an item that occupies 4 slots (that is, an `int`), the fastest way is to place it starting exactly at the beginning of a locker, so opening one locker retrieves it all in one go. But if this `int` straddles the boundary between two lockers—the first two slots in one locker, the last two in the next—the CPU has to open two lockers, fetch a piece from each, and stitch them together before returning the value. Some architectures (ARM, for example) outright refuse such boundary-crossing accesses and raise a hardware exception.

And there we have the underlying reason alignment exists: **the CPU accesses data at aligned addresses most efficiently; accessing an unaligned address is either slower or fails outright**. So when the compiler arranges a struct's memory layout, it proactively places every member at a position that satisfies that member's alignment requirement—the leftover space in between is padding bytes.

## Alignment Rules—How the Compiler Fills in the Blanks

Every fundamental type has a **natural alignment**, which usually equals the size of that type. `char` is 1-byte aligned (it can go anywhere), `int` is 4-byte aligned (its address must be a multiple of 4), and `double` is 8-byte aligned (its address must be a multiple of 8). Pointers are 8-byte aligned on 64-bit systems and 4-byte aligned on 32-bit systems.

For a struct, the compiler follows three rules. First: every member of the struct must be placed at an address that is a multiple of its own natural alignment. If the position where the previous member ends doesn't satisfy the next member's alignment requirement, the compiler inserts padding bytes between the two until the address qualifies. Second: the struct's overall size must be a multiple of the alignment requirement of its largest member. In other words, if the struct contains a `double` (8-byte aligned), the whole struct's size must be a multiple of 8—even if there is spare room after the last member, it gets padded up to a multiple. Third: the struct's own alignment requirement equals that of its largest member. This rule governs "where this struct should be placed when it serves as a member of another struct".

That sounds a bit abstract, so let's look at code directly.

## The Truth About sizeof—Where the Padding Bytes Hide

Here is a classic example, possibly one you have seen in interview questions:

```cpp
struct BadLayout {
    char a;   // 1 byte
    int  b;   // 4 bytes
    char c;   // 1 byte
};
```

The three members add up to `1 + 4 + 1 = 6` bytes, yet on most platforms `sizeof(BadLayout)` is **12**. The 6 extra bytes are all padding. Let's analyze member by member what the compiler actually did.

`a` is a `char`, 1-byte aligned, so it sits at offset 0 and occupies 1 byte. Next comes `b`, an `int` needing 4-byte alignment—which means its starting offset must be a multiple of 4. But `a` only reaches offset 1, so the compiler inserts 3 padding bytes at offsets 1, 2, and 3, placing `b` at offset 4, where it occupies offsets 4, 5, 6, and 7. Then comes `c`; a `char` needs only 1-byte alignment, so following `b` is no problem—it goes at offset 8 and takes 1 byte.

So far we've used 9 bytes. But don't forget the second rule: the struct's overall size must be a multiple of its largest member's alignment requirement. Here the largest alignment is `int`'s 4 bytes, so the struct's size must be a multiple of 4. 9 isn't a multiple of 4, so the compiler tacks on 3 more bytes at the end, rounding up to 12. Drawn as a picture, it looks like this:

```text
Offset:   0   1   2   3   4   5   6   7   8   9  10  11
         +---+---+---+---+---+---+---+---+---+---+---+---+
BadLayout| a | pad   pad   pad |   b (4 bytes)   | c | pad   pad   pad |
         +---+---+---+---+---+---+---+---+---+---+---+---+
```

Member declaration order directly affects the amount of padding and the size of the struct. It's a frequent interview topic and an even more frequent real-world stumble—in particular in scenarios such as network protocols and file formats where the memory layout must be precisely controlled, ignoring member order can make the data not line up. More critically, if we `memcpy` a struct out directly and the receiving end parses it with a different compiler, the padding rules may differ, and the data ends up misaligned.

Now let's adjust the member order, putting the larger ones first:

```cpp
struct GoodLayout {
    int  b;   // 4 bytes
    char a;   // 1 byte
    char c;   // 1 byte
};
```

`b` sits at offset 0, taking 4 bytes; `a` goes at offset 4—1-byte aligned, no problem. `c` follows right behind at offset 5. That's 6 bytes so far, and the overall size must be a multiple of 4—so 2 bytes of padding bring it to 8. `sizeof(GoodLayout)` is **8**, one-third less than the 12 from before.

```text
Offset:   0   1   2   3   4   5   6   7
         +---+---+---+---+---+---+---+---+
GoodLayout|   b (4 bytes)   | a | c | pad  pad |
         +---+---+---+---+---+---+---+---+
```

Merely by swapping the declaration order—without touching any logic—the struct slimmed down by 4 bytes. If our program holds a million such objects, that's 4 MB of memory saved. So a practical rule of thumb: **order members from the largest alignment requirement to the smallest**—put `double` and `int64_t` first, then `int` and `float`, and `char` and `bool` last.

## alignas and alignof—Taking Manual Control of Alignment

The compiler's default alignment rules are good enough in the vast majority of cases, but some scenarios call for manual intervention. C++11 introduced the two keywords `alignas` and `alignof`, for specifying an alignment requirement and querying one, respectively.

`alignof` is simple to use: give it a type, and it returns that type's alignment requirement in bytes. `alignof(int)` is 4, `alignof(double)` is 8, `alignof(char)` is 1. You can even apply it to structs: `alignof(GoodLayout)` returns 4, because its largest member, `int`, is 4-byte aligned.

`alignas`, on the other hand, forces a specific alignment. It can be applied to variable declarations as well as type definitions:

```cpp
// Force a single variable to be 16-byte aligned
alignas(16) char buffer[1024];

// Force a struct type to be 64-byte aligned (the size of one cache line)
struct alignas(64) CacheLine {
    int data[14];  // 56 bytes + the compiler pads it up to 64
};
```

`alignas` has three most typical application scenarios. The first is SIMD instructions: SSE requires operands to be 16-byte aligned, AVX requires 32-byte alignment, and AVX-512 requires 64-byte alignment. If our data isn't aligned to the required boundary, the SIMD load instruction raises a hardware exception outright and the program crashes on the spot. The second is cache line optimization: a modern CPU's cache line is usually 64 bytes; if our data structure straddles two cache lines, a single read triggers two cache misses, and aligning hot data to cache line boundaries avoids this kind of "false sharing". The third is hardware interaction: some DMA controllers or peripherals require the buffer's physical address to have a specific alignment, and `alignas` is what guarantees it.

`alignas` can only increase an alignment requirement, never decrease it. `alignas(1) int x;` won't actually make the `int` 1-byte aligned; the compiler ignores the request, because an `int`'s natural alignment is simply 4. And if we try to write something like `alignas(3)`—a value that isn't a power of two—the compiler rejects it outright.

Also worth a look: `std::aligned_storage`, introduced in C++17 (deprecated as of C++23; using `alignas` directly is recommended), and the `std::align` function in `<memory>`, which finds an address satisfying an alignment requirement within a given buffer at runtime. These tools are extremely practical when implementing custom allocators or type-erased containers (such as the underlying storage of `std::any`).

## Packed Structs—The Double-Edged Sword of pragma pack

Sometimes we genuinely want no padding at all—for instance, the header structs of network protocols, binary file formats, or structs that map one-to-one onto hardware registers. In such cases, `#pragma pack` can tell the compiler: don't add any padding for me.

```cpp
#pragma pack(push, 1)  // save the current alignment setting, then switch to 1-byte alignment
struct RawHeader {
    uint8_t  version;   // offset 0
    uint16_t length;    // offset 1 (no longer a multiple of 2!)
    uint32_t checksum;  // offset 3 (no longer a multiple of 4!)
};
#pragma pack(pop)       // restore the previous alignment setting
```

Now `sizeof(RawHeader)` is `1 + 2 + 4 = 7`, with no padding whatsoever. Every member sits tightly against the previous one—a completely compact memory layout. This style is very common in network programming and binary file parsing.

But `#pragma pack` is a true double-edged sword, and wielding it badly exacts a painful price.

Taking a reference to a member of a packed struct is undefined behavior. Consider `uint32_t& ref = header.checksum;`: `checksum` sits at offset 3, not a multiple of 4, while a `uint32_t&` requires the address it points to to be 4-byte aligned. The compiler may emit SIMD instructions that assume the address is already aligned, crashing the program on some architectures or silently returning wrong data on others. When we need to read a member of a packed struct, copy its value into a local variable first and use that—don't bind a reference to it directly.

On some platforms, accessing an unaligned member of a packed struct triggers a bus error; on x86 the hardware does handle unaligned accesses, but performance drops. If all we want is a smaller struct, reordering members should be the first resort, not `#pragma pack`. `#pragma pack` should be reserved for scenarios where "the memory layout must exactly match an external format".

## Hands-On Verification—alignment.cpp

Now let's put all of the above together and write a complete program to verify various alignment behaviors. This program defines several structs and prints their `sizeof` and member offsets, letting you see directly where the padding bytes sit, while also demonstrating how to optimize the layout by reordering members.

```cpp
// alignment.cpp
// Compile: g++ -std=c++17 -O0 alignment.cpp -o alignment && ./alignment

#include <cstddef>
#include <cstdint>
#include <iostream>

// --- Struct definitions ---

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
    int data[3];  // 12 bytes, padded to 16
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

// --- Helper functions ---

/// Print struct information and member offsets
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

After compiling and running, we'll see output like this:

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

`BadLayout` carries 6 bytes of padding (3 after `a`, 3 after `c`), while `GoodLayout` has only 2 bytes of trailing padding. `MixedTypes` is even more dramatic—7 bytes of padding get stuffed between a `char` and a `double`, ballooning the whole thing to 24 bytes, whereas `ReorderedMixed` needs only 16. Such is the power of member ordering: the same data, arranged differently, can differ in memory footprint by 33% or even more.

`PackedHeader` shows the effect of packing: no padding at all, and a size exactly equal to the sum of all members—but note that its alignment requirement becomes 1, which means it can be placed at any position when it appears inside another struct. `AlignedBuffer` demonstrates the effect of `alignas(16)`: although the data is only 12 bytes, the entire struct is forced onto a 16-byte boundary, and its size is 16 as well.

## Exercises

### Exercise 1: Compute sizeof by Hand

Without compiling, predict the `sizeof` of each of the following structs and the offset of every member:

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

Then verify your predictions with code.

### Exercise 2: Optimize a Struct Layout

What is the `sizeof` of the following struct on a 64-bit system? Rearrange its members to make it as small as possible:

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

Write a function that allocates a 32-byte-aligned `float` array (at least 8 elements), loads the data with AVX's `_mm256_load_ps`, and prints the result. Hint: you can declare a stack array with `alignas(32)`, or allocate on the heap with `std::aligned_alloc`.
