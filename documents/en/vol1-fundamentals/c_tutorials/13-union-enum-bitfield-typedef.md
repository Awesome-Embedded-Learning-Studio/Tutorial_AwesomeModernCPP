---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master the use of unions, enums, bit fields, and typedef, understand
  techniques like type punning and hardware register mapping, and compare them with
  C++'s type-safe alternatives.
difficulty: beginner
order: 17
platform: host
prerequisites:
- 12 结构体与内存对齐
reading_time_minutes: 11
tags:
- host
- cpp-modern
- beginner
- 入门
- 类型安全
title: Unions, Enums, Bit Fields, and Typedefs
translation:
  source: documents/vol1-fundamentals/c_tutorials/13-union-enum-bitfield-typedef.md
  source_hash: 6e4953c3a9b1086fbfca22324dcc15e10d01e47558380c048daf9de0ca019ecd
  translated_at: '2026-06-16T03:35:48.921052+00:00'
  engine: anthropic
  token_count: 2215
---
# Unions, Enums, Bit Fields, and typedef

In the previous post, we completely dissected the memory layout of structs and figured out how the compiler inserts padding bytes between your fields. In this post, we will look at four language features—unions, enums, bit-fields, and typedef—that seem like "supporting characters" to structs, but each has its own indispensable role. Unions let you play tricks on the same memory block, enums let you replace magic numbers with meaningful names, bit-fields allow you to control memory layout bit by bit, and typedef lets you create aliases for types and clean up complex declarations.

These four features are almost inseparable in embedded development. If you look at the header files of any MCU (like STM32's CMSIS headers), you will find that register definitions are a combination of unions + structs + bit-fields + typedef. Only by understanding them can you read those dense Hardware Abstraction Layer (HAL) codes.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Understand the memory sharing mechanism of unions and type punning techniques.
> - [ ] Master the definition, usage, and limitations of enums.
> - [ ] Use bit-fields to define compact hardware register structures.
> - [ ] Skillfully use typedef to simplify complex type declarations.
> - [ ] Combine these features to implement tagged unions and protocol frame parsing.
> - [ ] Understand the corresponding type-safe alternatives in C++.

## Environment Setup

All code in this post has been verified in the following environment:

- **Operating System**: Linux (Ubuntu 22.04+) / WSL2 / macOS
- **Compiler**: GCC 11+ (Confirm version via `gcc --version`)
- **Compiler Flags**: `-Wall -Wextra -std=c11` (Enable warnings, specify C11 standard)
- **Verification**: All code can be compiled and run directly

## Step 1 — Performing Magic on the Same Memory with Unions

### Understanding the Memory Model of Unions

The definition syntax of a union is almost identical to a struct, with the only difference being the keyword changing from `struct` to `union`. However, their memory behaviors are vastly different: each member of a struct occupies its own independent memory space, whereas all members of a union **share the exact same memory starting at the same address**. The size of a union is equal to the size of its largest member (possibly plus some alignment padding).

```c
#include <stdio.h>

// Define a union containing different types
union Data {
    int i;
    float f;
    char str[20];
};

int main() {
    union Data data;

    printf("sizeof(union Data) = %zu\n", sizeof(data));
    printf("Address of i: %p\n", (void*)&data.i);
    printf("Address of f: %p\n", (void*)&data.f);
    printf("Address of str: %p\n", (void*)&data.str);

    return 0;
}
```

Output:

```text
sizeof(union Data) = 20
Address of i: 0x7ffc12345678
Address of f: 0x7ffc12345678
Address of str: 0x7ffc12345678
```

The size of `union Data` is 20 bytes—determined by the largest member `str`. The starting addresses of `i`, `f`, and `str` are exactly the same; writing to one will overwrite the others.

> ⚠️ **Warning**: Only **one** member of a union is valid at any given moment. Writing to one member and then reading another is undefined behavior (UB) in the C standard (except for specific type punning exceptions). You must remember which member is currently active yourself; the compiler will not check this for you.

### Viewing the Binary Representation of Floats via Type Punning

Although the C standard states that "reading a member other than the last one written to is undefined behavior," there is an important exception: type punning through unions is **legal** in C99 and later. Type punning means interpreting the same memory block as different types:

```c
#include <stdio.h>

union FloatPunner {
    float f;
    unsigned int u; // Assuming 32-bit int
};

int main() {
    union FloatPunner pun;
    pun.f = 3.14159f;

    printf("Float value: %f\n", pun.f);
    printf("Hex representation: 0x%08x\n", pun.u);

    return 0;
}
```

Output:

```text
Float value: 3.141590
Hex representation: 0x40490fd8
```

This is completely legal in C. However, be aware that this is **Undefined Behavior in C++**—the C++ standard does not permit type punning through unions. If you need to do something similar in C++, you should use `memcpy` (which the compiler will optimize away) or `std::bit_cast` (C++20).

### Combining Unions and Structs to Implement Variant Types

The moment a union truly shines is when combined with structs and enums. A standalone union isn't very useful—because you don't know which member currently holds data. But if you add a "tag" to record the current type, it becomes a meaningful variant type:

```c
#include <stdio.h>
#include <string.h>

enum DataType { INT, FLOAT, STR };

struct Variant {
    enum DataType type;
    union {
        int i;
        float f;
        char str[20];
    } value;
};

void print_variant(struct Variant *v) {
    switch (v->type) {
        case INT:
            printf("Integer: %d\n", v->value.i);
            break;
        case FLOAT:
            printf("Float: %f\n", v->value.f);
            break;
        case STR:
            printf("String: %s\n", v->value.str);
            break;
    }
}

int main() {
    struct Variant v1;
    v1.type = INT;
    v1.value.i = 42;

    struct Variant v2;
    v2.type = STR;
    strcpy(v2.value.str, "Hello, World");

    print_variant(&v1);
    print_variant(&v2);

    return 0;
}
```

This combination of "tag + union" is called a **tagged union**, which is the basic technique for implementing polymorphism in C.

## Step 2 — Naming Integers with Enums

### Understanding the Nature of Enums

Enums allow you to define a set of named integer constants. The syntax is simple:

```c
enum Color {
    RED,
    GREEN,
    BLUE
};
```

Enum values increment starting from 0 by default. You can explicitly specify values:

```c
enum Status {
    OK = 0,
    ERROR = -1,
    PENDING = 1
};
```

### Beware of the Limitations of Enums

C language enums have a feature that is both loved and hated: **enum values are essentially `int`**. This means you can assign any integer to an enum variable, and the compiler won't complain:

```c
enum Color c = 123; // Legal in C, but 123 is not a valid Color!
```

This looseness is considered "flexibility" in C, but from a type safety perspective, it's a disaster—the compiler has no way to check "is this value a valid enum value?". This is the fundamental reason why C++ introduced `enum class`.

## Step 3 — Allocating Memory Bit by Bit with Bit Fields

### Basic Syntax of Bit Fields

Bit fields allow you to allocate storage space in a struct in units of **bits**. The syntax involves adding a colon and the number of bits after the field name:

```c
struct Flags {
    unsigned int flag1 : 1;
    unsigned int flag2 : 1;
    unsigned int mode  : 2;
    unsigned int reserved : 4;
};
```

Accessing bit field members is exactly the same as normal structs:

```c
struct Flags f;
f.flag1 = 1;
f.mode = 0b11;
```

### Mapping Hardware Registers with Bit Fields

The most common application of bit fields in embedded development is mapping hardware registers:

```c
typedef struct {
    uint32_t CR1; // Control Register 1
    uint32_t CR2; // Control Register 2
    uint32_t SR;  // Status Register
} USART_TypeDef;

// Or using bit fields (compiler-dependent!)
typedef struct {
    uint32_t UE : 1;      // USART Enable
    uint32_t UESM : 1;    // USART Enable in Stop Mode
    uint32_t RE : 1;      // Receiver Enable
    uint32_t TE : 1;      // Transmitter Enable
    uint32_t : 28;        // Reserved
} USART_CR1_Bits;
```

### Beware of Portability Traps with Bit Fields

Bit fields are pleasant to use, but they come with a cost you must face: **poor portability**. The C standard leaves several key details of bit fields unspecified—allocation order (low-to-high or vice-versa), alignment, and padding rules are all left to the compiler implementation.

> ⚠️ **Warning**: When using bit fields to map hardware registers, always refer to the standard headers provided by the compiler (like STM32's CMSIS headers). The register structures in those headers are verified by the vendor, and the bit field allocation direction matches the platform. Manually writing bit fields to map hardware registers is likely to cause issues across different compilers.

### Bit Fields vs. Manual Bitmasking

Because of the portability issues of bit fields, many embedded projects avoid them entirely in favor of hand-written bitwise operation masks:

```c
// Manual masking
#define REG_CR1_UE_POS  0
#define REG_CR1_UE_MASK (1U << REG_CR1_UE_POS)

// Enable
*reg_ptr |= REG_CR1_UE_MASK;

// Disable
*reg_ptr &= ~REG_CR1_UE_MASK;
```

The advantage of bitwise masks is complete portability and independence from compiler behavior, but the disadvantage is poor code readability. In practice, the two are often mixed.

## Step 4 — Aliasing Types with typedef

### Basic Usage

The core function of typedef is simple—create a new name for an existing type:

```c
typedef unsigned int uint32_t; // Standard style
typedef struct Node Node;      // Simplify struct names
```

### Simplifying Function Pointer Declarations

One of the most practical scenarios for typedef is simplifying function pointer declarations:

```c
// Original declaration
void (*signal_handler)(int signo);

// Using typedef
typedef void (*SignalHandler)(int signo);

SignalHandler old_handler; // Much cleaner
```

### Difference Between typedef and `#define`

typedef creates a **true type alias** processed by the compiler; whereas `#define` is just preprocessor text replacement:

```c
typedef int* IntPtr;
#define INT_PTR int*

IntPtr p1, p2; // p1 and p2 are both int*
INT_PTR p3, p4; // p3 is int*, p4 is int!
```

> ⚠️ **Warning**: typedef names cannot be used in forward declarations directly in some contexts (though `typedef struct X X;` works). The solution is to write `struct X;` for the forward declaration first, then use `typedef struct X X;` in the full definition later. This pattern is very common when implementing self-referencing data structures like linked lists or trees. Also, don't overuse typedef—a good typedef should add information (e.g., `DeviceStatus` is more meaningful than `int`), not just hide it.

## C++ Transition

### enum class: Type-Safe Enums (C++11)

```cpp
enum class Color { Red, Green, Blue };

Color c = Color::Red;
// int x = Color::Red; // Error! No implicit conversion
```

`enum class` can also specify the underlying type:

```cpp
enum class Status : uint8_t { Ok = 0, Error = 1 };
```

### std::variant: Type-Safe Unions (C++17)

```cpp
#include <variant>
std::variant<int, float, std::string> v;
v = 42;
if (std::holds_alternative<int>(v)) {
    int x = std::get<int>(v);
}
```

### Restricting Union Usage in C++

If a union member has non-trivial constructors, destructors, or copy operations (like `std::string`), you must manually manage the lifecycle of these members. Therefore, in C++, prioritize using `std::variant`.

### std::bitset: Replacing Manual Bit Fields

```cpp
#include <bitset>
std::bitset<8> flags;
flags.set(1);
```

### using instead of typedef (C++11)

```cpp
using IntPtr = int*;
template<typename T>
using Vec = std::vector<T>; // Typedef can't do this easily
```

## Summary

In this post, we covered four C language features—unions, enums, bit-fields, and typedef—as well as their modern alternatives in C++. These four features share a common theme: they are typical cases where C language chooses "flexibility" over "safety". C++'s improvement approach is very clear: `enum class` constrains enums, `std::variant` automatically manages the active member of unions, `std::bitset` provides portable bit set operations, and `using` provides a more intuitive alias syntax.

## Exercises

### Exercise 1: IEEE 754 Float Decomposition

Use a union to implement a tool that decomposes a `float` value into IEEE 754 format sign bit, exponent, and mantissa, and prints them out.

```c
#include <stdio.h>
#include <stdint.h>

// TODO: Define the union and implement the logic
```

### Exercise 2: 32-bit Hardware Control Register

Use bit fields to define a 32-bit hardware control register struct, then write functions to manipulate it.

```c
// TODO: Define the struct and functions
```

### Exercise 3: Simple Tagged Union

Use an enum and a union to implement a tagged union that can store an `int`, a `float`, or a string pointer.

```c
// TODO: Implement the tagged union
```
