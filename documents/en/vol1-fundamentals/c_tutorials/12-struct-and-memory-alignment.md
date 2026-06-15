---
chapter: 1
cpp_standard:
- 11
description: Master struct definitions, memory alignment and padding rules, flexible
  array members, and `offsetof` validation.
difficulty: beginner
order: 16
platform: host
prerequisites:
- restrict、不完整类型与结构体指针
reading_time_minutes: 16
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: Structs and Memory Alignment
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/c_tutorials/12-struct-and-memory-alignment.md
  source_hash: 1da76ff6fd68afc58f2076c9eb1028dc79bb5512d8d1c5ebdc53e6d00facaedb
  token_count: 3331
  translated_at: '2026-06-13T11:41:51.648816+00:00'
---
# Structs and Memory Alignment

If you have been writing C code until now using only basic types—like `int`, `float`, `char`—it is likely because you haven't encountered a scenario where you need to pass a group of related data together. Once you start writing slightly more sophisticated programs, such as a sensor data packet, a configuration table, or a communication protocol frame, you will find that relying on scattered variables is impossible to manage. The struct is the answer C provides: it allows us to knead different types of data into a whole, which can then be passed, stored, and manipulated as a single value.

But structs are far more than just "bundling data." The moment we put a struct into memory, the compiler does something behind the scenes that you might never have thought of—memory alignment. It secretly inserts padding bytes between your fields so that each field lands on an address the processor "likes." If you are unaware of this, one day when designing binary protocol frames, doing DMA transfers, or writing serialization code by hand, you will likely be driven to the brink of madness by these ghost bytes.

So, in this chapter, we will not only learn how to define and use structs but also thoroughly understand their true appearance in memory.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Proficiently define, initialize, and operate on structs and their pointers.
> - [ ] Understand the principles of memory alignment and the distribution rules of padding bytes.
> - [ ] Use `alignas`, `alignof`, and `offsetof` for alignment control and verification.
> - [ ] Master the use of designated initializers and flexible array members.
> - [ ] Understand the evolutionary relationship from C structs to C++ classes.

## Environment Setup

We will conduct all subsequent experiments in the following environment:

- Platform: Linux x86_64 (WSL2 is acceptable)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-std=c2x -Wall -Wextra`

## Step 1 — Master Struct Definition and Basic Operations

### Defining a Struct

In C, we define a struct using the `struct` keyword followed by a pair of braces:

```c
struct SensorData {
    int id;
    float value;
    char status;
};
```

Note that semicolon at the end—forgetting it is one of the most common compilation errors for beginners, and the error message usually points to the next line, leaving you confused. `struct SensorData` is now a type name, but writing `struct SensorData` every time is indeed a bit verbose, so we usually pair it with `typedef` to simplify:

```c
typedef struct SensorData {
    int id;
    float value;
    char status;
} SensorData;
```

Now we can write `SensorData` directly to declare variables, which is much cleaner. The two styles are functionally equivalent; the difference lies only in the usage of the type name: the former requires the `struct` prefix, while the latter does not. In actual projects, the `typedef` usage is more prevalent, especially in embedded development—look at any MCU vendor's SDK, and you will see `typedef struct` everywhere.

### Initialization and Assignment

There are several ways to initialize a struct. Let's start with the most basic. The first is sequential initialization—providing values in the order the fields are defined:

```c
struct SensorData sensor = {1, 25.4f, 'OK'};
```

This approach works, but readability is poor—you must remember which position corresponds to which field. Once the struct definition order is adjusted, all initialization code must be modified. C99 offers a better solution: **designated initializers**, which allow you to initialize arbitrary fields by name:

```c
struct SensorData sensor = {
    .id = 1,
    .value = 25.4f,
    .status = 'OK'
};
```

The benefits of designated initializers are obvious: the code is self-documenting, independent of field order, and unspecified fields are automatically zeroed. Honestly, in modern C code, as long as your compiler supports C99 (which basically all do), you should prefer designated initializers.

Struct assignment and initialization are two different things. Initialization happens at declaration; assignment happens after declaration. C allows direct assignment between structs of the same type, which is a byte-by-byte copy:

```c
struct SensorData sensor1 = {1, 25.4f, 'OK'};
struct SensorData sensor2;
sensor2 = sensor1; // Shallow copy
```

But be aware: struct assignment in C is a **shallow copy**—if a struct contains pointer members, after assignment, the pointer fields in both structs will point to the same memory block. This is a classic pitfall when handling structs containing dynamically allocated memory.

### Struct Pointers and the Arrow Operator

When a struct is large, or we need to modify the caller's struct within a function, passing a pointer is the only reasonable approach. This is where the difference between `.` and `->` comes in:

```c
struct SensorData sensor;
struct SensorData *ptr = &sensor;

sensor.id = 1;    // Direct member access
ptr->id = 2;      // Member access via pointer
(*ptr).id = 3;    // Equivalent to ptr->id
```

The `->` operator is just syntactic sugar for `(*ptr).`, nothing mysterious. But this sugar is so commonly used that you will hardly ever write `(*ptr).`—in C, as long as a function parameter involves a struct pointer, you are almost certainly using `->`.

Passing a struct pointer instead of the struct itself in function parameters not only avoids expensive copy overhead but also allows the function to modify the caller's data. If you do not want the function to modify the data, just add `const`:

```c
void print_sensor(const struct SensorData *s) {
    printf("ID: %d\n", s->id);
}
```

This distinction between `T*` and `const T*` is inherited in C++ as `const` member functions and reference semantics, forming a more complete "read-only vs. mutable" interface design.

## Step 2 — Understanding Memory Alignment and Padding Bytes

Next, we enter the core and most confusing part of this tutorial. Let's look at a question first: how many bytes does the following struct occupy?

```c
struct BadLayout {
    char a; // 1 byte
    int b;  // 4 bytes
    char c; // 1 byte
};
```

Intuitively, 1 + 4 + 1 = 6 bytes, right? But actually, on most 32-bit and 64-bit platforms, `sizeof(struct BadLayout)` is **12 bytes**. Where did the extra 6 bytes go? The answer is they were inserted into the struct by the compiler as **padding bytes**.

### Why Alignment is Needed

When a processor accesses memory, it does not read byte by byte. Most CPU architectures prefer to access data on 2, 4, or 8-byte boundaries—this is called **alignment**. An `int` placed at an address that is a multiple of 4 can be read in one go; but if it straddles a 4-byte boundary (e.g., placed at address 3), the CPU might need to read twice and stitch it together, resulting in a performance hit. Some architectures are even more extreme—throwing a hardware exception directly (for example, ARM accessing unaligned addresses in certain modes triggers a fault).

So, for performance and correctness, the compiler inserts padding bytes between struct members to ensure each member lands on its naturally aligned address.

### Rules of Alignment and Padding

There are actually only two rules for alignment, but understanding them requires a bit of patience. Rule one: **The starting address of each member must be an integer multiple of that member's alignment requirement**. `char` has an alignment requirement of 1 (any address works), `short` is 2, `int` is 4, `double` and `long long` are 8, and so on—the alignment requirement of basic types usually equals their size. Rule two: **The size of the struct itself must be an integer multiple of its largest alignment requirement**—this is to ensure that in an array of structs, every element satisfies the alignment requirement.

Now let's return to the `struct BadLayout` example and draw it out byte by byte:

```text
Address  0   1   2   3   4   5   6   7   8   9  10  11
        +---+---+---+---+---+---+---+---+---+---+---+---+
        | a | X | X | X |   b   |   b   | c | X | X | X |
        +---+---+---+---+---+---+---+---+---+---+---+---+
```

`a` is at offset 0, occupying 1 byte. `b` has an alignment requirement of 4, but the next available offset is 1, which is not a multiple of 4, so the compiler inserts 3 bytes of padding, letting `b` start at offset 4. `c` is at offset 8, alignment requirement 1, no problem. Finally, the struct's maximum alignment requirement is 4 (from `int`), so the total size must be a multiple of 4—currently 9, so it is padded to 12.

This is why明明 only 6 bytes of data actually occupy 12 bytes—50% of the space is wasted on padding.

### Reordering Fields to Reduce Padding

The solution to this problem is surprisingly simple: **put fields with larger alignment requirements first, and smaller ones last**. Let's rearrange the fields of `struct BadLayout`:

```c
struct GoodLayout {
    int b;  // 4 bytes
    char a; // 1 byte
    char c; // 1 byte
};
```

Now `sizeof(struct GoodLayout)` is **8 bytes**—saving one-third compared to the previous 12. `b` is at offset 0 (naturally aligned), `a` and `c` are packed tightly after it, requiring only 2 bytes of tail padding. This technique is very useful in actual engineering, especially in memory-constrained embedded systems—developing the habit of ordering fields from largest to smallest alignment requirement is worth it.

### Verifying Offsets with offsetof

The C standard library provides the `offsetof` macro (defined in `<stddef.h>`), which can tell you precisely the offset of a field within a struct. We often use it when debugging alignment issues or designing binary protocols:

```c
#include <stddef.h>
#include <stdio.h>

printf("Offset of a: %zu\n", offsetof(struct GoodLayout, a));
printf("Offset of b: %zu\n", offsetof(struct GoodLayout, b));
```

Make it a habit to print offsets with `offsetof` after writing a struct, especially when designing communication protocol frames—you will find that some fields' offsets are different from what you expected, which usually means an alignment problem.

## C11 Alignment Control: `_Alignas` and `alignof`

In the C99 era, if you needed manual alignment control, you had to rely on compiler extensions—GCC's `__attribute__ ((aligned))`, MSVC's `__declspec(align(...))`, etc. C11 finally standardized this capability, providing the `_Alignas` and `_Alignof` keywords, as well as the more friendly macro aliases `alignas` and `alignof` (defined in `<stdalign.h>`).

### `alignof`: Querying Alignment Requirements

`alignof` can query the alignment requirement of any type:

```c
#include <stdalign.h>

printf("Alignment of int: %zu\n", alignof(int));       // Usually 4
printf("Alignment of double: %zu\n", alignof(double)); // Usually 8
```

A struct's alignment requirement equals the largest alignment requirement among its members. `struct GoodLayout` has an `int`, so the overall alignment requirement is 4.

### `alignas`: Forcing Alignment

`alignas` can be used to force a variable or struct member to be allocated on a specified alignment boundary. This is very useful in embedded development—for example, DMA transfers often require the buffer start address to be 4-byte or even 32-byte aligned:

```c
alignas(16) char dma_buffer[256];
```

The parameter to `alignas` must be a power of two and cannot be less than the type's natural alignment requirement. If you write `alignas(2)` for an `int`, the compiler will ignore it or error—because `int` itself requires 4-byte alignment, you can't reduce it to 2.

## Designated Initializers in Detail

We briefly mentioned designated initializers earlier; let's take a deeper look at their full capabilities. Designated initializers are a feature introduced in C99 that allow you to specify which fields to initialize using the `.field_name = value` syntax when initializing structs, unions, and arrays.

Beyond the basic usage shown earlier, there are some details worth noting. For example, you can mix sequential initialization and designated initializers:

```c
struct SensorData s = { .id = 1, .value = 20.0f, .status = 'X' };
```

You can also use designated initializers in arrays:

```c
int mapping[256] = {
    [0] = 1,
    ['A'] = 2,
    ['Z'] = 26
};
```

This is particularly handy when creating ASCII character mapping tables or command dispatch tables, much clearer than hand-writing an initialization list of 256 elements. Unspecified elements are automatically initialized to zero (just like global variables).

## Step 3 — Understanding Flexible Array Members

Flexible Array Members (FAM) are a feature introduced in C99 that allows placing an array of unspecified size at the end of a struct. It sounds a bit strange, but its purpose is very practical—when you need a struct with a "variable-length tail of data," FAM is the cleanest way to do it.

```c
struct Packet {
    int header;
    int len;
    char data[]; // Flexible array member
};
```

`data` is an incomplete type array—it occupies no space in the struct (`sizeof(struct Packet)` does not include the size of `data`), but it tells the compiler "this struct may be followed by a contiguous block of memory." When using it, we need to manually allocate enough memory to hold the struct itself plus the data:

```c
struct Packet *pkt = malloc(sizeof(struct Packet) + 100);
pkt->len = 100;
strcpy(pkt->data, "Hello");
```

Flexible array members are widely used in communication protocols, variable-length message handling, and packet parsing. In the early days of C, people used a trick called "struct hack" to achieve similar functionality—placing an array of length 1 (or 0) at the end of the struct and then allocating extra space. But that was undefined behavior; C99's FAM is the standard approach.

One thing to note: structs containing flexible array members cannot be passed or copied by value—because the compiler doesn't know how large the tail data is. You can only operate on them through pointers.

## Struct Arrays

Combining structs and arrays is a very common way to organize data. For example, a configuration table, a set of sensor readings, or a message queue are essentially struct arrays:

```c
struct SensorData sensors[10];
```

Iterating over a struct array is the same as a normal array; you can use subscripts or pointers:

```c
for (int i = 0; i < 10; i++) {
    sensors[i].value = 0.0f;
}
```

Struct arrays are laid out tightly in memory—each element's size is `sizeof(struct)` (including padding), and the address of the i-th element is `base_address + i * sizeof(struct)`. This is why padding is needed at the end of a struct—without it, fields in the second element of the array might be misaligned.

## `__attribute__((packed))`: Removing Padding

There are scenarios where we truly need a struct without any padding—the most typical is binary communication protocols. Data received by an MCU via UART/SPI/I2C is a tightly packed byte stream. If the struct has padding, directly casting a pointer to interpret it will read incorrect values. GCC and Clang provide `__attribute__((packed))` to remove padding:

```c
struct __attribute__((packed)) ProtocolFrame {
    char start;
    int type;
    short checksum;
};
```

With this attribute, `sizeof(struct ProtocolFrame)` is a pure 1 + 4 + 2 = 7 bytes, with absolutely no padding. But be aware of the cost—accessing unaligned fields on some architectures can lead to performance degradation or even hardware exceptions. So `packed` should only be used when you genuinely need a compact layout, not scattered everywhere. ARM Cortex-M series can handle unaligned access in most cases (with a performance penalty), but some older architectures (like ARM7TDMI) will fault directly.

A safer approach is: **use a packed struct at the communication layer to parse raw bytes, then immediately convert it to an aligned internal struct for use**. Separate parsing and business logic to get the best of both worlds.

## C++ Transition

### Evolution from struct to class

In C, `struct` can only contain data members—no member functions, no access control, no inheritance. C++ retains the `struct` keyword but gives it almost the same capabilities as `class`. The only difference lies in default access rights: members of a `struct` default to `public`, while members of a `class` default to `private`. Beyond that, a C++ `struct` can have constructors, destructors, member functions, inheritance, virtual functions—it can do anything.

```cpp
struct Point {
    double x, y;

    void print() const; // Member function
    Point(double x, double y); // Constructor
};
```

So when you see `struct` in C++ code, don't assume it's the same as a C struct—it is simply a class with default public access.

### POD Types and Trivially Copyable

C++ has a specific concept for "simple structs compatible with C": POD types (Plain Old Data). Simply put, if a struct has no virtual functions, no non-trivial constructor/destructor, and all members are POD types, then it is itself a POD. POD types can be safely copied with `memcpy`, zeroed with `memset`, and safely binary serialized and deserialized—because their memory layout is fully consistent with C.

After C++11, the concept of POD was refined into several more precise type traits: `std::is_trivial`, `std::is_standard_layout`, etc. Understanding these concepts is crucial in cross-language interaction (C/C++ mixed programming), binary serialization, and shared memory communication.

### `std::aligned_storage`

The C++ standard library provides `std::aligned_storage` (since C++11, deprecated in C++23 in favor of `std::uninitialized_buffer`), a type trait tool for manually controlling the alignment of a block of raw memory. It is used in advanced scenarios like implementing type-erased containers, memory pools, and placement new:

```cpp
std::aligned_storage<sizeof(Task), alignof(Task)>::type task_buffer;
```

These concepts will be discussed in detail in later C++ chapters. For now, just know: the C language approach to alignment control is implemented more systematically and safely in C++.

## Summary

In this tutorial, we thoroughly dissected structs from "how to use them" to "what they look like in memory." Structs are the core composite type in C, and understanding their memory layout—especially alignment and padding—is the foundation for writing efficient, correct, and portable code.

### Key Takeaways

- [ ] Structs are defined with `struct`, and pointers use `->` to access members.
- [ ] C99 designated initializers `.field = val` are safer and more readable than sequential initialization.
- [ ] The compiler inserts padding bytes between members and at the end of the struct to ensure alignment.
- [ ] Ordering fields from largest to smallest alignment requirement can reduce padding and save memory.
- [ ] The `offsetof` macro can precisely verify the offset of fields.
- [ ] C11's `alignas`/`alignof` provide standardized alignment control capabilities.
- [ ] Flexible array members are for variable-length tail data and must be used via pointers and dynamic allocation.
- [ ] `__attribute__((packed))` removes padding for binary protocol parsing but has performance and portability costs.
- [ ] C++'s `struct` is a `class` with default public access; POD types maintain a C-compatible memory layout.

## Exercises

### Exercise: Design a Manually Aligned Communication Protocol Frame

Please design a binary protocol frame structure for embedded device communication. Requirements are as follows:

1. The frame header contains a 1-byte start flag `0xAA`, 1-byte frame type, 2-byte payload length, and 4-byte timestamp.
2. The payload is variable-length data (use a flexible array member).
3. The frame tail contains a 2-byte CRC16 checksum.
4. Use `alignas` to ensure the timestamp field is 4-byte aligned.
5. Use `__attribute__((packed))` to ensure the frame structure is compact (suitable for direct cast parsing of byte streams).
6. Write a function using `offsetof` to print the offset of each field to verify the layout.

```c
// TODO: Implement your protocol frame here
```

Hint: Be careful when using `alignas` inside a packed struct—packed removes automatic padding, but `alignas` can force a specific field's alignment. Think about this: in a packed struct, if the offset from the frame header to the timestamp is not a multiple of 4, how would you handle it?

## References

- [C struct - cppreference](https://en.cppreference.com/w/c/language/struct)
- [C11 alignas/alignof - cppreference](https://en.cppreference.com/w/c/language/alignment)
- [offsetof - cppreference](https://en.cppreference.com/w/c/types/offsetof)
- [Flexible array members - cppreference](https://en.cppreference.com/w/c/language/struct)
