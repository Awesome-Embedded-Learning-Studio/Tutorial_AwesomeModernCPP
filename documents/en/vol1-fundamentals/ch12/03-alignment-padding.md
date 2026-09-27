---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand the alignment rules and how sizeof is computed, and get comfortable using alignas and alignof
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
  source_hash: 3ae8574b6433360f7c56753b5b1d8af21ad85bb31614dd9113544430a461abea
  translated_at: '2026-09-27T04:15:12+00:00'
  engine: anthropic
  token_count: 4400
---
# Memory Alignment and Padding: Where Did the Extra Bytes Go

In the first article of this chapter we split a program's memory space into four major regions—stack, heap, static storage, and code segment—and worked out where data "lives" and how long it "survives".

But there is another question you might not see coming. **The data is actually not placed arbitrarily—on x64 we have strict memory alignment requirements!** To put that in plain words: even when data all lives in the same memory region, it cannot be arranged just any way it likes. If you have written C++ for a while, you have most likely run into this puzzle: a struct clearly has only three members, yet `sizeof` comes out considerably larger than the sum of the three members' sizes. Spooky, isn't it—where did those extra bytes go?

Ta-da! The answer is the very theme of this chapter: **alignment and padding**. To satisfy the CPU's efficiency requirements for memory access, the compiler inserts "blank" bytes between the members of a struct, aligning each member to specific address boundaries.

These blank bytes **store no useful data whatsoever**, yet they **genuinely take up memory space**. Understanding the alignment rules lets you predict `sizeof` results accurately, and in performance-sensitive scenarios it lets you shrink a struct by adjusting the order of its members. This optimization requires changing not a single line of logic—just swapping the order of member declarations can save a substantial amount of memory.

## Alignment—The Tacit Understanding Between CPU and Memory

To understand alignment, we first need to look at how the CPU accesses memory. Many people assume the CPU can freely read and write data at any address byte by byte. From the programmer's viewpoint that is indeed how it looks, but the underlying hardware does not work that way. When a modern CPU accesses memory over the bus, it usually transfers data in units of words. A 32-bit CPU reads or writes 4 bytes at a time, a 64-bit CPU 8 bytes at a time, and the hardware often requires the starting address of that access to be a multiple of the word size.

Picture memory as a row of lockers, each locker 4 slots wide. To grab an item that occupies 4 slots (that is, an `int`), the fastest way is to place it starting exactly at the beginning of a locker, so opening one locker retrieves it all in one go. But if this `int` straddles the boundary between two lockers—the first two slots in one locker, the last two in the next—the CPU has to open two lockers, fetch a piece from each, and stitch them together before returning the value. Some architectures (ARM, for example) outright refuse such boundary-crossing accesses and raise a hardware exception.

And there we have the underlying reason alignment exists: **the CPU accesses data at aligned addresses most efficiently; accessing an unaligned address is either slower or fails outright**. So when the compiler arranges a struct's memory layout, it proactively places every member at a position that satisfies that member's alignment requirement, and the leftover space in between is the padding bytes.

## The Alignment Rules—How the Compiler Fills in the Blanks

Every fundamental type has a **natural alignment**, which usually equals the size of that type. `char` is 1-byte aligned (it can go anywhere), `int` is 4-byte aligned (the address must be a multiple of 4), and `double` is 8-byte aligned (the address must be a multiple of 8). Pointers are 8-byte aligned on 64-bit systems and 4-byte aligned on 32-bit systems.

Let's watch how the compiler handles this! We trust that is friendlier for everyone. After all, the compiler is the one processing our code, right? When the compiler picks up a struct, the first thing it does is follow three important rules. Round one is this:

> Every member of a struct must be placed at an address that is a multiple of its own natural alignment requirement. If the position where the previous member ends does not satisfy the next member's alignment requirement, the compiler inserts padding bytes between the two until the address meets the condition.

What does that mean? Simply put: the starting address of each member must be **a multiple of its own alignment value**. If the position where the previous member ends does not land on such a multiple, the compiler stuffs padding bytes in between, propping things up until the next member lands on a qualifying address. Let's lay out the smallest possible combination—just two members, a `char` up front and an `int` behind. Simple enough!

```cpp
struct Tiny {
    char a;   // 1-byte alignment
    int  b;   // 4-byte alignment
};
```

Our dear compiler starts laying out the memory, beginning with `a`. `a` takes up offset 0, that single byte. Then comes `b`: it is 4 bytes in size, so it demands a starting address that is a multiple of 4. Opting for offset 1 would not qualify, so it has to be placed at offset 4. See those offsets 1, 2, and 3 in between? They are the 3 padding bytes the compiler propped in. `b` sits at offset 4 and takes 4 bytes, landing the total exactly on 8, so `sizeof(Tiny)` is 8. Drawn out, it looks like this:

![Memory layout of Tiny](./03-tiny-padding.drawio)

Round two is this:

> The overall size of the struct itself must be a multiple of the alignment requirement of its largest member. In other words, if a struct contains a `double` (8-byte aligned), the whole struct's size must be a multiple of 8—even if there is spare room after the last member, it must be topped up with padding bytes.

Members laid out—but we are not done yet! The struct's overall size **must also be a multiple of the largest alignment value**; whatever bytes are missing, that many padding bytes go on the tail. Too dry stated that way, right? Let's lay it out again: `int` up front, `char` at the end—the same two members, just in reverse order:

```cpp
struct Flip {
    int  b;   // 4-byte alignment
    char a;   // 1-byte alignment
};
```

Let's spread `Flip` out. The `int` occupies offsets 0 through 3, the `char` takes offset 4, and the layout totals 5 bytes. 5 is not a multiple of 4, so 3 bytes go on the tail—OK, that makes 8. Now you know where that 8 comes from!

![Memory layout of Flip](./03-flip-padding.drawio)

Just like `Tiny`, `Flip` is 8 bytes; the only difference is that the padding moved from the middle to the tail. That is the rule of round two.

Round three is this:

> A struct's own alignment requirement equals the alignment requirement of its largest member. This rule affects "where the struct should be placed when it serves as a member of another struct".

Since the struct contains an `int` that must be aligned to 4, the whole struct cannot be dropped just anywhere—it **must first land on a multiple-of-4 address** before its members can each land on qualifying addresses. So a struct's own alignment value can simply be that of its largest member. Let's lay things out one more time. This time we invite the `Flip` from the previous rule, in its entirety, onto a brand-new struct—kiddo, you get to be a member this time!

```cpp
struct Outer {
    char tag;  // 1-byte alignment
    Flip in;   // the Flip from the previous rule: 8 bytes, 4-byte aligned
};
```

We keep laying out. `tag` takes up offset 0, that one byte; then comes `in`. Just like the `b` in `Tiny`, it can only be placed at offset 4, the offsets 1, 2, and 3 in between are once again 3 padding bytes, and `in` itself takes 8 bytes, stretching all the way through offset 11. Drawn out, it looks like this:

![Memory layout of Outer](./03-outer-padding.drawio)

And `Outer`'s own alignment value is consequently 4; struct nested inside struct, the rules simply apply layer upon layer. You can compute `sizeof(Outer)` in your head now: 1 plus 3 padding bytes plus 8—exactly 12 bytes, not one more, not one less. Wonderful!

Each of the three rules is easy enough on its own; to see what they do combined on a single struct, let's go straight to the code.

## The Truth About sizeof—Where the Padding Bytes Hide

Here is a classic example—probably the kind you have seen in interview questions:

```cpp
struct BadLayout {
    char a;   // 1 byte
    int  b;   // 4 bytes
    char c;   // 1 byte
};
```

The three members add up to `1 + 4 + 1 = 6` bytes, but `sizeof(BadLayout)` on most platforms is **12**. The 6 extra bytes are all padding. Let's analyze member by member what the compiler actually did.

Watch: `a` is a `char`, 1-byte aligned, so it goes at offset 0 and takes 1 byte. Next up is `b`, an `int` that needs 4-byte alignment—meaning its starting offset must be a multiple of 4. But `a` only reaches offset 1, so the compiler inserts 3 padding bytes at offsets 1, 2, and 3, placing `b` at offset 4, where it occupies offsets 4, 5, 6, and 7. Then comes `c`: a `char` needs only 1-byte alignment, so following `b` is no problem—it goes at offset 8, taking 1 byte.

So far we have used 9 bytes in total. But don't forget the second rule: the struct's overall size must be a multiple of the alignment requirement of its largest member. Here the largest alignment is `int`'s 4 bytes, so the struct's size must be a multiple of 4. 9 is not, so the compiler fills 3 more bytes at the end, topping up to 12. Drawn as a picture:

![Memory layout of BadLayout](./03-badlayout-padding.drawio)

Member declaration order directly affects the amount of padding and the size of the struct. It is a favorite interview topic, and an even more frequent real-world trap to step in—in scenarios that demand precise control over memory layout, such as network protocols and file formats, not paying attention to member order can cause the data to fail to line up. Even more critically: if we `memcpy` a struct straight out and the receiving end parses it with a different compiler, the padding rules may differ, and the data ends up misaligned outright.

Now let's adjust the member order and put the big ones first:

```cpp
struct GoodLayout {
    int  b;   // 4 bytes
    char a;   // 1 byte
    char c;   // 1 byte
};
```

Watch: `b` sits at offset 0, taking 4 bytes; `a` goes at offset 4, and 1-byte alignment is fine there, no problem. `c` follows right behind at offset 5. That is 6 bytes so far, and the overall size needs to be a multiple of 4—pad 2 bytes up to 8. `sizeof(GoodLayout)` is **8**, one third less than the 12 we just had.

![Memory layout of GoodLayout](./03-goodlayout-padding.drawio)

Just by swapping the member declaration order—without changing any logic—the struct slimmed down by 4 bytes. If our program holds a million such objects, that is 4 MB of memory saved. So a practical rule of thumb: **order members from the largest alignment requirement to the smallest**—put `double` and `int64_t` first, then `int` and `float`, and `char` and `bool` last.

## alignas and alignof—Taking Control of Alignment Manually

The compiler's default alignment rules are good enough in the vast majority of cases, but some scenarios call for us to intervene manually. C++11 introduced the two keywords `alignas` and `alignof`, for specifying an alignment requirement and querying one, respectively.

`alignof` is simple to use: give it a type, and it returns that type's alignment requirement in bytes. `alignof(int)` is 4, `alignof(double)` is 8, `alignof(char)` is 1. You can even use it on structs: `alignof(GoodLayout)` returns 4, because its largest member, the `int`, is 4-byte aligned.

`alignas`, on the other hand, forcibly specifies alignment. It can be applied to variable declarations and to type definitions:

```cpp
// Force a single variable to be 16-byte aligned
alignas(16) char buffer[1024];

// Force a struct type to be 64-byte aligned (the size of one cache line)
struct alignas(64) CacheLine {
    int data[14];  // 56 bytes + the compiler pads it up to 64
};
```

`alignas` has three most typical use cases. The first is SIMD instructions: SSE requires operands to be 16-byte aligned, AVX requires 32-byte alignment, and AVX-512 requires 64-byte alignment. If our data is not aligned to the required boundaries, SIMD load instructions raise a hardware exception outright and the program crashes on the spot. The second is cache lines: a modern CPU's cache line is usually 64 bytes, and if a data structure straddles two cache lines, a single read has to trigger two cache loads—aligning it to a cache line boundary avoids that straddling. In multithreaded scenarios there is also a trap with an easily confused name to tell apart: false sharing refers to two threads each writing their own variables while those variables crowd into the same cache line, so the line keeps getting invalidated back and forth between the two cores and performance drops for nothing. Its fix is not "aligning boundaries" in general—it is using `alignas(64)` to separate variables written by different threads onto different cache lines. The third is hardware interaction: some DMA controllers or peripherals require a buffer's physical address to be aligned in a specific way, and that is where `alignas` comes in to guarantee it.

`alignas` can only increase an alignment requirement, never decrease it. `alignas(1) int x;` does not really turn `int` into 1-byte alignment; the compiler ignores that request, because `int`'s natural alignment is 4. And if we try to write a non-power-of-two value like `alignas(3)`, the compiler rejects it outright.

Let's also look at `std::aligned_storage` (introduced in C++17, deprecated since C++23—the advice is to just use `alignas` directly), plus the `std::align` function in `<memory>`, which finds an address that satisfies an alignment requirement within a given buffer at runtime. These tools come in very handy when implementing custom allocators or type-erased containers (the underlying storage of `std::any`, for instance).

## Packing Structs—The Double-Edged Sword of pragma pack

Sometimes we genuinely want no padding at all... Any folks doing network programming or binary security out there? Our representative examples are things like network protocol header structures, binary file formats, or structs that map one-to-one onto hardware registers. In such cases we can use `#pragma pack` to tell the compiler: no padding for me, thank you.

```cpp
#pragma pack(push, 1)  // save the current alignment setting, then set 1-byte alignment
struct RawHeader {
    uint8_t  version;   // offset 0
    uint16_t length;    // offset 1 (no longer a multiple of 2!)
    uint32_t checksum;  // offset 3 (no longer a multiple of 4!)
};
#pragma pack(pop)       // restore the previous alignment setting
```

Now `sizeof(RawHeader)` is `1 + 2 + 4 = 7`, with no padding whatsoever. Every member sits right up against the previous one; the memory layout is completely compact. This style is very common in network programming and binary file parsing.

But `#pragma pack` is a true double-edged sword, and the price of wielding it badly is steep.

Taking a reference to a member of a packed struct is undefined behavior. Consider `uint32_t& ref = header.checksum;`: `checksum` sits at offset 3, which is not a multiple of 4, while a `uint32_t&` requires the address it points to be 4-byte aligned. The compiler may emit SIMD instructions that assume the address is aligned, crashing the program on some architectures, or silently returning wrong data on others. When we need to read a member of a packed struct, copy its value into a local variable first and use that—do not bind a reference directly.

On some platforms, accessing an unaligned member of a packed struct triggers a bus error; on x86 the hardware does handle unaligned accesses, but performance suffers. If all we want is a smaller struct, adjusting member order comes first, not `#pragma pack`. `#pragma pack` should be reserved for scenarios where "the memory layout must exactly match an external format".

## Hands-On Verification—alignment.cpp

Now let's pull the above knowledge together and write a complete program to verify the various alignment behaviors. The program defines several structs, prints their `sizeof` and member offsets so you can see the padding bytes' positions directly, and demonstrates how reordering members optimizes the layout.

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
    int data[3];  // 12 bytes, padded up to 16
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

/// Print struct info and member offsets
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

We have put the complete code below—click "Try It Yourself" and it runs right away:

<OnlineCompilerDemo
  title="Hands-On Verification: alignment.cpp"
  source-path="code/examples/vol1/29_alignment.cpp"
  description="Print each struct's sizeof, alignof, and member offsets online. Try reordering BadLayout the way GoodLayout does and watch the padding bytes disappear with your own eyes."
  run-options="-O0 -std=c++17"
  allow-run
/>

Look: `BadLayout` carries 6 bytes of padding (3 after `a`, 3 after `c`), while `GoodLayout` has only 2 bytes of tail padding. `MixedTypes` is the more dramatic case—7 bytes of padding get stuffed between a `char` and a `double`, bloating the whole thing to 24 bytes, whereas `ReorderedMixed` needs only 16. That is the power of member ordering: same data, different arrangement, and the memory footprint can differ by 33% or even more.

`PackedHeader`, in turn, shows what packing achieves. There is no padding at all, and the size equals exactly the sum of the members—but note that its alignment requirement has become 1, which means it can sit at any position when it appears inside another struct. `AlignedBuffer` demonstrates the effect of `alignas(16)`: although the data is only 12 bytes, the whole struct is forced onto a 16-byte boundary, and its size is 16 as well.

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

Then verify your predictions with code—after all, staring at them hard enough never flushed out a problem, right? Haha.

### Exercise 2: Optimize a Struct Layout

What is the `sizeof` of the struct below on a 64-bit system? Rearrange its members to make it as small as possible:

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
