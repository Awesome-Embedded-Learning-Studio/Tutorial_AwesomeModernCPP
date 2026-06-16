---
chapter: 1
cpp_standard:
- 11
description: 掌握结构体定义、内存对齐与填充规则、柔性数组成员及 offsetof 验证
difficulty: beginner
order: 16
platform: host
prerequisites:
- restrict、不完整类型与结构体指针
reading_time_minutes: 20
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: 结构体与内存对齐
translation:
  source: documents/vol1-fundamentals/c_tutorials/12-struct-and-memory-alignment.md
  source_hash: e02b4890f94e0db74c8463b5674bb8d965121fa74d6eda28549fa842e1441525
  translated_at: '2026-06-16T03:36:00.155974+00:00'
  engine: anthropic
  token_count: 3331
---
# Structures and Memory Alignment

If you have been writing C until now and have only used basic types—like `int`, `float`, `char`—it is likely because you haven't yet encountered a scenario where you need to bundle a group of related data together for passing around. Once you start writing slightly more sophisticated programs, such as a sensor data packet, a configuration table, or a communication protocol frame, you will find that relying on scattered variables is impossible to manage. The structure (`struct`) is the answer C provides: it allows us to knead different types of data into a whole, which we can then pass, store, and manipulate as a single value.

But structures are far more than just "bundling data." The moment we put a structure into memory, the compiler does something behind the scenes that you might never have thought of—memory alignment. It secretly inserts padding bytes between your fields so that each field lands on an address the processor "likes." If you are unaware of this, one day when designing binary protocol frames, doing DMA transfers, or writing serialization code by hand, you will likely be driven to despair by these ghost bytes.

So, in this chapter, we will not only learn how to define and use structures but also thoroughly understand their true appearance in memory.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Proficiently define, initialize, and operate on structures and their pointers.
> - [ ] Understand the principles of memory alignment and the distribution rules of padding bytes.
> - [ ] Use `alignas`, `alignof`, and `offsetof` for alignment control and verification.
> - [ ] Master the use of designated initializers and flexible array members.
> - [ ] Understand the evolutionary relationship from C structures to C++ classes.

## Environment Setup

We will conduct all subsequent experiments in the following environment:

- **Platform**: Linux x86_64 (WSL2 is also acceptable).
- **Compiler**: GCC 13+ or Clang 17+.
- **Compiler Flags**: `-std=c2x -Wall -Wextra -pedantic`

## Step 1 — Master Structure Definition and Basic Operations

### Defining a Structure

In C, we define a structure using the `struct` keyword followed by a pair of curly braces:

```c
struct SensorData {
    int id;
    float value;
    char status;
}; // <--- Don't forget this semicolon!
```

Note that semicolon at the end—forgetting it is one of the most common compilation errors for beginners, and the error message usually points to the next line, leaving you confused. `struct SensorData` is now a type name, but writing `struct SensorData` every time is indeed a bit verbose, so we usually pair it with `typedef` to simplify:

```c
typedef struct SensorData {
    int id;
    float value;
    char status;
} SensorData; // Now we can use 'SensorData' directly
```

This allows us to write `SensorData` directly to declare variables, which is much cleaner. The two styles are functionally equivalent; the difference lies only in how the type name is used: the former requires the `struct` prefix, while the latter does not. In actual projects, the `typedef` usage is more prevalent, especially in embedded development—look at any MCU vendor's SDK, and you will see `typedef` everywhere.

### Initialization and Assignment

There are several ways to initialize a structure. Let's start with the most basic. The first is sequential initialization—providing values in the order the fields are defined:

```c
// Sequential initialization (not recommended)
struct SensorData sensor = {1, 25.4f, 'A'};
```

This works, but readability is poor—you must remember which position corresponds to which field. Once the structure definition order is adjusted, all initialization code must be modified accordingly. C99 gives us a better solution: **designated initializers**, which allow initializing arbitrary fields by name:

```c
// Designated initializer (recommended)
struct SensorData sensor = {
    .id = 1,
    .value = 25.4f,
    .status = 'A'
};
```

The benefits of designated initializers are obvious: the code is self-documenting, independent of field order, and unspecified fields are automatically zeroed out. Frankly, in modern C code, as long as your compiler supports C99 (which basically all do), you should prefer designated initializers.

Structure assignment and initialization are two different things. Initialization happens at declaration; assignment happens after declaration. C allows direct assignment between structures of the same type, which performs a byte-by-byte copy:

```c
struct SensorData sensor1 = {1, 25.4f, 'A'};
struct SensorData sensor2;
sensor2 = sensor1; // Member-wise copy
```

But be aware: structure assignment in C is a **shallow copy**—if a structure contains pointer members, after assignment, the pointer fields in both structures will point to the same memory block. This is a classic pitfall when handling structures containing dynamically allocated memory.

### Structure Pointers and the Arrow Operator

When a structure is large, or when we need to modify the caller's structure within a function, passing a pointer is the only reasonable approach. This is where the difference between `.` and `->` comes in:

```c
struct SensorData sensor = {1, 25.4f, 'A'};
struct SensorData *ptr = &sensor;

// Use -> to access members via pointer
ptr->value = 26.0f; // Equivalent to (*ptr).value = 26.0f;
```

The `->` operator is simply syntactic sugar for `(*ptr).`. There is nothing mysterious about it. But this syntactic sugar is so commonly used that you will hardly ever write `(*ptr).`—in C, as long as a function parameter involves a structure pointer, you are almost certainly using `->`.

Passing a structure pointer instead of the structure itself in function parameters not only avoids expensive copy overhead but also allows the function to modify the caller's data. If you do not want the function to modify the data, just add `const`:

```c
void print_sensor(const struct SensorData *s) {
    printf("ID: %d, Value: %.2f\n", s->id, s->value);
    // s->value = 0.0f; // Error: cannot assign to const object
}
```

This distinction between `T*` and `const T*` is inherited in C++ into member functions and reference semantics, forming a more complete "read-only vs. mutable" interface design.

## Step 2 — Understanding Memory Alignment and Padding Bytes

Next, we enter the core and most confusing part of this tutorial. Let's look at a problem first: how many bytes does the following structure occupy?

```c
struct BadLayout {
    char a; // 1 byte
    int b;  // 4 bytes
    char c; // 1 byte
};
```

Intuitively, 1 + 4 + 1 = 6 bytes, right? But actually, on most 32-bit and 64-bit platforms, `sizeof(struct BadLayout)` is **12 bytes**. Where did the extra 6 bytes go? The answer is that the compiler inserted them into the structure as **padding bytes**.

### Why Alignment is Necessary

When a processor accesses memory, it does not read byte by byte. Most CPU architectures prefer to access data on 2, 4, or 8-byte boundaries—this is called **alignment**. An `int` placed at an address that is a multiple of 4 can be read in one go; but if it straddles a 4-byte boundary (e.g., placed at address 3), the CPU might need to read twice and stitch it together, resulting in a performance hit. Some architectures are even more extreme—throwing a hardware exception directly (for example, ARM accessing an unaligned address in certain modes triggers a fault).

Therefore, for performance and correctness, the compiler inserts padding bytes between structure members to ensure each member lands on its naturally aligned address.

### Rules of Alignment and Padding

There are essentially two rules for alignment, but understanding them requires a bit of patience. Rule one: **The starting address of each member must be an integer multiple of that member's alignment requirement**. `char` has an alignment requirement of 1 (any address works), `short` is 2, `int` is 4, `double` and `long long` are 8, and so on—the alignment requirement of basic types usually equals their size. Rule two: **The size of the structure itself must be an integer multiple of its largest alignment requirement**—this is to ensure that in an array of structures, each element satisfies the alignment requirement.

Now let's return to the `struct BadLayout` example and draw it out byte by byte:

```text
Address  0   1   2   3   4   5   6   7   8   9   10  11
        +---+---+---+---+---+---+---+---+---+---+---+---+
Member  | a | X | X | X |   b   |   b   | c | X | X | X |
        +---+---+---+---+---+---+---+---+---+---+---+---+
Padding |     ^       |       ^       |       ^
        +-------------+---------------+-------------+
```

`a` is at offset 0, occupying 1 byte. `b` has an alignment requirement of 4, but the next available offset is 1, which is not a multiple of 4, so the compiler inserts 3 bytes of padding, making `b` start at offset 4. `c` is at offset 8, with an alignment requirement of 1, which is fine. Finally, the structure's maximum alignment requirement is 4 (from `int b`), so the total size must be a multiple of 4—currently 9, so it is padded to 12.

This is why明明 only 6 bytes of data actually occupy 12 bytes—50% of the space is wasted on padding.

### Reordering Fields to Reduce Padding

The solution to this problem is surprisingly simple: **place fields with larger alignment requirements first, and smaller ones later**. Let's rearrange the fields of `struct BadLayout`:

```c
struct GoodLayout {
    int b;  // 4 bytes
    char a; // 1 byte
    char c; // 1 byte
};
```

Now `sizeof(struct GoodLayout)` is **8 bytes**—saving one-third compared to the previous 12. `b` is at offset 0 (naturally aligned), `a` and `c` are packed tightly after it, and finally only 2 bytes of tail padding are needed. This technique is very useful in actual engineering, especially in memory-constrained embedded systems—developing the habit of ordering fields from largest to smallest alignment requirement is worth it.

### Verifying Offsets with offsetof

The C standard library provides the `offsetof` macro (defined in `<stddef.h>`), which can precisely tell you the offset of a specific field within a structure. We often use it when debugging alignment issues or designing binary protocols:

```c
#include <stddef.h>
#include <stdio.h>

int main() {
    printf("Offset of a: %zu\n", offsetof(struct GoodLayout, a));
    printf("Offset of b: %zu\n", offsetof(struct GoodLayout, b));
    return 0;
}
```

Make it a habit to print out `offsetof` for every field once you finish defining a structure, especially when designing communication protocol frames—you will find that some fields' offsets are different from what you expected, which usually means an alignment problem.

## C11 Alignment Control: `_Alignas` and `alignof`

In the C99 era, if you needed manual alignment control, you had to rely on compiler extensions—GCC's `__attribute__ ((aligned))`, MSVC's `__declspec(align(...))`, etc. C11 finally standardized this capability, providing the `_Alignas` and `_Alignof` keywords, as well as the more friendly macro aliases `alignas` and `alignof` (defined in `<stdalign.h>`).

### `alignof`: Querying Alignment Requirements

`alignof` can query the alignment requirement of any type:

```c
#include <stdalign.h>
#include <stdio.h>

int main() {
    printf("Alignment of int: %zu\n", alignof(int));       // Usually 4
    printf("Alignment of double: %zu\n", alignof(double)); // Usually 8
    printf("Alignment of GoodLayout: %zu\n", alignof(struct GoodLayout)); // 4
    return 0;
}
```

A structure's alignment requirement equals the largest alignment requirement among its members. `struct GoodLayout` has an `int`, so the overall alignment requirement is 4.

### `alignas`: Forcing Alignment

`alignas` can be used to force a variable or structure member to be allocated on a specified alignment boundary. This is very useful in embedded development—for example, DMA transfers often require the buffer start address to be aligned to 4 or even 32 bytes:

```c
#include <stdalign.h>

// Force this buffer to be aligned on a 32-byte boundary
alignas(32) char dma_buffer[256];
```

The parameter to `alignas` must be a power of two and cannot be less than the type's natural alignment requirement. If you write `alignas(2)` for an `int`, the compiler will ignore it or report an error—because `int` itself requires 4-byte alignment, you can't reduce it to 2.

## Designated Initializers in Detail

We briefly mentioned designated initializers earlier; let's take a deeper look at their full capabilities. Designated initializers are a feature introduced in C99 that allow you to use `.field_name = value` syntax to specify which fields to initialize when initializing structures, unions, and arrays.

Beyond the basic usage shown earlier, there are some details worth noting. For example, you can mix sequential initialization and designated initializers:

```c
struct SensorData sensor = {
    .id = 10,
    25.0f, // Sequential initialization applies to 'value'
    .status = 'B'
};
```

You can also use designated initializers in arrays:

```c
int lookup_table[256] = {
    ['A'] = 1,
    ['B'] = 2,
    ['C'] = 3
    // The rest are automatically 0
};
```

This style is particularly convenient when making ASCII character mapping tables or command dispatch tables, much clearer than hand-writing a 256-element initialization list. Unspecified elements are automatically initialized to zero (just like global variables).

## Step 3 — Understanding Flexible Array Members

A Flexible Array Member (FAM) is a feature introduced in C99 that allows placing an array of unspecified size at the end of a structure. It sounds a bit strange, but its use is very practical—when you need a structure with a "variable-length tail data," FAM is the cleanest way to do it.

```c
struct Packet {
    int header;
    int length;
    char data[]; // Flexible array member
};
```

`data` is an incomplete type array—it occupies no space within the structure (`sizeof(struct Packet)` does not include the size of `data`), but it tells the compiler "this structure may be followed by a contiguous block of memory." When using it, we need to manually allocate enough memory to hold the structure itself plus the data:

```c
// Calculate required size: size of struct + size of data
size_t total_size = sizeof(struct Packet) + 100 * sizeof(char);
struct Packet *pkt = malloc(total_size);

if (pkt) {
    pkt->length = 100;
    // Now we can safely use pkt->data[0] through pkt->data[99]
    pkt->data[0] = 'H';
    pkt->data[1] = 'i';
    // ...
    free(pkt);
}
```

Flexible array members are used heavily in communication protocols, variable-length message handling, and packet parsing. In the early days of C, people used a trick called "struct hack" to achieve similar functionality—placing an array of length 1 (or 0) at the end of the structure, then allocating extra space. But that was undefined behavior; C99's FAM is the standard way.

One thing to note: structures containing flexible array members cannot be passed or copied by value—because the compiler doesn't know how large the tail data is. You can only operate on them through pointers.

## Arrays of Structures

Combining structures and arrays is a very common way to organize data. For example, a configuration table, a set of sensor readings, or a message queue are essentially all arrays of structures:

```c
#define MAX_SENSORS 8
struct SensorData sensors[MAX_SENSORS];
```

Iterating over an array of structures is the same as iterating over a normal array; you can use subscripts or pointers:

```c
for (int i = 0; i < MAX_SENSORS; i++) {
    sensors[i].value = read_sensor(i);
}
```

Arrays of structures are laid out tightly in memory—each element's size is `sizeof(struct T)` (including padding), and the address of the i-th element is `base_address + i * sizeof(struct T)`. This is also why padding is needed at the end of a structure—without it, fields in the second element of the array might be misaligned.

## `__attribute__((packed))`: Removing Padding

There are scenarios where we truly need a structure with absolutely no padding—the most typical being binary communication protocols. Data received by an MCU via UART/SPI/I2C is a compact stream of bytes. If the structure has padding, casting a pointer directly to interpret it will read incorrect values. GCC and Clang provide `__attribute__((packed))` to remove padding:

```c
struct __attribute__((packed)) ProtocolFrame {
    char start;      // 1 byte
    short length;    // 2 bytes
    int timestamp;   // 4 bytes
    char payload[1]; // Flexible array placeholder or similar
};
```

With this attribute, `sizeof(struct ProtocolFrame)` is purely 1 + 2 + 1 + 4 = 8 bytes, with no padding. But be aware of the cost—accessing unaligned fields on some architectures can lead to performance degradation or even hardware exceptions. So `packed` should only be used when you truly need a compact layout, not sprinkled everywhere. ARM Cortex-M series can handle unaligned access in most cases (with a performance penalty), but some older architectures (like ARM7TDMI) will fault directly.

A safer approach is: **use a packed structure at the communication layer to parse raw bytes, then immediately convert it to an aligned internal structure for use**. Separate parsing from business logic to get the best of both worlds.

## C++ Connection

### Evolution from `struct` to `class`

In C, a `struct` can only contain data members—no member functions, no access control, no inheritance. C++ retains the `struct` keyword but gives it almost the same capabilities as `class`. The only difference lies in default access rights: members of a `struct` are `public` by default, while members of a `class` are `private` by default. Beyond that, C++ `struct`s can have constructors, destructors, member functions, inheritance, virtual functions—they can do anything.

```cpp
struct Sensor {
    int id;
    float value;

    // C++ allows member functions!
    void print() const {
        std::cout << "ID: " << id << ", Value: " << value << std::endl;
    }
};
```

So when you see `struct` in C++ code, don't assume it's the same as a C structure—it's just a class with default public access.

### POD Types and Trivially Copyable

C++ has a specific concept for "simple structures compatible with C": POD types (Plain Old Data). Simply put, if a structure has no virtual functions, no non-trivial constructor/destructor, and all members are POD types, then it is itself a POD. POD types can be safely copied with `memcpy`, zeroed with `memset`, and safely binary serialized and deserialized—because their memory layout is fully consistent with C.

After C++11, the POD concept was refined into several more precise type traits: `is_trivially_copyable`, `is_standard_layout`, etc. Understanding these concepts is important in cross-language interaction (C/C++ mixed programming), binary serialization, and shared memory communication.

### `std::aligned_storage`

The C++ Standard Library provides `std::aligned_storage` (since C++11, superseded by `std::aligned_union` in C++23, or more commonly `std::bytes` in modern code), a type trait tool for manually controlling the alignment of a block of raw memory. It is used in advanced scenarios like implementing type-erased containers, memory pools, and placement new:

```cpp
#include <type_traits>

std::aligned_storage<sizeof(SensorData), alignof(SensorData)>::type sensor_buffer;
// Use placement new to construct object in buffer
SensorData* sensor = new (&sensor_buffer) SensorData();
```

These concepts will be discussed in detail in later C++ chapters. For now, just know: the idea of alignment control in C is implemented more systematically and safely in C++.

## Summary

In this tutorial, we thoroughly dissected structures from "how to use them" to "what they look like in memory." The structure is the core composite type in C. Understanding its memory layout—especially alignment and padding—is the foundation for writing efficient, correct, and portable code.

### Key Takeaways

- [ ] Structures are defined with `struct`, and pointers use `->` to access members.
- [ ] C99 designated initializers `.field = val` are safer and more readable than sequential initialization.
- [ ] The compiler inserts padding bytes between members and at the end of the structure to ensure alignment.
- [ ] Ordering fields from largest to smallest alignment requirement can reduce padding and save memory.
- [ ] The `offsetof` macro can precisely verify field offsets.
- [ ] C11's `alignas`/`alignof` provide standardized alignment control capabilities.
- [ ] Flexible array members are for variable-length tail data and must be used with pointers and dynamic allocation.
- [ ] `__attribute__((packed))` removes padding for binary protocol parsing but has performance and portability costs.
- [ ] C++ `struct` is a `class` with default public access; POD types maintain a C-compatible memory layout.

## Exercises

### Exercise: Design a Manually Aligned Communication Protocol Frame

Please design a binary protocol frame structure for embedded device communication. Requirements are as follows:

1. **Frame Header**: 1-byte start flag `0xAA`, 1-byte frame type, 2-byte payload length, 4-byte timestamp.
2. **Payload**: Variable-length data (use a flexible array member).
3. **Frame Tail**: 2-byte CRC16 checksum.
4. Use `alignas(4)` to ensure the timestamp field is 4-byte aligned.
5. Use `__attribute__((packed))` to ensure the frame structure is compact (suitable for direct cast parsing of byte streams).
6. Write a function that uses `offsetof` to print the offset of each field to verify the layout.

```c
// TODO: Write your code here
```

**Hint**: When using `alignas` inside a `packed` structure, be careful—`packed` removes automatic padding, but `alignas` can force a specific field's alignment. Think about this: in a packed structure, if the offset from the frame header to the timestamp is not a multiple of 4, how would you handle it?

## References

- [C struct - cppreference](https://en.cppreference.com/w/c/language/struct)
- [C11 alignas/alignof - cppreference](https://en.cppreference.com/w/c/language/alignment)
- [offsetof - cppreference](https://en.cppreference.com/w/c/types/offsetof)
- [Flexible array members - cppreference](https://en.cppreference.com/w/c/language/struct)
