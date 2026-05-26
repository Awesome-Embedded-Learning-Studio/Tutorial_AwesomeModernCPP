---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: Exploring C++ object memory layout
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'Chapter 2: 零开支抽象'
reading_time_minutes: 12
tags:
- cpp-modern
- host
- intermediate
title: Object Size and Trivial Types
translation:
  source: documents/vol3-standard-library/05-object-size-and-trivial-types.md
  source_hash: f5c43a40fc7f0fc41bb19999da5fe2b0d173054f31d8622cf9a5f81d70468449
  translated_at: '2026-05-26T11:38:14.483343+00:00'
  engine: anthropic
  token_count: 1747
---
# Modern C++ for Embedded Systems Tutorial: Object Size, Memory Alignment, Trivial/Standard-Layout Types, and Aggregate Initialization

When writing low-level code, building embedded systems, or interfacing with C APIs, we often get tripped up by a string of obscure terms: `alignas`, `alignof`, `sizeof`, aggregates, and more. These concepts might seem fragmented, but they actually form an interconnected map: they dictate an object's memory representation, copy semantics, whether we can safely use `memcpy`, whether a type is ABI-compatible with C structs, and initialization flexibility.

------

## Starting with "Size" and "Alignment": Why `sizeof` Isn't Always the Sum of Members

`sizeof` reports the number of bytes an object occupies in memory (i.e., its full object representation, which includes necessary padding), while `alignof` reports the type's **alignment constraint**—meaning the object's starting address must be a multiple of `alignof`.

Imagine a building (the object) with different rooms (members) of varying sizes and alignment rules. To fit certain large items correctly, we might need gaps between floors (padding). To the compiler, these gaps are mandatory.

Let's look at a common example:

```cpp
struct A {
    char  c; // 1 byte
    int   i; // 4 bytes, alignment 4
};
// typical layout on common ABIs:
// offset 0: c
// offset 1..3: padding
// offset 4..7: i
// sizeof(A) == 8

```

If we rearrange the order:

```cpp
struct B {
    char a;   // offset 0
    int  i;   // offset 4 (padding 3 bytes)
    char b;   // offset 8
    // padding 3 bytes to make sizeof multiple of alignof(B) (which is 4)
    // sizeof(B) == 12
}

```

Placing the two `uint32_t` members together usually reduces padding:

```cpp
struct C {
    char a;
    char b;
    int  i;
    // char a@0, char b@1, padding@2-3, int@4..7 -> sizeof == 8
}

```

Therefore, reordering members and grouping wide-aligned members (like `uint64_t`, `double`, SIMD vectors, etc.) together or placing them at the end of a struct is a common memory compaction strategy. For embedded systems, this can often squeeze considerable space out of unnecessary RAM usage.

Additionally, a struct's overall alignment is the **largest alignment** among its members. The compiler also adds tail padding at the end of the struct to ensure that `sizeof` is a multiple of `alignof`. This affects the spacing between array elements and how structs are laid out in arrays.

We can use `alignas` to force or change alignment, for example, specifying alignment for a SIMD buffer that requires 16-byte alignment:

```cpp
struct alignas(16) Vec4 {
    float x,y,z,w; // sizeof == 16, alignof == 16
};

```

We need to be careful with `alignas`: increasing alignment changes the struct's ABI and `sizeof`, and can expose unaligned access issues on certain platforms (if we place an object at an unaligned address on unsupported hardware, it will crash).

------

## trivial / trivially_copyable / standard-layout: Why These "Type Traits" Matter

The C++ standard breaks down a set of type characteristics to precisely express "how an object of this type behaves in memory." This design, starting in C++11 (which split the historical POD concept into several distinct properties), is especially important for embedded and systems programming because it dictates whether we can use `memcpy`, interoperate with C, and what optimization opportunities exist.

Let's first put a few frequently confused terms into a natural language map:

- **trivial type**: Broadly speaking, a type with "trivial" special member functions (default constructor, copy/move constructors, assignment operators, and destructors are all compiler-generated without custom logic). In other words, construction, copying, and destruction execute no runtime code—the object's bit pattern is its object representation, with no hidden actions.
- **trivially_copyable type**: Objects of this type can be safely copied via byte-by-byte copying (`memcpy`). After copying, the target object has the same object representation and can be properly destroyed, etc. `is_trivially_copyable` is the key criterion for determining whether `memcpy` can be used.
- **standard-layout type**: This type has predictable memory layout rules (e.g., non-static data members are arranged in declaration order, providing certain guarantees for C interoperability). It avoids unpredictable memory layouts caused by complex access control, virtual inheritance, or multiple base classes.

A crucial fact is that the old concept `POD` (Plain Old Data) was split in C++11 into `is_trivial` and `is_standard_layout`; semantically, `is_pod` is simply "both trivial and standard-layout." Many safety assumptions related to ABI and C interoperability can be checked using `is_trivially_copyable` and `is_standard_layout`.

Why is this information useful? Because it directly affects:

- Whether we can read/write an object as a byte sequence (e.g., saving it to flash, or transferring it directly from memory via DMA).
  - **Only types that are `is_trivially_copyable` can safely use `memcpy` to copy the object representation**.
- Whether we can pass a C++ type as a C `struct` to an external C interface (e.g., device register mappings, bootloader data structures).
  - **This typically requires `is_standard_layout` to guarantee layout compatibility**.
- How the type behaves in constant expression and zero-initialization contexts (e.g., static storage duration object initialization and memory images).

Let's look at an example combining these concepts:

```cpp
struct S {
    int x;
    double y;
    // 没有用户定义构造/析构/拷贝、没有虚函数、没有基类……
};
// S 通常是 trivial、trivially_copyable、standard-layout -> POD
static_assert(std::is_trivially_copyable_v<S>);
static_assert(std::is_standard_layout_v<S>);

```

Compare this with a non-trivial type:

```cpp
struct T {
    T() { /* do something */ } // user-provided ctor
    int x;
};
// T 不是 trivial（因为用户定义了构造函数）；可能也不是 trivially_copyable。

```

To reiterate an easily misunderstood point: **trivial ≠ trivially_copyable**. The former emphasizes the "triviality" of special members (especially the default constructor), while the latter emphasizes whether byte-by-byte copying is safe. In practice, to determine whether we can `memcpy`, we should use `is_trivially_copyable`.

------

## Aggregates and Aggregate Initialization: From Braces to C++20 Designated Initializers

An aggregate is a highly convenient type category: it allows us to initialize objects by directly listing members inside braces (aggregate initialization). This is extremely intuitive when writing data descriptions (like device descriptor tables or configuration structs), and it naturally suits `constexpr` and static initialization.

A classic aggregate (described intuitively) is "a type with no user-defined constructors, no virtual functions, all non-static data members are public, and no base classes (or it meets the standard-layout restrictions)"—in short, the compiler can simply treat aggregate initialization as copying values into the object representation member by member in order.

Example:

```cpp
struct Point { int x, y; };
Point p1 { 1, 2 };    // aggregate initialization, 成员按声明顺序赋值

```

One benefit of aggregate initialization is that it allows partial initialization (the remaining members will be default-initialized/zero-initialized, depending on the context), and it is commonly used in `constexpr`:

```cpp
struct Config {
    int baud;
    int parity;
    int stop_bits;
};

constexpr Config default_cfg { 115200, 0, 1 };

```

### C++20 Designated Initializers: More Readable and Robust

C's "designated initializers" were officially introduced into C++20 as a formal language feature. This makes aggregate initialization more readable, insensitive to member order, and easier to maintain (adding new members won't break old code due to ordering issues).

Usage example:

```cpp
struct S {
    int a;
    int b;
    int c;
};

S s1 { .b = 2, .a = 1, .c = 3 }; // 成功：成员顺序不重要
S s2 { .a = 1 }; // 只初始化 a，b 和 c 会做默认初始化（对内置类型通常为未定义或零，取决上下文）

```

Designated initializers also support nested structs and array index designations (similar to C's `.name = value` and `[index] = value`)—this is highly practical for initializing complex hardware description data structures, register layouts, or long tables. Here is a more hardware-oriented example:

```cpp
struct Header {
    uint16_t id;
    uint16_t flags;
};

struct Packet {
    Header hdr;
    uint8_t payload[8];
};

Packet pkt {
    .hdr = { .id = 0x1234, .flags = 0x1 },
    .payload = { [0] = 0xAA, [3] = 0x55 } // 只给第 0 和第 3 个元素赋值
};

```

This brings several practical benefits:

- Significantly improved readability: seeing `.baud_rate = 115200` makes the meaning clear, rather than guessing by position.
- Resilience to extension: Adding new members won't break old code (unless the old code relies on positional order).
- Better compatibility with C (making it easier to port C-style initialization paradigms to C++).

Note: designated initializers only apply to **aggregate types**. We cannot use this syntax for classes with user-defined constructors.

------

## Connecting the Dots: How Embedded and Low-Level Engineers Apply This Knowledge

Now let's string the points above into some practical, actionable principles, written as a continuous narrative to help us avoid pitfalls and write more robust code when doing embedded C++.

When defining data structures that interact with C (such as device register layouts, bootloader metadata, serialization formats, or DMA buffers), we usually need to ensure the type is **standard-layout** (to guarantee a predictable memory layout) and ideally **trivially_copyable** (to easily `memcpy` or interpret a block of memory as that struct). When defining them, avoid virtual functions, avoid private non-static data members, and do not write custom constructors/destructors/copy operations. For important assertions, use `static_assert`:

```cpp
static_assert(std::is_standard_layout_v<MyRegs>, "MyRegs must be standard-layout for C-ABI compatibility");
static_assert(std::is_trivially_copyable_v<MyRegs>, "MyRegs must be trivially_copyable for memcpy usage");

```

Memory alignment affects `sizeof` and array layout. If our hardware or DMA requires special alignment (e.g., 16-byte aligned cache lines or SIMD), we should use `alignas` to specify it explicitly, noting that this changes `sizeof` and the ABI. For example, a struct decorated with `alignas(16)` will occupy a multiple of 16 bytes for each element in an array.

When writing initialization code, we should prefer brace initialization and C++20 designated initializers. This not only makes the code readable but also reduces bugs introduced by changes in member order. It is particularly safe and intuitive for registers or configuration tables. For example:

```cpp
struct DeviceConfig {
    uint32_t mode;
    uint32_t timeout_ms;
    uint8_t  flags;
};

DeviceConfig cfg {
    .mode = 3,
    .timeout_ms = 1000,
    // .flags 未指定 -> 按规则零/默认初始化
};

```

When we need to save RAM, remember that rearranging fields can significantly reduce struct size, especially in scenarios with large numbers of objects or arrays. Place wide-aligned members (`uint64_t`, `double`, SIMD) at the beginning of the struct or close together, and group small-byte members together to avoid interleaving that causes multiple padding instances. Always use `sizeof` and `alignof` to verify our assumptions, and use `static_assert` to encode those assumptions at compile time when necessary.

Finally, regarding an object's copy semantics: **only when a type is `is_trivially_copyable` is it safe to binary-copy it to another object (e.g., via `memcpy`)**. Do not perform binary copies on classes containing virtual functions, non-trivial destructors, or special members; for these types, use constructor/copy/assignment semantics.

------

## Run Online

Experience memory alignment and padding, type traits checks, and C++20 designated initializers online:

<OnlineCompilerDemo
  title="Object Size and Trivial Types"
  source-path="code/examples/vol34567/04_object_size.cpp"
  description="Observe memory alignment and padding, is_trivially_copyable checks, and C++20 designated initializers"
  allow-run
  allow-x86-asm
/>

## Summary

- `alignof` determines an object's alignment requirements; `sizeof` reports how many bytes an object actually occupies in memory (including padding).
- Internal padding within an object comes from alignment rules; arranging member order thoughtfully can reduce padding and save RAM.
- `is_trivial`, `is_trivially_copyable`, and `is_standard_layout` are the standard's fine-grained divisions of type characteristics:
  - To use `memcpy` or save a binary image, ensure `is_trivially_copyable`.
  - To guarantee layout compatibility with C, ensure `is_standard_layout`.
  - `is_pod` is conceptually just both `is_trivial` and `is_standard_layout`.
- Aggregate initialization is very convenient; C++20 designated initializers make initialization safer, more readable, and less dependent on member order.
- In embedded and low-level scenarios, we should at least use `static_assert` to check these invariants (size, alignment, whether trivially_copyable/standard-layout) at interface boundaries. Code built this way is both efficient and robust.
