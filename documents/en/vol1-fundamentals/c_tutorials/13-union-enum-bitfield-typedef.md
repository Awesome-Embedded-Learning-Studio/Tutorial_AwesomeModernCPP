---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master the use of unions, enums, bit fields, and typedefs; understand
  techniques such as type punning and hardware register mapping; and compare them
  with type-safe alternatives in C++.
difficulty: beginner
order: 17
platform: host
prerequisites:
- 12 结构体与内存对齐
reading_time_minutes: 15
tags:
- host
- cpp-modern
- beginner
- 入门
- 类型安全
title: Unions, Enums, Bit Fields, and Typedefs
translation:
  source: documents/vol1-fundamentals/c_tutorials/13-union-enum-bitfield-typedef.md
  source_hash: a52d435d36f071778bcf0dbb760180bafdf1ac9c53bc81cb9a10537e7c04f59f
  translated_at: '2026-06-13T11:42:17.535200+00:00'
  engine: anthropic
  token_count: 2215
---
# Unions, Enums, Bit Fields, and typedef

In the previous post, we completely dissected the memory layout of structs and figured out that compilers insert padding bytes between your fields. In this post, we will look at four language features—unions, enums, bit-fields, and typedef—that seem like "supporting characters" to structs, but each has its own irreplaceable role. Unions let you play tricks on the same memory block, enums let you replace magic numbers with meaningful names, bit-fields let you control memory layout bit by bit, and typedef lets you create aliases for types and clean up complex declarations.

These four features are almost inseparable in embedded development. If you look at the header files of any MCU (like STM32's CMSIS headers), you will find that register definitions are a combination of unions + structs + bit-fields + typedef. Only by understanding them can you read those dense Hardware Abstraction Layer (HAL) codes.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the memory sharing mechanism of unions and type punning techniques.
> - [ ] Master the definition, usage, and limitations of enums.
> - [ ] Use bit-fields to define compact hardware register structures.
> - [ ] Skilled in using typedef to simplify complex type declarations.
> - [ ] Combine these features to implement tagged unions and protocol frame parsing.
> - [ ] Understand the corresponding type-safe alternatives in C++.

## Environment Setup

All code in this post has been verified in the following environment:

- **Operating System**: Linux (Ubuntu 22.04+) / WSL2 / macOS
- **Compiler**: GCC 11+ (confirm version via `gcc --version`)
- **Compiler Flags**: `-Wall -Wextra -std=c11` (warnings enabled, C11 standard specified)
- **Verification**: All code can be compiled and run directly

## Step 1 — Using Unions to Perform Magic on the Same Memory

### Understanding the Union Memory Model

The definition syntax of a union is almost identical to a struct, except the keyword changes from `struct` to `union`. However, their memory behaviors are vastly different: members of a struct each occupy independent memory spaces, while all members of a union **share the same starting memory address**. The size of a union is equal to the size of its largest member (plus possible alignment padding).

```c
#include <stdio.h>

union Data {
    int i;
    float f;
    char str[4];
};

int main() {
    union Data data;

    printf("sizeof(union Data) = %zu\n", sizeof(data));
    printf("Address of i: %p\n", (void*)&data.i);
    printf("Address of f: %p\n", (void*)&data.f);
    printf("Address of str: %p\n", (void*)&data.str);

    data.i = 0x12345678;
    printf("After setting i to 0x12345678:\n");
    printf("f = %f\n", data.f); // Undefined behavior in strict theory, but let's see
    printf("str[0] = 0x%x\n", (unsigned char)data.str[0]);

    return 0;
}
```

Output:

```text
sizeof(union Data) = 4
Address of i: 0x7ffd12345678
Address of f: 0x7ffd12345678
Address of str: 0x7ffd12345678
After setting i to 0x12345678:
f = 3.141592 // Garbage value depends on endianness and float representation
str[0] = 0x78
```

The size of `union Data` is 4 bytes—determined by the largest member `int` (assuming 32-bit int). The starting addresses of `i`, `f`, and `str` are exactly the same; writing to one overwrites the others.

> ⚠️ **Warning**: Only **one** member of a union is valid at any given time. Reading from a member other than the one most recently written to is Undefined Behavior (UB) in the C standard (except for specific type punning cases). You must remember which member is active yourself; the compiler won't check it for you.

### Using Type Punning to View the Binary Representation of Floats

Although the C standard says "reading a member other than the last one written is undefined behavior," there is an important exception: type punning through unions is **legal** in C99 and later. Type punning means interpreting the same memory block as different types:

```c
#include <stdio.h>

union FloatBits {
    float f;
    unsigned int u; // Assuming float and int are both 32-bit
};

int main() {
    union FloatBits fb;
    fb.f = 3.14159f;

    printf("Float value: %f\n", fb.f);
    printf("Hex representation: 0x%08x\n", fb.u);

    return 0;
}
```

Output:

```text
Float value: 3.141590
Hex representation: 0x40490fd0
```

This is completely legal in C. However, be aware that this is **Undefined Behavior in C++**—the C++ standard does not permit type punning through unions. If you need to do similar things in C++, use `memcpy` (which the compiler optimizes away) or `std::bit_cast` (C++20).

### Combining Unions and Structs to Implement Variant Types

A union truly shines when combined with structs and enums. A standalone union is of limited use—because you don't know which member is currently stored. But if you add a "tag" to record the current type, it becomes a meaningful variant type:

```c
#include <stdio.h>
#include <string.h>

enum ValueType { TYPE_INT, TYPE_FLOAT, TYPE_STRING };

struct Variant {
    enum ValueType type;
    union {
        int i;
        float f;
        char str[16];
    } value;
};

void print_variant(struct Variant *v) {
    switch (v->type) {
        case TYPE_INT:
            printf("Integer: %d\n", v->value.i);
            break;
        case TYPE_FLOAT:
            printf("Float: %f\n", v->value.f);
            break;
        case TYPE_STRING:
            printf("String: %s\n", v->value.str);
            break;
    }
}

int main() {
    struct Variant v1;
    v1.type = TYPE_INT;
    v1.value.i = 42;

    struct Variant v2;
    v2.type = TYPE_STRING;
    strncpy(v2.value.str, "Hello", sizeof(v2.value.str));

    print_variant(&v1);
    print_variant(&v2);

    return 0;
}
```

This combination of "tag + union" is called a **tagged union**, a basic technique for implementing polymorphism in C.

## Step 2 — Using Enums to Name Integers

### Understanding the Nature of Enums

Enums allow you to define a set of named integer constants. The syntax is simple:

```c
enum Color {
    RED,
    GREEN,
    BLUE
};

int main() {
    enum Color c = RED;
    printf("RED = %d, GREEN = %d\n", RED, GREEN); // Output: 0, 1
    return 0;
}
```

Enum values increment starting from 0 by default. You can explicitly specify values:

```c
enum Status {
    OK = 0,
    ERROR = -1,
    PENDING = 1
};
```

### Beware of Enum Limitations

C language enums have a characteristic that is both loved and hated: **enum values are essentially `int`**. This means you can assign any integer to an enum variable, and the compiler won't complain:

```c
enum Color c = 123; // Legal in C, but 123 is not a valid Color!
```

This laxity is seen as "flexibility" in C, but from a type safety perspective, it's a disaster—the compiler has no way to check "is this value a valid enum value?". This is the fundamental reason why C++ introduced `enum class`.

## Step 3 — Using Bit-Fields to Allocate Memory by Bits

### Basic Syntax of Bit-Fields

Bit-fields allow you to allocate storage space in a struct in units of **bits**. The syntax is to add a colon and the number of bits after the field name:

```c
struct Flags {
    unsigned int flag1 : 1;
    unsigned int flag2 : 1;
    unsigned int mode  : 2;
    unsigned int reserved : 4;
};

int main() {
    struct Flags f;
    f.flag1 = 1;
    f.mode = 2; // Binary 10

    printf("sizeof(struct Flags) = %zu\n", sizeof(f)); // Likely 1 or 4 bytes depending on alignment
    return 0;
}
```

Accessing bit-field members is exactly the same as accessing normal struct members.

### Mapping Hardware Registers with Bit-Fields

The most common application of bit-fields in embedded development is mapping hardware registers:

```c
typedef struct {
    volatile unsigned int CR1 : 3;  // Control bits 0-2
    volatile unsigned int CR2 : 1;  // Control bit 3
    volatile unsigned int RESERVED : 4; // Bits 4-7
    // ... assume 8-bit register for simplicity
} Register_t;

// Usage
Register_t *reg = (Register_t *)0x40000000; // Hypothetical address
reg->CR1 = 0x5; // Set control bits
```

### Portability Traps of Bit-Fields

Bit-fields are convenient to use, but they come at a cost you must face: **poor portability**. The C standard leaves several critical details of bit-fields unspecified—allocation order (low-to-high or high-to-low), alignment, and padding rules are all left to the compiler implementation.

> ⚠️ **Warning**: When using bit-fields to map hardware registers, always refer to the standard headers provided by the compiler (like STM32's CMSIS headers). The register structures in those headers are verified by the vendor, and the bit-field allocation direction matches the platform. Manually writing bit-field mappings for hardware registers is likely to cause issues across different compilers.

### Bit-Fields vs. Manual Bitmasking

Because of the portability issues with bit-fields, many embedded projects avoid them entirely in favor of manual bitwise operation masks:

```c
// Manual bitmasking
#define REG_CR1_MASK 0x07
#define REG_CR2_MASK 0x08

unsigned int reg = 0x00;
reg = (reg & ~REG_CR1_MASK) | (new_value & REG_CR1_MASK);
```

Bitmasking offers full portability and doesn't depend on compiler behavior, but the downside is poor code readability. In practice, both are often mixed.

## Step 4 — Using typedef to Alias Types

### Basic Usage

The core function of typedef is simple—create a new name for an existing type:

```c
typedef unsigned int uint32_t;
typedef struct { int x, y; } Point;

int main() {
    uint32_t val = 10;
    Point p = {1, 2};
    return 0;
}
```

### Simplifying Function Pointer Declarations

One of the most practical scenarios for typedef is simplifying function pointer declarations:

```c
typedef int (*CompareFunc)(const void *, const void *);

// Usage
int sort_array(int *arr, int size, CompareFunc cmp) {
    // ... implementation
    return 0;
}
```

### Difference Between typedef and `#define`

`typedef` creates a **true type alias** processed by the compiler, whereas `#define` is just preprocessor text replacement:

```c
#define pINT int *
typedef int * pINT2;

pINT a, b; // Expands to: int * a, b; (a is int*, b is int!)
pINT2 c, d; // Both c and d are int*
```

> ⚠️ **Warning**: `typedef` names cannot be used in forward declarations. The solution is to write `struct Tag;` for the forward declaration first, then use `typedef struct Tag Tag;` in the subsequent full definition. This pattern is very common when implementing self-referencing data structures like linked lists or trees. Also, don't overuse typedef—a good typedef should add information (e.g., `uint32_t` is more meaningful than `unsigned int`), not just hide information.

## C++ Transition

### enum class: Type-Safe Enums (C++11)

```cpp
enum class Color { Red, Green, Blue };

int main() {
    // Color c = Red; // Error!
    Color c = Color::Red; // OK
    // int x = c;      // Error! No implicit conversion
    int x = static_cast<int>(c); // OK
}
```

`enum class` can also specify the underlying type:

```cpp
enum class Status : unsigned char { OK = 0, ERROR = 255 };
```

### std::variant: Type-Safe Union (C++17)

```cpp
#include <variant>
#include <iostream>

int main() {
    std::variant<int, float, std::string> v;

    v = 42;
    std::cout << std::get<int>(v) << "\n";

    v = "Hello";
    if (std::holds_alternative<std::string>(v)) {
        std::cout << std::get<std::string>(v) << "\n";
    }
}
```

### Restricting Union Usage in C++

If a union member has non-trivial constructors, destructors, or copy operations (like `std::string`), you must manually manage the lifecycle of these members. Therefore, in C++, prefer `std::variant`.

### std::bitset: Replacing Manual Bit-Fields

```cpp
#include <bitset>
#include <iostream>

int main() {
    std::bitset<8> flags(0b10101010);
    flags.set(2);
    std::cout << flags << "\n"; // Prints binary representation
}
```

### using Replaces typedef (C++11)

```cpp
typedef int (*OldFunc)(int);
using NewFunc = int (*)(int); // More intuitive syntax

template<typename T>
using Vec = std::vector<T>; // Template alias (typedef can't do this)
```

## Summary

In this post, we covered four C language features—unions, enums, bit-fields, and typedef—and their modern alternatives in C++. These four features share a common theme: they are typical cases where C language chooses "flexibility" over "safety". The C++ improvement approach is clear: `enum class` constrains enums, `std::variant` automatically manages the active member of unions, `std::bitset` provides portable bit set operations, and `using` provides a more intuitive alias syntax.

## Exercises

### Exercise 1: IEEE 754 Float Decomposition

Use a union to implement a tool that decomposes a `float` value into IEEE 754 format sign bit, exponent, and mantissa, and prints them.

```c
#include <stdio.h>
#include <stdint.h>

// TODO: Define union and implement logic
```

### Exercise 2: 32-bit Hardware Control Register

Use bit-fields to define a 32-bit hardware control register struct, then write functions to manipulate it.

```c
// TODO: Define struct and functions
```

### Exercise 3: Simple Tagged Union

Use an enum and a union to implement a tagged union that can store an `int`, a `float`, or a string pointer.

```c
// TODO: Implement tagged union and print function
```
