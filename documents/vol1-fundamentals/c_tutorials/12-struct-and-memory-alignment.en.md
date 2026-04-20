---
chapter: 1
cpp_standard:
- 11
description: Master struct definitions, memory alignment and padding rules, flexible
  array members, and `offsetof` validation
difficulty: beginner
order: 16
platform: host
prerequisites:
- restrict、不完整类型与结构体指针
reading_time_minutes: 24
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: Structs and Memory Alignment
translation:
  source: documents/vol1-fundamentals/c_tutorials/12-struct-and-memory-alignment.md
  source_hash: b0fa66698cfdb3a2581c038aaba68ef7299ab625d4ccbe410a6e5587ad0f7432
  translated_at: '2026-04-20T03:30:36.017124+00:00'
  engine: anthropic
  token_count: 3333
---
# Structs and Memory Alignment

If you have been writing C up to this point using only basic types—`int`, `float`, `char`, and the like—it is probably because you have not yet encountered a scenario where you need to bundle related data together. Once you start writing more substantial programs, such as a sensor data packet, a configuration table, or a communication protocol frame, you will find that loose variables are simply unmanageable. The struct is C's answer: it lets us combine data of different types into a single whole, which we can then pass, store, and manipulate as one value.

But structs are far more than just "data bundles." The moment we place a struct in memory, the compiler does something behind the scenes that you might never have considered—memory alignment. It silently inserts padding bytes between your fields so that each field lands on an address the processor "prefers." If you are unaware of this, there will come a day when you are designing a binary protocol frame, doing a DMA transfer, or writing serialization code by hand, and those phantom bytes will drive you to question your sanity.

So in this chapter, we will not only learn how to define and use structs, but also thoroughly understand what a struct truly looks like in memory.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Proficiently define, initialize, and operate on structs and their pointers
> - [ ] Understand the principles of memory alignment and the distribution rules for padding bytes
> - [ ] Use `_Alignas`, `alignof`, and `offsetof` for alignment control and verification
> - [ ] Master the use of designated initializers and flexible array members
> - [ ] Understand the evolution from C structs to C++ classes

## Environment Setup

We will run all of the following experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c17`

## Step One — Mastering Struct Definition and Basic Operations

### Defining a Struct

In C, we define a struct using the `struct` keyword followed by a pair of curly braces:

```c
struct SensorReading {
    uint32_t timestamp;
    float temperature;
    float humidity;
    uint8_t status;
};
```

Note the semicolon at the end—forgetting it is one of the most common compilation errors for beginners, and the error message usually points to the next line, leaving you baffled. `struct SensorReading` is now a type name, but writing `struct SensorReading` every time is indeed a bit verbose, so we typically pair it with `typedef` to simplify:

```c
typedef struct {
    uint32_t timestamp;
    float temperature;
    float humidity;
    uint8_t status;
} SensorReading;
```

This way we can write `SensorReading reading;` directly to declare variables, which is much cleaner. The two approaches are functionally equivalent; the only difference is how the type name is used: the former requires the `struct` prefix, while the latter does not. In real-world projects, the `typedef` approach is far more common, especially in embedded development—if you look at any MCU vendor's SDK, it is full of `typedef struct`.

### Initialization and Assignment

There are several ways to initialize a struct. We will start with the most basic. The first is sequential initialization—providing values in the order the fields are defined:

```c
SensorReading r1 = {1700000000, 23.5f, 60.0f, 1};
```

This works, but its readability is not great—you must remember which position corresponds to which field, and if the struct definition changes the order, all initialization code must be updated accordingly. C99 gave us a better solution: the **designated initializer**, which lets us initialize any field by name:

```c
SensorReading r2 = {
    .timestamp = 1700000000,
    .temperature = 23.5f,
    .humidity = 60.0f,
    .status = 1
};

// 不需要按定义顺序，也可以只初始化部分字段
SensorReading r3 = {
    .humidity = 45.0f,
    .status = 0
    // timestamp 和 temperature 自动初始化为 0
};
```

The benefits of designated initializers are obvious: the code is self-documenting, it does not depend on field order, and unspecified fields are automatically zeroed. Frankly, in modern C code, as long as your compiler supports C99 (which virtually all do), you should prefer designated initializers.

Struct assignment and initialization are two different things. Initialization happens at declaration, while assignment happens after declaration. C allows direct assignment between structs of the same type, which performs a byte-by-byte copy:

```c
SensorReading r4;
r4 = r2;  // 把 r2 的所有字段复制到 r4
```

But note that struct assignment in C is a **shallow copy**—if the struct contains pointer members, the pointer fields of both structs will point to the same memory after assignment. This is a classic pitfall when dealing with structs that contain dynamically allocated memory.

### Struct Pointers and the Arrow Operator

When a struct is large, or when we need to modify the caller's struct inside a function, passing a pointer is the only reasonable approach. This is where the difference between `.` and `->` comes in:

```c
SensorReading reading = {
    .timestamp = 1700000000,
    .temperature = 25.0f,
    .humidity = 50.0f,
    .status = 1
};

// 通过变量名直接访问——用点号
reading.temperature = 26.0f;

// 通过指针访问——用箭头
SensorReading* ptr = &reading;
ptr->humidity = 55.0f;
// 等价于 (*ptr).humidity = 55.0f
```

The `->` operator is simply syntactic sugar for `(*ptr).`; there is nothing mysterious about it. But this syntactic sugar is so commonly used that you would almost never write `(*ptr).`—in C, whenever a function parameter is a struct pointer, you are almost certainly using `->`.

Passing a struct pointer instead of the struct itself as a function parameter not only avoids expensive copy overhead but also allows the function to modify the caller's data. If you do not want the function to modify the data, simply add `const`:

```c
/// @brief 打印传感器读数（只读访问）
void print_reading(const SensorReading* r) {
    printf("T=%.1fC H=%.1f%% status=%u\n",
           r->temperature, r->humidity, r->status);
}

/// @brief 更新传感器状态（可修改）
void update_status(SensorReading* r, uint8_t new_status) {
    r->status = new_status;
}
```

This distinction between `const SensorReading*` and `SensorReading*` is inherited in C++ into `const` member functions and reference semantics, forming a more complete "read-only vs. mutable" interface design.

## Step Two — Understanding Memory Alignment and Padding Bytes

Now we are entering the most core and potentially confusing part of this tutorial. Let us start with a question: how many bytes does the following struct occupy?

```c
typedef struct {
    uint8_t  a;   // 1 字节
    uint32_t b;   // 4 字节
    uint8_t  c;   // 1 字节
} WeirdLayout;
```

Intuitively, 1 + 4 + 1 = 6 bytes, right? But in reality, on most 32-bit and 64-bit platforms, `sizeof(WeirdLayout)` is **12 bytes**. Where did the extra 6 bytes go? The answer is that the compiler inserted them into the struct as **padding bytes**.

### Why Alignment Is Needed

When a processor accesses memory, it does not read one byte at a time. Most CPU architectures prefer to access data on 2-, 4-, or 8-byte boundaries—this is what we call **alignment**. If an `uint32_t` is placed at an address that is a multiple of 4, the CPU can read it in one go; but if it straddles a 4-byte boundary (for example, at address 3), the CPU might need to read twice and stitch the results together, which incurs a performance penalty. Some architectures are even more extreme—they will throw a hardware exception directly (for instance, ARM triggers a fault when accessing unaligned addresses in certain modes).

So, for performance and correctness, the compiler inserts padding bytes between struct members to ensure that each member lands on its naturally aligned address.

### Alignment and Padding Rules

There are essentially two alignment rules, but understanding them requires a bit of patience. The first rule: **the starting address of each member must be an integer multiple of that member's alignment requirement**. The alignment requirement for `uint8_t` is 1 (any address works), for `uint16_t` it is 2, for `uint32_t` it is 4, and for `double` and `uint64_t` it is 8, and so on—the alignment requirement of a basic type usually equals its size. The second rule: **the total size of the struct itself must be an integer multiple of its largest alignment requirement**—this ensures that in an array of structs, every element satisfies the alignment requirement.

Now let us return to the `WeirdLayout` example and map it out byte by byte:

```text
偏移  0  1  2  3  4  5  6  7  8  9  10  11
     [a ][pad pad pad][b         ][c ][pad pad pad]
      ^              ^           ^
      |              |           b: 偏移 4（4 的倍数，满足）
      |              填充 3 字节让 b 对齐到 4
      a: 偏移 0（1 的倍数，满足）
```

`a` is at offset 0, occupying 1 byte. The alignment requirement for `b` is 4, but the next available offset is 1, which is not a multiple of 4, so the compiler inserts 3 bytes of padding, letting `b` start at offset 4. `c` is at offset 8, with an alignment requirement of 1, so that is fine. Finally, the struct's maximum alignment requirement is 4 (from `uint32_t b`), so the total size must be a multiple of 4—the current size is 9, so it is padded to 12.

This is why data that is only 6 bytes actually occupies 12 bytes—50% of the space is wasted on padding.

### Reordering Fields to Reduce Padding

The solution to this problem is surprisingly simple: **place fields with larger alignment requirements first, and smaller ones last**. Let us reorder the fields of `WeirdLayout`:

```c
typedef struct {
    uint32_t b;   // 4 字节，偏移 0
    uint8_t  a;   // 1 字节，偏移 4
    uint8_t  c;   // 1 字节，偏移 5
    // 填充 2 字节（偏移 6-7），使总大小为 4 的倍数
} BetterLayout;
```

Now `sizeof(BetterLayout)` is **8 bytes**—saving one-third compared to the previous 12. `b` is at offset 0 (naturally aligned), `a` and `c` are packed right after it, and only 2 bytes of trailing padding are needed. This technique is extremely useful in real-world engineering, especially on memory-constrained embedded systems—building the habit of ordering fields from largest to smallest alignment requirement is well worth it.

### Verifying Offsets with offsetof

The C standard library provides the `offsetof` macro (defined in `<stddef.h>`), which can precisely tell you the offset of a specific field within a struct. We frequently use it when debugging alignment issues or designing binary protocols:

```c
#include <stddef.h>
#include <stdio.h>

printf("offset of a: %zu\n", offsetof(WeirdLayout, a));  // 0
printf("offset of b: %zu\n", offsetof(WeirdLayout, b));  // 4
printf("offset of c: %zu\n", offsetof(WeirdLayout, c));  // 8
printf("total size: %zu\n", sizeof(WeirdLayout));         // 12
```

Make it a habit to print the offsets with `offsetof` right after defining a struct, especially when designing communication protocol frames—you will find that some fields' offsets differ from what you expected, and this usually indicates an alignment issue.

## C11 Alignment Control: _Alignas and alignof

In the C99 era, if you needed to manually control alignment, you had to rely on compiler extensions—GCC's `__attribute__((aligned(n)))`, MSVC's `__declspec(align(n))`, and the like. C11 finally standardized this capability, providing the `_Alignas` and `_Alignof` keywords, along with the more friendly macro aliases `alignas` and `alignof` (defined in `<stdalign.h>`).

### alignof: Querying Alignment Requirements

`alignof` can query the alignment requirement of any type:

```c
#include <stdalign.h>
#include <stdio.h>

printf("alignof(uint8_t)  = %zu\n", alignof(uint8_t));   // 1
printf("alignof(uint32_t) = %zu\n", alignof(uint32_t));  // 4
printf("alignof(double)   = %zu\n", alignof(double));    // 通常 8
printf("alignof(WeirdLayout) = %zu\n", alignof(WeirdLayout)); // 4
```

A struct's alignment requirement equals the largest alignment requirement among its members. `WeirdLayout` contains `uint32_t`, so the overall alignment requirement is 4.

### alignas: Forcing Alignment

`alignas` can be used to force a variable or struct member to be allocated on a specified alignment boundary. This is very useful in embedded development—for example, DMA transfers typically require the buffer's starting address to be 4-byte or even 32-byte aligned:

```c
#include <stdalign.h>

// 强制 DMA 缓冲区 32 字节对齐
alignas(32) uint8_t dma_buffer[256];

// 在结构体中强制某个字段的对齐
typedef struct {
    uint8_t header;
    alignas(4) uint32_t payload;  // 即使前面有 header，也保证 payload 4 字节对齐
} AlignedFrame;
```

The argument to `alignas` must be a power of two and cannot be less than the type's natural alignment requirement. If you write `alignas(2)` for an `uint32_t`, the compiler will ignore it or report an error—because `uint32_t` itself requires 4-byte alignment, and you cannot reduce it to 2.

## Designated Initializers in Detail

We briefly mentioned designated initializers earlier; now let us take a deeper look at their full capabilities. Designated initializers are a feature introduced in C99 that allows you to use the `.成员名 = 值` syntax to specify which fields to initialize when initializing structs, unions, and arrays.

In addition to the basic usage shown earlier, there are some noteworthy details. For example, you can mix sequential initialization with designated initializers:

```c
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t z;
    uint16_t flags;
} Point3D;

Point3D p1 = {
    10, 20,        // x=10, y=20（顺序初始化）
    .flags = 0xFF  // 指定初始化 flags
    // z 自动为 0
};
```

You can also use designated initializers in arrays:

```c
// 稀疏初始化——只初始化需要的下标
uint8_t lookup[256] = {
    ['A'] = 1,
    ['B'] = 2,
    ['C'] = 3,
    // 其余全部为 0
};
```

This approach is particularly handy when creating ASCII character mapping tables or command dispatch tables—it is much clearer than hand-writing an initializer list with 256 elements. Unspecified elements are automatically initialized to zero (just like global variables).

## Step Three — Understanding Flexible Array Members

A Flexible Array Member (FAM) is a feature introduced in C99 that allows an array of unspecified size to be placed at the end of a struct. It sounds a bit odd, but its use cases are very practical—when you need a struct with "variable-length trailing data," FAM is the cleanest approach.

```c
typedef struct {
    uint16_t length;
    uint8_t  type;
    uint8_t  data[];  // 柔性数组成员，不占结构体大小
} Packet;
```

`data[]` is an incomplete array type—it occupies no space within the struct (`sizeof(Packet)` does not include the size of `data`), but it tells the compiler "this struct may be followed by a contiguous block of memory." When using it, we need to manually allocate enough memory to hold the struct itself plus the data:

```c
#include <stdlib.h>
#include <string.h>

/// @brief 创建一个指定长度的数据包
Packet* create_packet(uint8_t type, const uint8_t* payload, uint16_t len) {
    // 分配：结构体大小 + 数据长度
    Packet* pkt = malloc(sizeof(Packet) + len);
    if (pkt == NULL) {
        return NULL;
    }
    pkt->type = type;
    pkt->length = len;
    memcpy(pkt->data, payload, len);
    return pkt;
}

// 使用
uint8_t payload[] = {0x01, 0x02, 0x03};
Packet* pkt = create_packet(0x42, payload, sizeof(payload));
// 访问 pkt->data[0], pkt->data[1], pkt->data[2]
free(pkt);
```

Flexible array members are heavily used in communication protocols, variable-length message handling, and packet parsing. In the early days of C, people used a trick called the "struct hack" to achieve similar functionality—placing an array of length 1 (or 0) at the end of the struct and then allocating extra space. But that was undefined behavior; C99's FAM is the standard approach.

One thing to note: structs containing a flexible array member cannot be passed or copied by value—because `sizeof` does not know how large the trailing data is. You can only operate on them through pointers.

## Arrays of Structs

Combining structs and arrays is a very common way to organize data. A configuration table, a set of sensor readings, or a message queue are essentially all arrays of structs:

```c
typedef struct {
    uint8_t  id;
    uint16_t timeout_ms;
    uint8_t  retry_count;
    uint8_t  priority;
} TaskConfig;

// 初始化一个结构体数组
TaskConfig config_table[] = {
    {.id = 1, .timeout_ms = 100, .retry_count = 3, .priority = 2},
    {.id = 2, .timeout_ms = 200, .retry_count = 5, .priority = 1},
    {.id = 3, .timeout_ms = 50,  .retry_count = 1, .priority = 3},
};

// 获取数组元素个数
size_t task_count = sizeof(config_table) / sizeof(config_table[0]);
```

Iterating over an array of structs is the same as iterating over a plain array—you can use subscripts or pointers:

```c
/// @brief 按优先级查找最高优先级任务的 ID
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

Arrays of structs are laid out contiguously in memory—each element's size is `sizeof(TaskConfig)` (including padding), and the address of the i-th element is `base + i * sizeof(TaskConfig)`. This is also why structs need trailing padding—without it, the fields of the second element in an array might become misaligned.

## `__attribute__((packed))`: Removing Padding

There are scenarios where we truly need a struct without any padding—the most typical case is binary communication protocols. The data an MCU receives over UART/SPI/I2C is a tightly packed byte stream; if the struct has padding, casting a pointer directly to interpret it will yield incorrect values. GCC and Clang provide `__attribute__((packed))` to remove padding:

```c
typedef struct __attribute__((packed)) {
    uint8_t  header;
    uint16_t length;
    uint8_t  command;
    uint32_t parameter;
} PackedFrame;
```

With this attribute, `sizeof(PackedFrame)` is a pure 1 + 2 + 1 + 4 = 8 bytes, with no padding at all. But be aware of the cost—accessing unaligned fields on some architectures can cause performance degradation or even hardware exceptions. So `packed` should only be used when you genuinely need a compact layout, not sprinkled everywhere. The ARM Cortex-M series can handle unaligned access in most cases (with a performance penalty), but some older architectures (like the ARM7TDMI) will fault directly.

A safer approach is: **use a packed struct at the communication layer to parse raw bytes, then immediately convert it to an aligned internal struct for use**. This separates parsing from business logic, giving each layer exactly what it needs.

## C++ Connections

### Evolution from struct to class

In C, a `struct` can only contain data members—no member functions, no access control, no inheritance. C++ retains the `struct` keyword but gives it nearly the same capabilities as `class`. The only difference is the default access specifier: members of `struct` are `public` by default, while members of `class` are `private` by default. Beyond that, a C++ `struct` can have constructors, destructors, member functions, inheritance, virtual functions—it can do anything.

```cpp
// C++ 中的 struct——可以有成员函数
struct SensorReading {
    uint32_t timestamp;
    float temperature;
    float humidity;

    // 成员函数
    bool is_overheating() const {
        return temperature > 85.0f;
    }

    void print() const {
        printf("T=%.1fC H=%.1f%%\n", temperature, humidity);
    }
};
```

So when you see `struct` in C++ code, do not assume it is the same as a C struct—it is simply a class with default public access.

### POD Types and Trivially Copyable

C++ has a specific concept for "simple structs compatible with C": POD types (Plain Old Data). Simply put, if a struct has no virtual functions, no non-trivial constructors or destructors, and all members are POD types, then it is itself a POD. POD types can be safely copied with `memcpy`, zeroed with `memset`, and safely binary-serialized and deserialized—because their memory layout is completely consistent with C.

After C++11, the POD concept was refined into several more precise type traits: `is_trivially_copyable`, `is_standard_layout`, and so on. Understanding these concepts is very important in cross-language interaction (C/C++ mixed compilation), binary serialization, and shared memory communication.

### std::aligned_storage

The C++ standard library provides `std::aligned_storage` (from C++11 onward, replaced by `alignas` in C++23), which is a type trait tool for manually controlling the alignment of a block of raw memory. It is used in advanced scenarios such as implementing type-erased containers, memory pools, and placement new:

```cpp
#include <type_traits>

// 分配一块 64 字节对齐的原始内存
alignas(64) std::byte storage[sizeof(MyStruct)];

// 或者使用 std::aligned_storage（C++23 前的做法）
using AlignedStorage = std::aligned_storage_t<sizeof(MyStruct), alignof(MyStruct)>;
```

These concepts will be discussed in detail in later C++ chapters. For now, you just need to know that the approach to alignment control in C is implemented more systematically and safely in C++.

## Summary

In this tutorial, we thoroughly broke down structs from "how to use them" to "what they look like in memory." Structs are the most core composite type in C, and understanding their memory layout—especially alignment and padding—is the foundation for writing efficient, correct, and portable code.

### Key Takeaways

- [ ] Structs are defined with `typedef struct { ... } Name;`, and members are accessed via `->` when using pointers
- [ ] C99 designated initializers (`.field = value`) are safer and more readable than sequential initialization
- [ ] The compiler inserts padding bytes between members and at the end of the struct to ensure each member is aligned
- [ ] Ordering fields from largest to smallest alignment requirement reduces padding and saves memory
- [ ] The `offsetof` macro can precisely verify the offset of each field
- [ ] C11's `alignas`/`alignof` provide standardized alignment control capabilities
- [ ] Flexible array members are used for variable-length trailing data and must be used via pointers and dynamic allocation
- [ ] `__attribute__((packed))` removes padding for binary protocol parsing, but incurs performance and portability costs
- [ ] C++'s `struct` is a `class` with default public access; POD types maintain a C-compatible memory layout

## Exercises

### Exercise: Design a Communication Protocol Frame with Manual Alignment Control

Please design a binary protocol frame structure for embedded device communication. The requirements are as follows:

1. The frame header contains a 1-byte start flag `0xAA`, a 1-byte frame type, a 2-byte payload length, and a 4-byte timestamp
2. The payload section is variable-length data (use a flexible array member)
3. The frame footer contains a 2-byte CRC16 checksum
4. Use `_Alignas` to ensure the timestamp field is 4-byte aligned
5. Use `__attribute__((packed))` to ensure the frame structure is compact (suitable for directly casting and parsing a byte stream)
6. Write a function that uses `offsetof` to print the offset of each field to verify the layout

```c
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// TODO: 定义 Frame 结构体
// typedef struct __attribute__((packed)) {
//     ...
// } Frame;

// TODO: 实现 print_frame_layout() 函数
// 使用 offsetof 打印每个字段的偏移量

// TODO: 实现 create_frame() 函数
// 分配内存并填充帧数据（含柔性数组成员）

int main(void) {
    print_frame_layout();

    // TODO: 创建一个测试帧并验证偏移
    return 0;
}
```

Hint: Using `alignas` inside a packed struct requires care—packed removes automatic padding, but `alignas` can force the alignment of a specific field. Think about this: in a packed struct, if the offset from the frame header to the timestamp is not a multiple of 4, how would you handle it?

## References

- [C struct - cppreference](https://en.cppreference.com/w/c/language/struct)
- [C11 alignas/alignof - cppreference](https://en.cppreference.com/w/c/language/alignment)
- [offsetof - cppreference](https://en.cppreference.com/w/c/types/offsetof)
- [Flexible array members - cppreference](https://en.cppreference.com/w/c/language/struct)
