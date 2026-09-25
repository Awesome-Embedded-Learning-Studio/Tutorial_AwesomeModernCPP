---
chapter: 1
cpp_standard:
- 11
description: Master structure definitions, memory alignment and padding rules,
  flexible array members, and verifying offsets with offsetof
difficulty: beginner
order: 16
platform: host
prerequisites:
- restrict, Incomplete Types, and Structure Pointers
reading_time_minutes: 50
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: Structures and Memory Alignment
translation:
  source: documents/vol1-fundamentals/c_tutorials/12-struct-and-memory-alignment.md
  source_hash: bcedd77b003996534a684d95862f8a14c7940dbb816407dad6ad79f4a4bc02dd
  translated_at: '2026-09-25T13:26:36+00:00'
  engine: anthropic
  token_count: 8000
---
# Structures and Memory Alignment

If you've been writing C this whole time using nothing but basic types—int, float, char and friends—chances are you just haven't yet run into a situation where a bundle of related data has to travel together. The moment you start writing a program with any real substance—a sensor data packet, a configuration table, a communication protocol frame—you'll discover that loose, standalone variables are impossible to manage. The structure (struct) is C's answer: it lets us knead data of different types into a single whole, and then pass, store, and manipulate it as one value.

But structs are far more than "data packaging". The moment a struct lands in memory, the compiler does something behind the scenes that you may never have thought about—memory alignment. It quietly slips padding bytes between your fields so that each one sits at an address the processor "likes". If you don't know this is happening, then one day—while designing a binary protocol frame, setting up a DMA transfer, or hand-writing serialization code—those ghost bytes will probably make you question your sanity.

So in this article, we won't just learn how to define and use structs; we'll also pin down exactly what a struct really looks like in memory.

## Step 1 — Master Structure Definitions and Basic Operations

### Defining a Structure

In C, you define a structure with the `struct` keyword plus a pair of curly braces:

```c
struct SensorReading {
    uint32_t timestamp;
    float temperature;
    float humidity;
    uint8_t status;
};
```

Mind the semicolon at the end—forgetting it is one of the most common beginner compile errors, and the diagnostic usually points at the next line, leaving you completely baffled. `struct SensorReading` is now a type name, but writing `struct SensorReading` every single time is a bit of a mouthful, so we usually pair it with `typedef` to simplify:

```c
typedef struct {
    uint32_t timestamp;
    float temperature;
    float humidity;
    uint8_t status;
} SensorReading;
```

Now we can declare variables by writing `SensorReading reading;` directly—much cleaner. The two forms are functionally equivalent; the only difference lies in how the type name is used: the former requires the `struct` prefix, the latter doesn't. In real projects, the `typedef` form is far more common, especially in embedded development—open any MCU vendor's SDK and you'll see `typedef struct` everywhere.

### Initialization and Assignment

There are several ways to initialize a struct; let's start with the most basic. The first is sequential initialization—values are supplied in the order the fields are defined:

```c
SensorReading r1 = {1700000000, 23.5f, 60.0f, 1};
```

It works, but it doesn't read well—you have to remember which position maps to which field, and the moment the struct definition reorders its fields, every initialization site has to change with it. C99 hands us a better option: the **designated initializer**, which can initialize any field by name:

```c
SensorReading r2 = {
    .timestamp = 1700000000,
    .temperature = 23.5f,
    .humidity = 60.0f,
    .status = 1
};

// No need to follow the definition order; you can also initialize only some fields
SensorReading r3 = {
    .humidity = 45.0f,
    .status = 0
    // timestamp and temperature are automatically initialized to 0
};
```

The advantages of designated initializers are obvious: the code documents itself, it doesn't depend on field order, and unspecified fields are automatically zeroed. Honestly, in modern C code, as long as your compiler supports C99—which virtually all of them do—designated initializers should be your first choice.

Assignment and initialization are two different things for structs. Initialization happens at the declaration; assignment happens after it. C allows direct assignment between structs of the same type; after the assignment, each member holds the same value as the corresponding member of the source object:

```c
SensorReading r4;
r4 = r2;  // Copy every field of r2 into r4
```

But note that struct assignment in C is a **shallow copy**—if the struct has pointer members, after the assignment both structs' pointer fields point at the same memory. This is a classic trap when working with structs that contain dynamically allocated memory.

### Structure Pointers and the Arrow Operator

When a struct is on the large side, or when a function needs to modify the caller's struct, passing a pointer is the only sensible option. This is where the distinction between `.` and `->` shows up:

```c
SensorReading reading = {
    .timestamp = 1700000000,
    .temperature = 25.0f,
    .humidity = 50.0f,
    .status = 1
};

// Access directly through the variable name — use the dot
reading.temperature = 26.0f;

// Access through a pointer — use the arrow
SensorReading* ptr = &reading;
ptr->humidity = 55.0f;
// Equivalent to (*ptr).humidity = 55.0f
```

The `->` operator is just syntactic sugar for `(*ptr).`—nothing mysterious about it. But this particular sugar is so heavily used that you'll simply never write `(*ptr).`—in C, whenever a function parameter involves a struct pointer, you're almost certainly using `->`.

Passing a struct pointer instead of the struct itself in function parameters not only avoids an expensive copy but also allows the function to modify the caller's data. If you don't want the function to modify the data, just add `const`:

```c
/// @brief Print a sensor reading (read-only access)
void print_reading(const SensorReading* r) {
    printf("T=%.1fC H=%.1f%% status=%u\n",
           r->temperature, r->humidity, r->status);
}

/// @brief Update the sensor status (modifiable)
void update_status(SensorReading* r, uint8_t new_status) {
    r->status = new_status;
}
```

This distinction between `const SensorReading*` and `SensorReading*` carries over into C++ as `const` member functions and reference semantics, growing into a more complete "read-only vs. mutable" interface design.

## Step 2 — Understand Memory Alignment and Padding Bytes

Next we move into the most essential—and most easily confusing—part of this tutorial. First, a question: how many bytes does the struct below occupy?

```c
typedef struct {
    uint8_t  a;   // 1 byte
    uint32_t b;   // 4 bytes
    uint8_t  c;   // 1 byte
} WeirdLayout;
```

Intuitively, 1 + 4 + 1 = 6 bytes, right? In reality, on most 32-bit and 64-bit platforms, `sizeof(WeirdLayout)` is **12 bytes**. So where did the extra 6 bytes go? The answer: the compiler stuffed them into the struct as **padding bytes**.

### Why Alignment Is Needed

When a processor accesses memory, it doesn't read one byte at a time. On most architectures, CPUs prefer to access data along 2-, 4-, or 8-byte boundaries—this is what's called **alignment**. A `uint32_t` placed at an address that is a multiple of 4 can be read in one shot; but if it straddles a 4-byte boundary (say it sits at address 3), the CPU may need two reads plus a stitch, and performance suffers. Some architectures are even harsher about it—they raise a hardware exception outright (on ARM, for instance, accessing a misaligned address in certain modes triggers a fault).

So for the sake of performance and correctness, the compiler inserts padding bytes between struct members to ensure each member lands on its naturally aligned address.

### The Rules of Alignment and Padding

There are really just two alignment rules, though they take a little patience to absorb. First: **each member's starting address must be a multiple of that member's alignment requirement**. The alignment requirement of `uint8_t` is 1 (any address will do), `uint16_t` is 2, `uint32_t` is 4, `double` and `uint64_t` are 8, and so on—for basic types, the alignment requirement usually equals the size. Second: **the struct's own size must be a multiple of its strictest alignment requirement**—this is so that in an array of structs, every element can satisfy its alignment requirements.

Now let's return to the `WeirdLayout` example and draw it out byte by byte:

```text
Offset 0  1  2  3  4  5  6  7  8  9  10  11
      [a ][pad pad pad][b         ][c ][pad pad pad]
       ^              ^           ^
       |              |           b: offset 4 (multiple of 4, satisfied)
       |              3 bytes of padding so b aligns to 4
       a: offset 0 (multiple of 1, satisfied)
```

`a` sits at offset 0 and takes 1 byte. `b`'s alignment requirement is 4, but the next available offset is 1—not a multiple of 4—so the compiler fills in 3 bytes of padding and starts `b` at offset 4. `c` lands at offset 8 with an alignment requirement of 1, no problem there. Finally, the struct's strictest alignment requirement is 4 (contributed by `uint32_t b`), so the total size must be a multiple of 4—it currently stands at 9, so it gets padded up to 12.

That's how 6 bytes of actual data end up occupying 12 bytes—50% of the space is wasted on padding.

### Reordering Fields to Reduce Padding

The fix is surprisingly simple: **put the fields with larger alignment requirements first, and smaller ones last**. Let's rearrange the fields of `WeirdLayout`:

```c
typedef struct {
    uint32_t b;   // 4 bytes, offset 0
    uint8_t  a;   // 1 byte, offset 4
    uint8_t  c;   // 1 byte, offset 5
    // 2 bytes of padding (offsets 6-7) so the total size is a multiple of 4
} BetterLayout;
```

Now `sizeof(BetterLayout)` is **8 bytes**—a third smaller than the previous 12. `b` sits at offset 0 (naturally aligned), `a` and `c` follow right behind it, and only 2 bytes of trailing padding remain. This trick is extremely useful in real engineering, especially on memory-constrained embedded systems—it's worth building the habit of ordering fields from the strictest alignment requirement to the loosest.

### Verifying Offsets with offsetof

The C standard library provides the `offsetof` macro (defined in `<stddef.h>`), which tells you exactly where a field sits inside a struct. We reach for it all the time when debugging alignment issues or designing binary protocols:

```c
#include <stddef.h>
#include <stdio.h>

printf("offset of a: %zu\n", offsetof(WeirdLayout, a));  // 0
printf("offset of b: %zu\n", offsetof(WeirdLayout, b));  // 4
printf("offset of c: %zu\n", offsetof(WeirdLayout, c));  // 8
printf("total size: %zu\n", sizeof(WeirdLayout));         // 12
```

Get into the habit of running a quick `offsetof` printout whenever you finish writing a struct, especially when designing communication protocol frames—you'll find that some fields don't sit at the offsets you expected, and that usually means alignment trouble.

## Alignment Control in C11: _Alignas and alignof

In the C99 era, if you needed manual control over alignment, you had to rely on compiler extensions—GCC's `__attribute__((aligned(n)))`, MSVC's `__declspec(align(n))`, and the like. C11 finally standardized this capability, providing the `_Alignas` and `_Alignof` keywords, plus the friendlier macro aliases `alignas` and `alignof` (defined in `<stdalign.h>`).

### alignof: Querying Alignment Requirements

`alignof` can query the alignment requirement of any type:

```c
#include <stdalign.h>
#include <stdio.h>

printf("alignof(uint8_t)  = %zu\n", alignof(uint8_t));   // 1
printf("alignof(uint32_t) = %zu\n", alignof(uint32_t));  // 4
printf("alignof(double)   = %zu\n", alignof(double));    // usually 8
printf("alignof(WeirdLayout) = %zu\n", alignof(WeirdLayout)); // 4
```

Absent extra alignment specifiers, most ABIs give a struct an alignment requirement no looser than the strictest one among its members. `WeirdLayout` contains a `uint32_t`, so in the current test environment its overall alignment requirement is 4; when you need cross-platform certainty, always defer to what `alignof(WeirdLayout)` actually reports.

### alignas: Forcing Alignment

`alignas` can force a variable or a struct member to be allocated on a specified alignment boundary. This is extremely useful in embedded development—DMA transfers, for instance, often require the buffer's starting address to be 4-byte or even 32-byte aligned:

```c
#include <stdalign.h>

// Force the DMA buffer to be 32-byte aligned
alignas(32) uint8_t dma_buffer[256];

// Force the alignment of a specific member inside a struct
typedef struct {
    uint8_t header;
    alignas(4) uint32_t payload;  // Even with header before it, payload is guaranteed 4-byte aligned
} AlignedFrame;
```

The argument to `alignas` must be a power of two, and it cannot be smaller than the type's natural alignment requirement. If you write `alignas(2)` on a `uint32_t`, the compiler will either ignore it or reject it—a `uint32_t` inherently needs 4-byte alignment, and there's no talking it down to 2.

## Designated Initializers in Depth

We briefly touched on designated initializers earlier; now let's take a deeper look at their full capabilities. Designated initializers are a C99 feature that lets you pick which fields to initialize when initializing a struct, union, or array, using the `.member = value` syntax.

Beyond the basic usage shown above, there are a few details worth knowing. For example, you can mix sequential initialization and designated initializers:

```c
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t z;
    uint16_t flags;
} Point3D;

Point3D p1 = {
    10, 20,        // x=10, y=20 (sequential initialization)
    .flags = 0xFF  // designated initialization of flags
    // z is automatically 0
};
```

Designated initializers also work in arrays:

```c
// Sparse initialization — only initialize the indices you need
uint8_t lookup[256] = {
    ['A'] = 1,
    ['B'] = 2,
    ['C'] = 3,
    // everything else is 0
};
```

This style is particularly convenient when building ASCII character mapping tables or command dispatch tables—far clearer than hand-writing an initialization list of 256 elements. Unspecified elements are automatically initialized to zero (just like global variables).

## Step 3 — Get to Know Flexible Array Members

The flexible array member (Flexible Array Member, FAM for short) is a C99 feature that allows an array of unspecified size at the very end of a struct. It sounds a bit strange, but its use is highly practical—whenever you need a struct to carry a stretch of "variable-length trailing data", a FAM is the cleanest way to do it.

```c
typedef struct {
    uint16_t length;
    uint8_t  type;
    uint8_t  data[];  // Flexible array member; doesn't count toward the struct's size
} Packet;
```

`data[]` is an array of incomplete type—it takes up no space inside the struct (`sizeof(Packet)` doesn't include the size of `data`), but it tells the compiler that "a stretch of contiguous memory may follow the end of this struct". To use it, we manually allocate enough memory to hold the struct itself plus the data:

```c
#include <stdlib.h>
#include <string.h>

/// @brief Create a packet with the given payload length
Packet* create_packet(uint8_t type, const uint8_t* payload, uint16_t len) {
    // Allocate: struct size + data length
    Packet* pkt = malloc(sizeof(Packet) + len);
    if (pkt == NULL) {
        return NULL;
    }
    pkt->type = type;
    pkt->length = len;
    memcpy(pkt->data, payload, len);
    return pkt;
}

// Usage
uint8_t payload[] = {0x01, 0x02, 0x03};
Packet* pkt = create_packet(0x42, payload, sizeof(payload));
// Access pkt->data[0], pkt->data[1], pkt->data[2]
free(pkt);
```

Flexible array members see heavy use in communication protocols, variable-length message handling, and packet parsing. In C's early days, people used a trick called the "struct hack" to achieve something similar—putting an array of length 1 (or 0) at the end of a struct, then over-allocating some extra space. But that was undefined behavior; C99's FAM is the standard way.

One point to note: a struct with a flexible array member **can** be passed by value, assigned, or returned—but those operations copy only the fixed members, never the trailing data. Protocol frames and variable-length messages should still be passed by pointer, or you should explicitly allocate the destination buffer and copy the full byte range; otherwise it's easy to mistakenly assume the payload got copied along too.

## Arrays of Structures

Combining structs with arrays is a very common way to organize data. A configuration table, a set of sensor readings, a message queue—at heart they are all arrays of structs:

```c
typedef struct {
    uint8_t  id;
    uint16_t timeout_ms;
    uint8_t  retry_count;
    uint8_t  priority;
} TaskConfig;

// Initialize an array of structs
TaskConfig config_table[] = {
    {.id = 1, .timeout_ms = 100, .retry_count = 3, .priority = 2},
    {.id = 2, .timeout_ms = 200, .retry_count = 5, .priority = 1},
    {.id = 3, .timeout_ms = 50,  .retry_count = 1, .priority = 3},
};

// Get the number of elements in the array
size_t task_count = sizeof(config_table) / sizeof(config_table[0]);
```

Iterating over an array of structs works just like a plain array—you can use subscripts or pointers:

```c
/// @brief Find the ID of the highest-priority task
uint8_t find_highest_priority(const TaskConfig* tasks, size_t count) {
    uint8_t max_priority = 0;
    uint8_t result_id = 0;

    for (size_t i = 0; i < count; i++) {
        if (tasks[i].priority > max_priority) {
            max_priority = tasks[i].priority;
            result_id = tasks[i].id;
        }
    }
    return result_id;
}
```

An array of structs is laid out tightly in memory—each element occupies `sizeof(TaskConfig)` bytes (padding included), and the address of the i-th element is exactly `base + i * sizeof(TaskConfig)`. This is also why a struct needs trailing padding—without it, fields of the second element in the array could end up misaligned.

## `__attribute__((packed))`: Removing Padding

In some scenarios we genuinely need a struct with no padding at all—the most typical case being binary communication protocols. The data an MCU receives over UART/SPI/I2C is a tightly packed byte stream; if the struct has padding and you blindly cast a pointer to interpret it, you'll read wrong values. GCC and Clang provide `__attribute__((packed))` to remove the padding:

```c
typedef struct __attribute__((packed)) {
    uint8_t  header;
    uint16_t length;
    uint8_t  command;
    uint32_t parameter;
} PackedFrame;
```

With this attribute, `sizeof(PackedFrame)` is a clean 1 + 2 + 1 + 4 = 8 bytes, with no padding whatsoever. But mind the cost—accessing misaligned fields degrades performance or even raises hardware exceptions on some architectures. So `packed` should be used only when you truly need a compact layout, not sprinkled everywhere. The ARM Cortex-M family can handle misaligned access in most cases (with a performance penalty), but some older architectures (ARM7TDMI, for example) fault outright.

`packed` only constrains the layout: it deals with neither big-endian nor little-endian, and it doesn't make it safe to cast an arbitrary `uint8_t` receive buffer to a struct pointer. The more robust approach: have the communication layer read byte by byte, or first `memcpy` into a well-aligned temporary object and then decode field by field; afterwards, convert into a naturally aligned internal struct for the business code to use. Parsing and business logic stay separate, each taking what it needs.

## Bridging to C++

### From struct to class

In C, a `struct` can contain only data members—no member functions, no access control, no inheritance. C++ keeps the `struct` keyword but grants it nearly the same capabilities as `class`. The only difference lies in the default access level: members of a `struct` default to `public`, members of a `class` default to `private`. Beyond that, a C++ `struct` can have constructors, destructors, member functions, inheritance, virtual functions—it can do everything.

```cpp
// A struct in C++ — member functions allowed
struct SensorReading {
    uint32_t timestamp;
    float temperature;
    float humidity;

    // Member functions
    bool is_overheating() const {
        return temperature > 85.0f;
    }

    void print() const {
        printf("T=%.1fC H=%.1f%%\n", temperature, humidity);
    }
};
```

So when you see `struct` in C++ code, don't assume it's the same thing as a C structure—it's just a class that defaults to public.

### POD Types and trivially copyable

C++ has a dedicated concept for "simple structs compatible with the C language": the POD type (Plain Old Data). Put simply, if a struct has no virtual functions, no non-trivial constructors/destructors, and all of its members are POD types, then it is itself POD. POD types can be safely copied with `memcpy`, zeroed with `memset`, and safely binary-serialized and deserialized—because their memory layout is exactly identical to C's.

After C++11, the notion of POD was refined into several more precise type traits: `is_trivially_copyable`, `is_standard_layout`, and so on. Understanding these concepts is very important in cross-language interop (mixed C/C++), binary serialization, and shared-memory communication.

### std::aligned_storage

The C++ standard library provides `std::aligned_storage` (since C++11, superseded by `alignas` from C++23 onward), a type-traits utility for manually controlling the alignment of a block of raw memory. You'll run into it in advanced scenarios such as type-erased containers, memory pools, and placement new:

```cpp
#include <type_traits>

// Allocate a block of raw memory aligned to 64 bytes
alignas(64) std::byte storage[sizeof(MyStruct)];

// Or use std::aligned_storage (the pre-C++23 approach)
using AlignedStorage = std::aligned_storage_t<sizeof(MyStruct), alignof(MyStruct)>;
```

These concepts get a full discussion in the C++ chapters ahead. All you need to know here is this: the alignment-control thinking from C comes back in C++ in a more systematic, safer implementation.

## Exercises

### Exercise 1: Alignment Prediction and Verification

**Difficulty: Basic** · Work out sizeof and offsetof by hand first, then verify with a program

Assume `int` has alignment 4 and `sizeof(int) == 4`. First work out by hand the `offsetof` of every field and the `sizeof` of each struct below; then write a program that prints them with `offsetof` and `sizeof` and compare:

```c
#include <stddef.h>
#include <stdio.h>

typedef struct {
    char  a;
    int   b;
    char  c;
} StructA;

typedef struct {
    int   b;
    char  a;
    char  c;
} StructB;

typedef struct {
    char  a;
    char  c;
    int   b;
} StructC;
```

Think about it: the three structs contain exactly the same fields, only in different order—so why do their `sizeof` values differ? Which one saves the most space?

::: details Reference Solution

First turn the three structs into a complete program, and use `offsetof` and `sizeof` to verify the hand-computed results:

```c
#include <stddef.h>
#include <stdio.h>

typedef struct {
    char a;
    int b;
    char c;
} StructA;

typedef struct {
    int b;
    char a;
    char c;
} StructB;

typedef struct {
    char a;
    char c;
    int b;
} StructC;

int main(void)
{
    printf("StructA: a=%zu, b=%zu, c=%zu, sizeof=%zu\n",
           offsetof(StructA, a), offsetof(StructA, b),
           offsetof(StructA, c), sizeof(StructA));
    printf("StructB: a=%zu, b=%zu, c=%zu, sizeof=%zu\n",
           offsetof(StructB, a), offsetof(StructB, b),
           offsetof(StructB, c), sizeof(StructB));
    printf("StructC: a=%zu, b=%zu, c=%zu, sizeof=%zu\n",
           offsetof(StructC, a), offsetof(StructC, b),
           offsetof(StructC, c), sizeof(StructC));
    return 0;
}
```

Compile and run:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic alignment_verify.c -o alignment_verify && ./alignment_verify
```

In a test environment where `int` is 4 bytes and 4-byte aligned, the output is:

```text
StructA: a=0, b=4, c=8, sizeof=12
StructB: a=4, b=0, c=5, sizeof=8
StructC: a=0, b=4, c=1, sizeof=8
```

Hand-computed results (`int` aligned to 4 bytes):

| Struct | offset of a | offset of b | offset of c | sizeof |
|--------|--------|--------|--------|--------|
| StructA | 0 | 4 | 8 | 12 |
| StructB | 4 | 0 | 5 | 8 |
| StructC | 0 | 4 | 1 | 8 |

In StructA, `a` sits at 0; `b` needs 4-byte alignment, so 3 bytes of padding in front lift it to offset 4; `c` lands at 8; and the tail is padded out to 12. Grouping the large-alignment fields (`int`) together and the small fields (`char`) together cuts down the interior padding. StructB and StructC both take 8 bytes, saving space over StructA's 12.

:::

### Exercise 2: Comparing packed and Explicit Alignment

**Difficulty: Intermediate** · Compare three layouts: default alignment, packed, and _Alignas

For the same set of fields, define structs in three ways—default alignment, `__attribute__((packed))`, and `_Alignas`—print `sizeof` and each field's offset, and see how the three layouts differ:

```c
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    uint8_t  type;
    uint32_t value;
} FrameNormal;

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint32_t value;
} FramePacked;

typedef struct {
    uint8_t  type;
    _Alignas(4) uint32_t value;
} FrameAligned;
```

Think about it: `FramePacked` saves the most space, so why can accessing the `value` field actually be slower on some CPUs—or even crash outright? When should you use packed, and when explicit alignment?

::: details Reference Solution

The program below prints all three layouts in one pass. `__attribute__((packed))` is a GCC/Clang extension, so we compile with GCC here:

```c
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint8_t type;
    uint32_t value;
} FrameNormal;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint32_t value;
} FramePacked;

typedef struct {
    uint8_t type;
    _Alignas(4) uint32_t value;
} FrameAligned;

int main(void)
{
    printf("FrameNormal: type=%zu, value=%zu, sizeof=%zu\n",
           offsetof(FrameNormal, type), offsetof(FrameNormal, value),
           sizeof(FrameNormal));
    printf("FramePacked: type=%zu, value=%zu, sizeof=%zu\n",
           offsetof(FramePacked, type), offsetof(FramePacked, value),
           sizeof(FramePacked));
    printf("FrameAligned: type=%zu, value=%zu, sizeof=%zu\n",
           offsetof(FrameAligned, type), offsetof(FrameAligned, value),
           sizeof(FrameAligned));
    return 0;
}
```

Compile and run:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic packed_compare.c -o packed_compare && ./packed_compare
```

Output:

```text
FrameNormal: type=0, value=4, sizeof=8
FramePacked: type=0, value=1, sizeof=5
FrameAligned: type=0, value=4, sizeof=8
```

| Struct | offset of `type` | offset of `value` | `sizeof` |
|---|---:|---:|---:|
| `FrameNormal` | 0 | 4 | 8 |
| `FramePacked` | 0 | 1 | 5 |
| `FrameAligned` | 0 | 4 | 8 |

`FramePacked`'s problem is `value` itself: it starts at offset 1 and is no longer guaranteed to sit on a 4-byte boundary. On CPUs that usually support misaligned access, such as x86, the processor can often still read the value, but possibly at the cost of extra memory accesses; if the data happens to straddle a cache line, the penalty becomes more noticeable. Move to a CPU with strict alignment requirements, and the compiler either splits a single read into multiple byte reads to work around the problem, or the misaligned read it generates triggers a hardware exception. Above all, don't pass `&frame.value` to another interface as if it were an ordinary `uint32_t*`—that pointer's alignment guarantee is already broken.

So `packed` is not a "turn it on to save space" switch. It belongs where the bytes must match an external format exactly, such as communication protocol frames, file headers, or fixed records in Flash; and it only solves the layout—it won't handle big-endian or little-endian for you. The more robust engineering approach: the transceive layer reads byte by byte, or uses `packed` to describe the external format; once length and byte-order checks are done, immediately copy and decode into a normally aligned internal struct, then hand it to the business code. Hot-path data, arrays of structs, atomic variables, and objects sensitive to access efficiency or hardware constraints—DMA buffers, for instance—should never be packed rashly just to save a few bytes.

`_Alignas` is the opposite kind of tool: introduced in C11, it explicitly guarantees that an object or member satisfies a stricter alignment requirement. In this example, `uint32_t` is naturally 4-byte aligned anyway, so `FrameAligned` ends up with the same layout as `FrameNormal`; its value lies in writing the constraint into the code—for example, a DMA buffer that needs 32-byte alignment, or a member that must start at a specified boundary. Remember just this one rule: prefer `packed` for external byte formats, and keep in-memory everyday data naturally aligned; separate the two worlds with one explicit parse-and-convert step, and the rest of your code never has to run around carrying alignment risk.

:::

### Exercise 3: Communication Protocol Frame Design (Challenge, Optional)

**Difficulty: Challenge** · Optional; assumes some familiarity with CRC and byte order—beginners may skip

Design a binary protocol frame structure for communication between embedded devices: a frame header (start delimiter, frame type, payload length, timestamp), a variable-length payload (flexible array member), and a frame-tail checksum.

- Use `offsetof` to print each field's offset and verify the layout matches expectations
- For the frame-tail checksum field, you can just reserve 2 placeholder bytes and write a `// TODO: fill in CRC16` line—no need to implement the CRC algorithm right now
- Think about it: when devices with different byte orders (big-endian, little-endian) communicate, how should multi-byte fields (the timestamp, for example) be handled?

Hint: this article has covered flexible array members, `_Alignas`, `__attribute__((packed))`, and `offsetof`—they are all your tools. CRC algorithms and byte-order conversion belong to the communication topic; here you only need to be aware of both issues and leave good placeholders, not deliver a complete implementation.

::: details Reference Solution

The exercise only asks you to reserve the checksum field; below is a complete CRC8/CRC16 and protocol frame implementation, for readers with energy to spare to check against. The full program is split across three files:

**crc_ref.h**

```c
#ifndef CRC_REF_H
#define CRC_REF_H

#include <stdint.h>
#include <stdbool.h>

void Append_CRC8_Check_Sum(uint8_t *pchMessage, uint16_t dwLength);

bool Verify_CRC8_Check_Sum(uint8_t *pchMessage, uint16_t dwLength);

uint8_t Get_CRC8_Check_Sum(uint8_t *pchMessage, uint16_t dwLength,
                           uint8_t ucCRC8);

void Append_CRC16_Check_Sum(uint8_t *pchMessage, uint32_t dwLength);

bool Verify_CRC16_Check_Sum(uint8_t *pchMessage, uint32_t dwLength);

uint16_t Get_CRC16_Check_Sum(uint8_t *pchMessage, uint32_t dwLength,
                             uint16_t wCRC);

#endif

```

**crc_ref.c**

```c

#include "crc_ref.h"

#include <stddef.h>

/* The CRC lookup tables and interface naming are adapted from the official RoboMaster example; the original names are kept for easy side-by-side comparison. */

// Table-driven CRC

// Initial value used by CRC8.
static const uint8_t k_crc8_init = 0xffu;

// CRC8 precomputed state-transition table.
// Each round uses current_crc ^ next_byte as the index; the table entry is the next CRC state after folding in that byte.
static const uint8_t k_crc8_tab[256] = {
    0x00, 0x5e, 0xbc, 0xe2, 0x61, 0x3f, 0xdd, 0x83, 0xc2, 0x9c, 0x7e, 0x20,
    0xa3, 0xfd, 0x1f, 0x41, 0x9d, 0xc3, 0x21, 0x7f, 0xfc, 0xa2, 0x40, 0x1e,
    0x5f, 0x01, 0xe3, 0xbd, 0x3e, 0x60, 0x82, 0xdc, 0x23, 0x7d, 0x9f, 0xc1,
    0x42, 0x1c, 0xfe, 0xa0, 0xe1, 0xbf, 0x5d, 0x03, 0x80, 0xde, 0x3c, 0x62,
    0xbe, 0xe0, 0x02, 0x5c, 0xdf, 0x81, 0x63, 0x3d, 0x7c, 0x22, 0xc0, 0x9e,
    0x1d, 0x43, 0xa1, 0xff, 0x46, 0x18, 0xfa, 0xa4, 0x27, 0x79, 0x9b, 0xc5,
    0x84, 0xda, 0x38, 0x66, 0xe5, 0xbb, 0x59, 0x07, 0xdb, 0x85, 0x67, 0x39,
    0xba, 0xe4, 0x06, 0x58, 0x19, 0x47, 0xa5, 0xfb, 0x78, 0x26, 0xc4, 0x9a,
    0x65, 0x3b, 0xd9, 0x87, 0x04, 0x5a, 0xb8, 0xe6, 0xa7, 0xf9, 0x1b, 0x45,
    0xc6, 0x98, 0x7a, 0x24, 0xf8, 0xa6, 0x44, 0x1a, 0x99, 0xc7, 0x25, 0x7b,
    0x3a, 0x64, 0x86, 0xd8, 0x5b, 0x05, 0xe7, 0xb9, 0x8c, 0xd2, 0x30, 0x6e,
    0xed, 0xb3, 0x51, 0x0f, 0x4e, 0x10, 0xf2, 0xac, 0x2f, 0x71, 0x93, 0xcd,
    0x11, 0x4f, 0xad, 0xf3, 0x70, 0x2e, 0xcc, 0x92, 0xd3, 0x8d, 0x6f, 0x31,
    0xb2, 0xec, 0x0e, 0x50, 0xaf, 0xf1, 0x13, 0x4d, 0xce, 0x90, 0x72, 0x2c,
    0x6d, 0x33, 0xd1, 0x8f, 0x0c, 0x52, 0xb0, 0xee, 0x32, 0x6c, 0x8e, 0xd0,
    0x53, 0x0d, 0xef, 0xb1, 0xf0, 0xae, 0x4c, 0x12, 0x91, 0xcf, 0x2d, 0x73,
    0xca, 0x94, 0x76, 0x28, 0xab, 0xf5, 0x17, 0x49, 0x08, 0x56, 0xb4, 0xea,
    0x69, 0x37, 0xd5, 0x8b, 0x57, 0x09, 0xeb, 0xb5, 0x36, 0x68, 0x8a, 0xd4,
    0x95, 0xcb, 0x29, 0x77, 0xf4, 0xaa, 0x48, 0x16, 0xe9, 0xb7, 0x55, 0x0b,
    0x88, 0xd6, 0x34, 0x6a, 0x2b, 0x75, 0x97, 0xc9, 0x4a, 0x14, 0xf6, 0xa8,
    0x74, 0x2a, 0xc8, 0x96, 0x15, 0x4b, 0xa9, 0xf7, 0xb6, 0xe8, 0x0a, 0x54,
    0xd7, 0x89, 0x6b, 0x35,
};

// Initial value used by the system's whole-frame CRC16.
static const uint16_t k_crc16_init = 0xffffu;

// CRC16 precomputed state-transition table.
static const uint16_t k_crc16_tab[256] = {
    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf, 0x8c48,
    0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7, 0x1081, 0x0108,
    0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e, 0x9cc9, 0x8d40, 0xbfdb,
    0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876, 0x2102, 0x308b, 0x0210, 0x1399,
    0x6726, 0x76af, 0x4434, 0x55bd, 0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e,
    0xfae7, 0xc87c, 0xd9f5, 0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e,
    0x54b5, 0x453c, 0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd,
    0xc974, 0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
    0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3, 0x5285,
    0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a, 0xdecd, 0xcf44,
    0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72, 0x6306, 0x728f, 0x4014,
    0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9, 0xef4e, 0xfec7, 0xcc5c, 0xddd5,
    0xa96a, 0xb8e3, 0x8a78, 0x9bf1, 0x7387, 0x620e, 0x5095, 0x411c, 0x35a3,
    0x242a, 0x16b1, 0x0738, 0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862,
    0x9af9, 0x8b70, 0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e,
    0xf0b7, 0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
    0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036, 0x18c1,
    0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e, 0xa50a, 0xb483,
    0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5, 0x2942, 0x38cb, 0x0a50,
    0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd, 0xb58b, 0xa402, 0x9699, 0x8710,
    0xf3af, 0xe226, 0xd0bd, 0xc134, 0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7,
    0x6e6e, 0x5cf5, 0x4d7c, 0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1,
    0xa33a, 0xb2b3, 0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72,
    0x3efb, 0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
    0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a, 0xe70e,
    0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1, 0x6b46, 0x7acf,
    0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9, 0xf78f, 0xe606, 0xd49d,
    0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330, 0x7bc7, 0x6a4e, 0x58d5, 0x495c,
    0x3de3, 0x2c6a, 0x1ef1, 0x0f78,
};

// Compute the CRC8 over a run of contiguous bytes.
uint8_t Get_CRC8_Check_Sum(uint8_t *pchMessage, uint16_t dwLength,
                           uint8_t ucCRC8)
{
  uint8_t ucIndex = 0u;

  // Each iteration consumes 1 byte and folds it into the current CRC state.
  while (dwLength-- != 0u)
  {
    ucIndex = ucCRC8 ^ (*pchMessage++);
    ucCRC8 = k_crc8_tab[ucIndex];
  }
  return ucCRC8;
}

// Verify the CRC8 already stored at the end of the buffer.
bool Verify_CRC8_Check_Sum(uint8_t *pchMessage, uint16_t dwLength)
{
  uint8_t expected = 0u;

  // The system frame header must contain at least the valid fields plus the 1-byte CRC8.
  if ((pchMessage == NULL) || (dwLength <= 2u))
  {
    return false;
  }

  // The computation window excludes the CRC8 byte already stored at the end.
  expected = Get_CRC8_Check_Sum(pchMessage, (uint16_t)(dwLength - 1u),
                                k_crc8_init);
  return expected == pchMessage[dwLength - 1u];
}

// Write the freshly recomputed CRC8 to the end of the buffer.
void Append_CRC8_Check_Sum(uint8_t *pchMessage, uint16_t dwLength)
{
  // Don't write to a null pointer or an undersized buffer.
  if ((pchMessage == NULL) || (dwLength <= 2u))
  {
    return;
  }

  // The check byte itself does not take part in this CRC8 computation.
  pchMessage[dwLength - 1u] =
      Get_CRC8_Check_Sum(pchMessage, (uint16_t)(dwLength - 1u), k_crc8_init);
}

// Compute the CRC16 over a run of contiguous bytes.
uint16_t Get_CRC16_Check_Sum(uint8_t *pchMessage, uint32_t dwLength,
                             uint16_t wCRC)
{
  uint8_t chData = 0u;

  if (pchMessage == NULL)
  {
    return 0xffffu;
  }

  // Fold each input byte into the low bits of the CRC, then advance to the next state via the table.
  while (dwLength-- != 0u)
  {
    chData = *pchMessage++;
    wCRC = (uint16_t)((wCRC >> 8u) ^
                      k_crc16_tab[(uint8_t)(wCRC ^ chData)]);
  }
  return wCRC;
}

// Verify the CRC16 stored in the last 2 bytes of a complete frame.

bool Verify_CRC16_Check_Sum(uint8_t *pchMessage, uint32_t dwLength)
{
  uint16_t expected = 0u;

  // A complete frame must contain at least the protected data plus the 2-byte CRC16.
  if ((pchMessage == NULL) || (dwLength <= 2u))
  {
    return false;
  }

  // Recompute only the protected payload/header region; the trailing CRC16 is not part of the computation.
  expected = Get_CRC16_Check_Sum(pchMessage, dwLength - 2u, k_crc16_init);
  return (((expected & 0x00ffu) == pchMessage[dwLength - 2u]) &&
          (((expected >> 8u) & 0x00ffu) == pchMessage[dwLength - 1u]));
}

// Append the CRC16 to the last 2 bytes of a complete frame buffer.
void Append_CRC16_Check_Sum(uint8_t *pchMessage, uint32_t dwLength)
{
  uint16_t wCRC = 0u;

  // Don't write to a null pointer or an undersized frame buffer.
  if ((pchMessage == NULL) || (dwLength <= 2u))
  {
    return;
  }

  // Little-endian protocol: low byte first, high byte second.
  wCRC = Get_CRC16_Check_Sum(pchMessage, dwLength - 2u, k_crc16_init);
  pchMessage[dwLength - 2u] = (uint8_t)(wCRC & 0x00ffu);
  pchMessage[dwLength - 1u] = (uint8_t)((wCRC >> 8u) & 0x00ffu);
}

```

**protocol_frame.c**

```c
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc_ref.h"

#define FRAME_START_BYTE 0xA5u /* Fixed header byte, used by the receiver to find the start of a frame. */
#define FRAME_CRC16_SIZE 2u    /* The CRC16 occupies two on-wire bytes. */

/*
 * On-wire frame format (all offsets are relative to the start address of protocol_frame_t):
 *
 *   Offset 0      start              Header byte, 1 byte
 *   Offset 1      type               Category, 1 byte
 *   Offset 2..3   payload_length     Length of the payload, 2 bytes, little-endian
 *   Offset 4..7   timestamp          Timestamp, 4 bytes, little-endian
 *   Offset 8      header_crc8        Header CRC8, 1 byte
 *   Offset 9..    payload            Variable-length application data
 *   Last 2 bytes  crc16              Whole-frame CRC16, low byte first
 *
 * The payload is the protocol's variable-length region. A flexible array member must be the
 * last member of the struct, so the CRC16 tail is placed after the payload through extra
 * space in the same allocation. Multi-byte fields are stored as byte arrays and explicitly
 * read and written in the protocol's byte order, so that struct padding and host byte order
 * cannot influence the on-wire format.
 */
typedef struct {
    uint8_t start;
    uint8_t type;
    uint8_t payload_length[2];
    uint8_t timestamp[4];
    uint8_t header_crc8;
    uint8_t payload[];
} protocol_frame_t;

enum {
    FRAME_START_OFFSET = offsetof(protocol_frame_t, start), // Offset of the header byte
    FRAME_TYPE_OFFSET = offsetof(protocol_frame_t, type), // Offset of the frame type
    FRAME_PAYLOAD_LENGTH_OFFSET = offsetof(protocol_frame_t, payload_length), // Offset of the payload length
    FRAME_TIMESTAMP_OFFSET = offsetof(protocol_frame_t, timestamp), // Offset of the timestamp
    FRAME_HEADER_CRC8_OFFSET = offsetof(protocol_frame_t, header_crc8), // Offset of the header CRC8
    FRAME_PAYLOAD_OFFSET = offsetof(protocol_frame_t, payload), // Offset of the variable-length data
    FRAME_HEADER_SIZE = offsetof(protocol_frame_t, payload) // Length of the fixed on-wire header
};

/*
 * Write a 16-bit integer into the on-wire buffer in the chosen byte order.
 *
 * big_endian == true: big-endian, high byte first;
 * big_endian == false: little-endian, low byte first.
 * C has no optional default parameters, so false is the default choice a caller
 * should use, and it must be passed explicitly at every call site. The current
 * protocol's call sites uniformly pass false, i.e. little-endian.
 */
static void write_u16(uint8_t *destination, uint16_t value,
                         bool big_endian) {
    if (big_endian) {
        destination[0] = (uint8_t)(value >> 8u);
        destination[1] = (uint8_t)value;
    } else {
        destination[0] = (uint8_t)value;
        destination[1] = (uint8_t)(value >> 8u);
    }
}

/* Write a 32-bit integer into the on-wire buffer in the byte order selected by big_endian. */
static void write_u32(uint8_t *destination, uint32_t value,
                         bool big_endian) {
    if (big_endian) {
        destination[0] = (uint8_t)(value >> 24u);
        destination[1] = (uint8_t)(value >> 16u);
        destination[2] = (uint8_t)(value >> 8u);
        destination[3] = (uint8_t)value;
    } else {
        destination[0] = (uint8_t)value;
        destination[1] = (uint8_t)(value >> 8u);
        destination[2] = (uint8_t)(value >> 16u);
        destination[3] = (uint8_t)(value >> 24u);
    }
}

/* Read a 16-bit integer from the on-wire buffer in the byte order selected by big_endian. */
static uint16_t read_u16(const uint8_t *source, bool big_endian) {
    if (big_endian) {
        return (uint16_t)(((uint16_t)source[0] << 8u) |
                          (uint16_t)source[1]);
    }

    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8u));
}

/* Read a 32-bit integer from the on-wire buffer in the byte order selected by big_endian. */
static uint32_t read_u32(const uint8_t *source, bool big_endian) {
    if (big_endian) {
        return ((uint32_t)source[0] << 24u) |
               ((uint32_t)source[1] << 16u) |
               ((uint32_t)source[2] << 8u) |
               (uint32_t)source[3];
    }

    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8u) |
           ((uint32_t)source[2] << 16u) |
           ((uint32_t)source[3] << 24u);
}

/*
 * Assemble one complete on-wire frame.
 *
 * The function first validates lengths and pointers, then allocates space; after that it
 * serializes the fields per the protocol and copies the payload, and finally computes the
 * CRC8 and CRC16. That way the CRCs cover the exact bytes that will actually be sent.
 */
static bool data_process(uint8_t type, uint32_t timestamp,
                        const uint8_t *payload, size_t payload_size,
                        protocol_frame_t **frame_out,
                        uint32_t *frame_size_out) {
    size_t calculated_frame_size; // Calculated frame size
    protocol_frame_t *frame; // The allocated frame buffer

    /* Results cannot be written back when output parameters are invalid. */
    if ((frame_out == NULL) || (frame_size_out == NULL)) {
        return false;
    }

    *frame_out = NULL;
    *frame_size_out = 0u;

    /* payload_length is only 16 bits; beyond that, the forced conversion would truncate. */
    if (payload_size > UINT16_MAX) {
        fputs("Payload is too large for the 16-bit length field.\n", stderr);
        return false;
    }
    /* A NULL payload pointer is allowed only for an empty payload. */
    if ((payload == NULL) && (payload_size != 0u)) {
        fputs("Payload pointer is NULL for a non-empty payload.\n", stderr);
        return false;
    }
    /* Check the addition before computing the total length, to guard against size_t overflow. */
    if (payload_size > SIZE_MAX - sizeof(protocol_frame_t) - FRAME_CRC16_SIZE) {
        fputs("Frame size calculation overflowed size_t.\n", stderr);
        return false;
    }

    calculated_frame_size =
        sizeof(protocol_frame_t) + payload_size + FRAME_CRC16_SIZE;
    /* payload_size is already capped at uint16_t, so a full frame is at most 65546 bytes and converts safely to uint32_t. */

    frame = calloc(1u, calculated_frame_size);
    if (frame == NULL) {
        fputs("Failed to allocate protocol frame.\n", stderr);
        return false;
    }

    /* Write the fixed fields: ordinary multi-byte fields uniformly use little-endian. */
    frame->start = FRAME_START_BYTE;
    frame->type = type;
    /* The protocol specifies little-endian for payload_length, so false is passed explicitly here. */
    write_u16(frame->payload_length, (uint16_t)payload_size, false);
    /* The protocol specifies little-endian for timestamp, so false is passed explicitly here. */
    write_u32(frame->timestamp, timestamp, false);
    frame->header_crc8 = 0u;

    /* The payload is the variable-length region, immediately after the fixed header. */
    if (payload_size != 0u) {
        memcpy(frame->payload, payload, payload_size);
    }

    /* The CRC8 covers the first 8 header bytes and writes its result to offset 8. */
    Append_CRC8_Check_Sum((uint8_t *)frame, (uint16_t)FRAME_HEADER_SIZE);
    /* The CRC16 computation covers the header and payload; its result goes into the two CRC16 bytes reserved at the frame tail. */
    Append_CRC16_Check_Sum((uint8_t *)frame, (uint32_t)calculated_frame_size);

    *frame_out = frame;
    *frame_size_out = (uint32_t)calculated_frame_size;
    return true;
}

static void print_layout(void) {
    printf("start offset:          %zu\n", (size_t)FRAME_START_OFFSET);
    printf("type offset:           %zu\n", (size_t)FRAME_TYPE_OFFSET);
    printf("payload_length offset: %zu\n", (size_t)FRAME_PAYLOAD_LENGTH_OFFSET);
    printf("timestamp offset:      %zu\n", (size_t)FRAME_TIMESTAMP_OFFSET);
    printf("header_crc8 offset:    %zu\n", (size_t)FRAME_HEADER_CRC8_OFFSET);
    printf("payload offset:        %zu\n", (size_t)FRAME_PAYLOAD_OFFSET);
}

int main(void) {
    uint8_t crc_test_vector[] = "123456789";
    /* Sample payload; in a real project, replace it with the application data to send. */
    const uint8_t sample_payload[] = {0x10u, 0x20u, 0x30u, 0x40u};
    const size_t payload_size = sizeof(sample_payload);
    protocol_frame_t *frame = NULL;
    uint32_t frame_size = 0u;
    const uint8_t *crc16;

    /* type=0x01 marks a sample application category; the timestamp is fixed at 0x12345678. */
    if (data_process(0x01u, 0x12345678u, sample_payload,
                     payload_size, &frame, &frame_size) == false) {
        return EXIT_FAILURE;
    }

    /* The CRC16 sits after the payload, so its starting offset varies with the payload length. */
    crc16 = &frame->payload[payload_size];

    /* The fixed test vector locks in the current CRC parameters, guarding against accidental edits to the lookup tables or initial values. */
    if ((Get_CRC8_Check_Sum(crc_test_vector, 9u, 0xffu) != 0x0bu) ||
        (Get_CRC16_Check_Sum(crc_test_vector, 9u, 0xffffu) != 0x6f91u)) {
        fputs("CRC test vector check failed.\n", stderr);
        free(frame);
        return EXIT_FAILURE;
    }

    print_layout();
    printf("CRC test vector:       passed (CRC-8=0x0B, CRC-16=0x6F91)\n");
    printf("crc16 offset:          %u\n",
           (unsigned int)(crc16 - (const uint8_t *)frame));
    printf("total frame size:      %u bytes\n", (unsigned int)frame_size);
    /* Read the on-wire fields through the decode functions to verify both byte order and values. */
    printf("payload length:        %u\n",
           (unsigned int)read_u16(frame->payload_length, false));
    printf("timestamp:             0x%08X\n",
           (unsigned int)read_u32(frame->timestamp, false));
    printf("timestamp wire bytes:  %02X %02X %02X %02X\n",
           (unsigned int)frame->timestamp[0],
           (unsigned int)frame->timestamp[1],
           (unsigned int)frame->timestamp[2],
           (unsigned int)frame->timestamp[3]);
    printf("crc8 check:            %s\n",
           Verify_CRC8_Check_Sum((uint8_t *)frame,
                                 (uint16_t)FRAME_HEADER_SIZE)
               ? "passed"
               : "failed");
    printf("crc16 check:           %s\n",
           Verify_CRC16_Check_Sum((uint8_t *)frame, frame_size)
               ? "passed"
               : "failed");

    free(frame);
    return EXIT_SUCCESS;
}

```

Put the three files in the same directory, then compile and run:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic protocol_frame.c crc_ref.c -o protocol_frame && ./protocol_frame
```

With this test data, the output is:

```text
start offset:          0
type offset:           1
payload_length offset: 2
timestamp offset:      4
header_crc8 offset:    8
payload offset:        9
CRC test vector:       passed (CRC-8=0x0B, CRC-16=0x6F91)
crc16 offset:          13
total frame size:      15 bytes
payload length:        4
timestamp:             0x12345678
timestamp wire bytes:  78 56 34 12
crc8 check:            passed
crc16 check:           passed
```

Notice that we didn't stuff `uint16_t` or `uint32_t` directly into the on-wire struct; instead, multi-byte fields are kept as byte arrays and explicitly serialized by `write_u16` and `write_u32`—the current protocol passes `false` when calling them, fixing little-endian. That way neither struct padding nor host byte order can silently change the protocol format, and the receiver can restore the values with the corresponding read functions.

The fixed on-wire header length is taken from `offsetof(protocol_frame_t, payload)`, rather than treating `sizeof(protocol_frame_t)` as the protocol length. The flexible array member itself doesn't count toward `sizeof`, but the standard allows a struct to keep extra padding at its end; computing the on-wire header length from a member offset is what pinpoints where the payload truly begins. Memory allocation still uses `sizeof(protocol_frame_t)`, so there is room even for any possible trailing padding; the CRC16 lives in the same allocation, in the two extra bytes right after the payload.

A CRC is not just the name "CRC16": the polynomial, initial value, input/output reflection, final XOR value, and on-wire byte order jointly determine the result. In this example the CRC-8 uses polynomial `0x31`, initial value `0xFF`, reflected input/output, and final XOR value `0x00`; the CRC-16 uses polynomial `0x1021`, initial value `0xFFFF`, reflected input/output, and final XOR value `0x0000`, written little-endian at the frame tail. The fixed test results for `"123456789"` are `0x0B` and `0x6F91` respectively, guarding the example's internal parameters against accidental edits; when connecting to real devices, you must still check every item against the protocol documentation or the device's own test frames—never reuse a CRC just because the bit counts happen to match.

:::

## References

- [C struct - cppreference](https://en.cppreference.com/w/c/language/struct)
- [C11 alignas/alignof - cppreference](https://en.cppreference.com/w/c/language/_Alignas)
- [offsetof - cppreference](https://en.cppreference.com/w/c/types/offsetof)
- [Flexible array members - cppreference](https://en.cppreference.com/w/c/language/struct)
