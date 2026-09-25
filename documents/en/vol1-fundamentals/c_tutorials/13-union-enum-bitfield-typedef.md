---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the use of unions, enums, bit fields, and typedef, understand techniques such as type punning and hardware register mapping, and compare them with C++'s type-safe alternatives.
difficulty: beginner
order: 17
platform: host
prerequisites:
- Structures and Memory Alignment
reading_time_minutes: 22
tags:
- host
- cpp-modern
- beginner
- 入门
- 类型安全
title: Unions, Enums, Bit Fields, and typedef
translation:
  source: documents/vol1-fundamentals/c_tutorials/13-union-enum-bitfield-typedef.md
  source_hash: 8e5bf0c6542d732cc205f282ae371eac85ef30054ddb24e89179b7aeda9c9baa
  translated_at: '2026-09-25T13:21:49+00:00'
  engine: anthropic
  token_count: 3900
---
# Unions, Enums, Bit Fields, and typedef

Last time we took structures completely apart and settled the question of memory layout, including how the compiler sneaks padding bytes between your fields. This chapter looks at four language features — `union`, `enum`, bit fields, and `typedef` — that seem like supporting actors next to `struct`, yet each has an irreplaceable role to play. Unions let you perform conjuring tricks on the same block of memory, enums replace magic numbers with meaningful names, bit fields give you bit-level control over memory layout, and typedef lets you alias types and tidy up complicated declarations.

These four features are practically inseparable companions in embedded development. Open the header file of any MCU (STM32's `stm32f1xx.h`, for example) and you will find that register definitions are a combination of union + struct + bit field + typedef. Only once you understand them can you read that densely packed hardware abstraction layer code.

## Step 1 — Conjuring on the Same Block of Memory with Unions

### Understand the Union Memory Model

The definition syntax of a union is nearly identical to a struct; the only difference is that the keyword `struct` becomes `union`. Their memory behavior, however, is worlds apart: each member of a struct occupies its own independent memory, while all members of a union **share one block of memory that starts at the same address**. The size of a union equals the size of its largest member (possibly plus some alignment padding).

```c
#include <stdio.h>
#include <stdint.h>

typedef union {
    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;
} IntUnion;

int main(void) {
    printf("sizeof(IntUnion) = %zu\n", sizeof(IntUnion));  // 4
    return 0;
}
```

Output:

```text
sizeof(IntUnion) = 4
```

In the current GCC x86_64 environment, `IntUnion` is 4 bytes. The three members `u8`, `u16`, and `u32` all start at exactly the same address; once you write to one member, the others see the bytes in this overlapping storage too — not three independent copies of the data.

Only **one** member of a union is valid at any given moment. Writing to one member and then reading another is undefined behavior under the C standard (apart from the type-punning exception). You must keep track of which member is currently active yourself; the compiler will not check it for you.

### Inspect a Float's Binary Representation via Type Punning

Although the C standard says "writing to one member and then reading another is undefined behavior," there is one important exception: type punning through a union is **legal** in C99 and later. Type punning simply means reinterpreting the same block of memory as a different type:

```c
#include <stdio.h>
#include <stdint.h>

typedef union {
    float    f;
    uint32_t u;
} FloatBits;

int main(void) {
    FloatBits fb;
    fb.f = 3.14f;
    printf("float 值: %f\n", fb.f);        // 3.140000
    printf("二进制表示: 0x%08X\n", fb.u);  // 0x4048F5C3
    return 0;
}
```

Output:

```text
float 值: 3.140000
二进制表示: 0x4048F5C3
```

This is perfectly legal in C. Note, however, that it is **undefined behavior in C++** — the C++ standard does not allow type punning through a union. When you need something similar in C++ code, use `memcpy` (which the compiler optimizes away) or `std::bit_cast` (C++20) instead.

### Combine Unions and Structs into a Variant Type

The moment unions truly show their power is when combined with structs and enums. A union on its own is fairly useless — you have no idea which member it currently holds. But once you add a "tag" that records the current type, it becomes a meaningful variant type:

```c
#include <stdio.h>
#include <stdint.h>

typedef enum {
    kValueTypeInt,
    kValueTypeFloat,
    kValueTypeString
} ValueType;

typedef struct {
    ValueType tag;
    union {
        int32_t  int_val;
        float    float_val;
        const char* str_val;
    } data;
} TaggedValue;

void print_value(const TaggedValue* v) {
    switch (v->tag) {
        case kValueTypeInt:
            printf("int: %d\n", v->data.int_val);
            break;
        case kValueTypeFloat:
            printf("float: %f\n", v->data.float_val);
            break;
        case kValueTypeString:
            printf("string: %s\n", v->data.str_val);
            break;
    }
}
```

This "tag + union" pattern is called a **tagged union**, and it is the fundamental technique for implementing polymorphism in C.

## Step 2 — Giving Integers Names with Enums

### Understand What Enums Really Are

An enum lets you define a set of named integer constants, and the syntax is simple:

```c
typedef enum {
    kColorRed,
    kColorGreen,
    kColorBlue
} Color;

Color c = kColorGreen;
printf("%d\n", c);  // 1
```

Enumerators start at 0 and increase from there by default. You can also assign values explicitly:

```c
typedef enum {
    kStatusOk         = 0,
    kStatusError      = 1,
    kStatusTimeout    = 2,
    kStatusBusy       = 3,
    kStatusInvalidArg = 4
} StatusCode;
```

### Mind the Limitations of Enums

C enums have a property people love to hate: **enumerators are, at bottom, just ints**. This means you can assign any integer to an enum variable and the compiler will not raise an error:

```c
Color c = 42;          // Legal! But 42 is not any of the enumerators
int x = kColorRed;     // Legal! Implicitly converts to int
```

What C regards as "flexibility" here is a disaster from a type-safety standpoint — the compiler has no way to check for you whether "this value is a legal enumerator". This is the fundamental reason C++11 introduced `enum class`.

## Step 3 — Allocating Memory Bit by Bit with Bit Fields

### First, the Basic Syntax of Bit Fields

Bit fields let you allocate storage inside a struct with the **bit** as the unit. The syntax appends a colon and a bit count after the field name:

```c
typedef struct {
    uint32_t enable    : 1;   // 1 bit
    uint32_t mode      : 3;   // 3 bits (can represent 0-7)
    uint32_t priority  : 4;   // 4 bits (can represent 0-15)
    uint32_t reserved  : 24;  // 24 bits reserved
} ControlReg;  // 32 bits in total = 4 bytes
```

Bit-field members are accessed exactly like ordinary struct members:

```c
ControlReg reg = {0};
reg.enable   = 1;
reg.mode     = 5;
reg.priority = 3;
```

### Map Hardware Registers with Bit Fields

The most common application of bit fields in embedded development is mapping hardware registers:

```c
typedef struct {
    volatile uint32_t enable     : 1;   // bit 0: enable
    volatile uint32_t tickint    : 1;   // bit 1: interrupt enable
    volatile uint32_t clksource  : 1;   // bit 2: clock source selection
    volatile uint32_t reserved   : 13;  // bit 15:3 reserved
    volatile uint32_t countflag  : 1;   // bit 16: count flag
    volatile uint32_t reserved2  : 15;  // bit 31:17 reserved
} SysTickCtrl;

volatile SysTickCtrl* systick_ctrl = (volatile SysTickCtrl*)0xE000E010;
systick_ctrl->enable    = 1;
systick_ctrl->tickint   = 1;
systick_ctrl->clksource = 1;
```

### Mind the Portability Traps of Bit Fields

Bit fields are a joy to use, but they come at a cost you must face squarely: **poor portability**. The C standard leaves several key details of bit fields unspecified — the allocation order of the bits (low to high, or the other way around), plus the alignment and padding rules — all of it delegated to the compiler implementation.

When mapping hardware registers with bit fields, always use the standard headers provided by the compiler (the CMSIS headers for STM32, for instance) as your reference. The register structs in those headers are vendor-verified, and their bit-field allocation direction matches the platform. Hand-writing your own bit fields to map hardware registers is very likely to break across compilers.

### Bit Fields vs. Handwritten Bitwise Masks

Precisely because of this portability problem, many embedded projects avoid bit fields entirely and use handwritten bitwise masks instead:

```c
#define CTRL_ENABLE_MASK    (1U << 0)
#define CTRL_MODE_MASK      (0x7U << 1)

volatile uint32_t* ctrl_reg = (volatile uint32_t*)0xE000E010;
*ctrl_reg |= CTRL_ENABLE_MASK;
*ctrl_reg = (*ctrl_reg & ~CTRL_MODE_MASK) | (5U << 1);
```

Bitwise masks have the advantage of being fully portable and independent of compiler behavior; the drawback is poorer readability. In practice the two are often mixed.

## Step 4 — Giving Types Aliases with typedef

### First, the Basic Usage

typedef's core function is simple — creating a new name for an existing type:

```c
typedef uint32_t Timestamp;
typedef struct { float x; float y; } Point2D;

Timestamp now = 1700000000;
Point2D origin = {0.0f, 0.0f};
```

### Simplify Function Pointer Declarations

One of typedef's most practical uses is simplifying function pointer declarations:

```c
// Without typedef: declaring an array of 8 function pointers
void (*handlers[8])(int);

// With typedef: far clearer
typedef void (*EventHandler)(int);
EventHandler handlers[8];
```

### typedef vs. `#define`

typedef creates a **genuine type alias** handled by the compiler, whereas `#define` is mere textual substitution by the preprocessor:

```c
typedef char* CharPtr;
#define CHAR_PTR char*

CharPtr a, b;    // a and b are both char*
CHAR_PTR c, d;  // expands to char* c, d; — only c is char*, d is char!
```

A typedef name cannot be used for a forward declaration. The solution is to first write `typedef struct TagName TagName;` as the forward declaration, then provide the full definition later as `struct TagName { ... };`. This style is extremely common when implementing self-referential data structures such as linked lists and trees. Also, do not overuse typedef — a good typedef adds information (`Timestamp` says more than `uint32_t`) rather than simply hiding it.

## Bridging to C++

### enum class: Type-Safe Enums (C++11)

```cpp
enum class Color { kRed, kGreen, kBlue };
Color c = Color::kRed;       // the scope qualifier is mandatory
int x = c;                    // Compile error! No implicit conversion to int
int y = static_cast<int>(c);  // OK, but the conversion must be explicit
```

`enum class` can also specify an underlying type:

```cpp
enum class StatusCode : uint8_t { kOk = 0, kError = 1 };
static_assert(sizeof(StatusCode) == 1);
```

### std::variant: The Type-Safe Union (C++17)

```cpp
#include <variant>
using Value = std::variant<int, float, const char*>;

Value v1 = 42;
int x = std::get<int>(v1);    // OK
// float f = std::get<float>(v1);  // throws std::bad_variant_access
```

### Limiting union Usage in C++

If the members of a union have non-trivial constructors, destructors, or copy operations (`std::string`, for example), you must manage those members' lifetimes manually. So in C++, prefer `std::variant`.

### std::bitset: Replacing Handwritten Bit Fields

```cpp
#include <bitset>
std::bitset<32> ctrl_reg(0);
ctrl_reg[0] = 1;   // enable
bool enabled = ctrl_reg[0];
```

### using as a typedef Replacement (C++11)

```cpp
using EventHandler = void (*)(int);  // more intuitive than typedef
```

## Exercises

### Exercise 1: Decomposing an IEEE 754 Float

**Difficulty: Intermediate** · Splitting a float into its bits with a union

Use a union to build a tool that decomposes a `float` value into its IEEE 754 sign bit, exponent, and mantissa, and prints them out.

```c
#include <stdio.h>
#include <stdint.h>

// Exercise: define a union containing float and uint32_t
// Exercise: implement the decomposition function
// void print_float_bits(float f) {
//     // Extract the sign bit (1 bit), exponent (8 bits), and mantissa (23 bits)
//     // Hint: use the bitwise operators & and >>
// }

int main(void) {
    // Exercise: test several values: 0.0f, -3.14f, 1.0f, 42.0f, 0.1f
    return 0;
}
```

::: details Reference Solution

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef union {
    float f;
    uint32_t u;
} FloatBits;

// IEEE 754 floating-point bit layout for a float
// 1-bit sign |  8-bit exponent                     |  23-bit mantissa
// pos or neg | how large/small values can be (range) | how many significant digits (precision)
void print_float_bits(float f)
{
    FloatBits data;
    data.f = f;

    uint32_t sign = (data.u >> 31) & 0x1;
    uint32_t exponent = (data.u >> 23) & 0xff;
    uint32_t fraction = data.u & 0x7fffff;

    printf("float = %f\n", f);

    printf("bits     = ");
    for (int i = 31; i >= 0; --i)
    {
        printf("%u", (unsigned int)((data.u >> i) & 1u));
        if (i == 31 || i == 23)
        {
            printf(" ");
        }
    }
    printf("\n");

    printf("1 位符号     = %u\n", (unsigned int)sign);
    printf("8 位指数 = ");
    for (int i = 7; i >= 0; --i)
    {
        printf("%u", (unsigned int)((exponent >> i) & 1u));
    }
    printf("\n");

    printf("23 位尾数 = ");
    for (int i = 22; i >= 0; --i)
    {
        printf("%u", (unsigned int)((fraction >> i) & 1u));
    }
    printf("\n");
}

int main(void)
{
    char input[32] = {0};
    char trailing_character;
    float value = 0.0f;

    printf("请输入 float：");
    fflush(stdout);

    if (fgets(input, sizeof(input), stdin) == NULL)
    {
        fputs("读取输入失败。\n", stderr);
        return 1;
    }

    /* If the buffer did not capture the newline, check for unread characters to avoid silent truncation. */
    if (strchr(input, '\n') == NULL)
    {
        // Read the 32nd character of the input
        int next_character = getchar();

        if (next_character != '\n' && next_character != EOF)
        {
            while (next_character != '\n' && next_character != EOF)
            {
                next_character = getchar();
            }
            fputs("输入过长。\n", stderr);
            return 1;
        }
    }

    /* The second conversion item rejects input that still contains non-whitespace characters after the number. */
    if (sscanf(input, "%f %c", &value, &trailing_character) != 1)
    {
        printf("输入无效！\n");
        return 1;
    }

    print_float_bits(value);
    return 0;
}

```

Compile and run:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic float_bits.c -o float_bits && ./float_bits
```

On a GCC x86_64 environment with IEEE 754 binary32 support, entering `-3.14` produces:

```text
请输入 float：-3.14
float = -3.140000
bits     = 1 10000000 10010001111010111000011
1 位符号     = 1
8 位指数 = 10000000
23 位尾数 = 10010001111010111000011
```

`fgets` reads at most 31 characters into `char input[32]` and appends a `\0` at the end. When the input line exceeds 31 characters, the function still returns success, but the newline and the remaining content are still sitting in the input stream; without checking whether `next_character` is `\n` or `EOF`, even an overlong input would be judged a complete read.

:::

### Exercise 2: A 32-Bit Hardware Control Register

**Difficulty: Basic** · Mapping a register with bit fields plus a union

Define a 32-bit hardware control register struct with bit fields, then write functions that operate on it.

```c
#include <stdio.h>
#include <stdint.h>

// Exercise: define the ControlRegister bit-field struct
// Bit allocation:
//   bit 0:     enable (1 bit)
//   bit 1:     interrupt_enable (1 bit)
//   bit 2:     dma_enable (1 bit)
//   bit 5:3    mode (3 bits)
//   bit 9:6    speed (4 bits)
//   bit 31:10  reserved (22 bits)

typedef union {
    // Exercise: the bit-field struct view
    // Exercise: the uint32_t whole-value view
} ControlRegister;

// Exercise: implement void print_register(ControlRegister reg)
// Exercise: implement void set_mode(ControlRegister* reg, uint32_t mode)

int main(void) {
    ControlRegister reg = {0};
    // Exercise: test the various operations
    return 0;
}
```

::: details Reference Solution

```c
#include <stdio.h>
#include <stdint.h>

// Bit allocation:
//   bit 0:     enable (1 bit)
//   bit 1:     interrupt_enable (1 bit)
//   bit 2:     dma_enable (1 bit)
//   bit 5:3    mode (3 bits)
//   bit 9:6    speed (4 bits)
//   bit 31:10  reserved (22 bits)
typedef union {
    struct {
        uint32_t enable : 1;
        uint32_t interrupt_enable : 1;
        uint32_t dma_enable : 1;
        uint32_t mode : 3;
        uint32_t speed : 4;
        uint32_t reserved : 22;
    } bits;
    uint32_t value;
} ControlRegister;

void print_register(ControlRegister reg)
{
    printf("Register = 0x%08X\n", (unsigned int)reg.value);
    printf("  enable: %u\n", (unsigned int)reg.bits.enable);
    printf("  interrupt_enable: %u\n", (unsigned int)reg.bits.interrupt_enable);
    printf("  dma_enable: %u\n", (unsigned int)reg.bits.dma_enable);
    printf("  mode: %u\n", (unsigned int)reg.bits.mode);
    printf("  speed: %u\n", (unsigned int)reg.bits.speed);
}

void set_mode(ControlRegister *reg, uint32_t mode)
{
    if (reg == NULL) {
        return;
    }

    // mode occupies only 3 bits, so keep just the low 3 bits of the parameter.
    reg->bits.mode = mode & 0x7U;
}

int main(void)
{
    ControlRegister reg = {0};

    reg.bits.enable = 1U;
    reg.bits.interrupt_enable = 1U;
    reg.bits.dma_enable = 0U;
    reg.bits.speed = 10U;
    set_mode(&reg, 5U);

    print_register(reg);
    // 9:1001 (take the low 3 bits)
    set_mode(&reg, 9U); // 9 is truncated to the 3-bit value 1.
    print_register(reg);

    return 0;
}
```

In this example's GCC x86_64 layout, the fields are allocated from the low bits to the high bits in declaration order; viewed from the most significant bit of the 32-bit integer down, the layout is:

```text
Bit     31                  10 9       6 5       3   2   1   0
       +-----------------------+---------+-------+---+---+---+
Field  |     reserved (22)     | speed 4 | mode 3| D | I | E |
       +-----------------------+---------+-------+---+---+---+
Range           [31:10]            [9:6]    [5:3]  2    1   0
```

- `E` = `enable`, occupying bit 0
- `I` = `interrupt_enable`, occupying bit 1
- `D` = `dma_enable`, occupying bit 2

`value` and `bits` share the same 32 bits of storage, so `value` lets you observe these bits as one combined value. The actual allocation direction, alignment, and padding of bit fields are implementation-defined; the figure above describes only the GCC x86_64 layout verified for this example and must not be taken directly as a register protocol across compilers or target platforms.

Compile and run:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic bitfield_register.c -o bitfield_register && ./bitfield_register
```

Run on WSL Ubuntu 24.04 (GCC 13.3, x86_64):

```text
Register = 0x000002AB
  enable: 1
  interrupt_enable: 1
  dma_enable: 0
  mode: 5
  speed: 10
Register = 0x0000028B
  enable: 1
  interrupt_enable: 1
  dma_enable: 0
  mode: 1
  speed: 10
```

:::

### Exercise 3: A Simple Tagged Union

**Difficulty: Basic** · An enum plus a union plus a tag check

Use an enum and a union to implement a tagged union that can store an `int`, a `float`, or a string pointer.

```c
#include <stdio.h>
#include <stdint.h>

// Exercise: define the enum type tag
// Exercise: define the tagged union struct
// Exercise: implement the constructors make_int/make_float/make_string
// Exercise: implement the print_tagged_value function
// Exercise: implement the safe accessors get_as_int/get_as_float/get_as_string
//       (check whether the tag matches; if not, print an error message)

int main(void) {
    // Exercise: create values of the three types and print them
    // Exercise: try accessing with the wrong tag to verify the safety check
    return 0;
}
```

::: details Reference Solution

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* A type tag identifying the currently active member of the union. */
typedef enum {
    TAG_INT,
    TAG_FLOAT,
    TAG_STRING
} ValueTag;

/*
 * All members of the union share one block of storage.
 * When reading, the ValueTag decides which member to read — the one matching the last write.
 */
typedef union {
    int int_value;
    float float_value;
    const char *string_value;
} ValueData;

/* Binds the type tag to the actual data, forming a value that can hold several types. */
typedef struct {
    ValueTag tag;
    ValueData data;
} TaggedValue;

/* Constructs a tagged value holding an int. */
TaggedValue make_int(int value)
{
    TaggedValue result = { TAG_INT, { .int_value = value } };
    return result;
}

/* Constructs a tagged value holding a float. */
TaggedValue make_float(float value)
{
    TaggedValue result = { TAG_FLOAT, { .float_value = value } };
    return result;
}

/*
 * Constructs a tagged value holding a string pointer.
 * This function does not copy the string; the caller must guarantee the string stays valid for as long as it is used.
 */
TaggedValue make_string(const char *value)
{
    TaggedValue result = { TAG_STRING, { .string_value = value } };
    return result;
}

/* Prints the currently active member in the format matching its type tag. */
void print_tagged_value(const TaggedValue *value)
{
    /* Guard against dereferencing a NULL pointer passed by the caller. */
    if (value == NULL) {
        fprintf(stderr, "Error: value is NULL.\n");
        return;
    }

    switch (value->tag) {
    case TAG_INT:
        printf("int: %d\n", value->data.int_value);
        break;
    case TAG_FLOAT:
        printf("float: %.2f\n", value->data.float_value);
        break;
    case TAG_STRING:
        /* The argument for %s must not be a NULL pointer; this avoids undefined behavior. */
        if (value->data.string_value == NULL) {
            fprintf(stderr, "Error: string value is NULL.\n");
            return;
        }
        printf("string: %s\n", value->data.string_value);
        break;
    default:
        /* If the tag is corrupted or was not initialized as agreed, refuse to read the union member. */
        fprintf(stderr, "Error: unknown value tag.\n");
        break;
    }
}

/* Writes the output parameter and returns true only when the value actually holds an int. */
bool get_as_int(const TaggedValue *value, int *out)
{
    if ((value == NULL) || (out == NULL)) {
        fprintf(stderr, "Error: value or output is NULL.\n");
        return false;
    }

    if (value->tag != TAG_INT) {
        fprintf(stderr, "Error: value is not an int.\n");
        return false;
    }

    *out = value->data.int_value;
    return true;
}

/* Writes the output parameter and returns true only when the value actually holds a float. */
bool get_as_float(const TaggedValue *value, float *out)
{
    if ((value == NULL) || (out == NULL)) {
        fprintf(stderr, "Error: value or output is NULL.\n");
        return false;
    }

    if (value->tag != TAG_FLOAT) {
        fprintf(stderr, "Error: value is not a float.\n");
        return false;
    }

    *out = value->data.float_value;
    return true;
}

/* Writes the output parameter and returns true only when the value actually holds a string pointer. */
bool get_as_string(const TaggedValue *value, const char **out)
{
    if ((value == NULL) || (out == NULL)) {
        fprintf(stderr, "Error: value or output is NULL.\n");
        return false;
    }

    if (value->tag != TAG_STRING) {
        fprintf(stderr, "Error: value is not a string.\n");
        return false;
    }

    *out = value->data.string_value;
    return true;
}

int main(void)
{
    /* Create one tagged value of each of the three types. */
    TaggedValue int_value = make_int(42);
    TaggedValue float_value = make_float(3.14f);
    TaggedValue string_value = make_string("Hello, tagged union!");
    int int_result = 0;
    float float_result = 0.0f;
    const char *text = NULL;

    /* When printing, the type tag decides which member of the union to read. */
    print_tagged_value(&int_value);
    print_tagged_value(&float_value);
    print_tagged_value(&string_value);

    /* Read the data through the accessors whose types match. */
    if (get_as_int(&int_value, &int_result)) {
        printf("get_as_int: %d\n", int_result);
    }
    if (get_as_float(&float_value, &float_result)) {
        printf("get_as_float: %.2f\n", float_result);
    }
    if (get_as_string(&string_value, &text)) {
        printf("get_as_string: %s\n", text);
    }

    /* The two calls below demonstrate the error handling on a type mismatch. */
    (void)get_as_float(&int_value, &float_result);
    (void)get_as_string(&float_value, &text);

    return 0;
}

```

Compile and run:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic tagged_union.c -o tagged_union && ./tagged_union
```

Output:

```text
int: 42
float: 3.14
string: Hello, tagged union!
get_as_int: 42
get_as_float: 3.14
get_as_string: Hello, tagged union!
Error: value is not a float.
Error: value is not a string.
```

The `tag` and `data` of `TaggedValue` must be maintained together: after writing `data.float_value`, it may be read only while `tag == TAG_FLOAT`. `make_string` stores only the pointer and does not copy the string, so the caller must guarantee that the string's storage duration covers the entire time it is in use; the three `get_as_*` functions report the access result with a `bool` plus an output parameter, which keeps a legitimate `0`, `0.0f`, or `NULL` from being confused with an error.

:::

## References

- [cppreference: C union](https://en.cppreference.com/w/c/language/union), [C enumeration](https://en.cppreference.com/w/c/language/enum), and [C bit-field](https://en.cppreference.com/w/c/language/bit_field): a quick reference for the C language rules and implementation differences.
- [cppreference: `std::variant`](https://en.cppreference.com/w/cpp/utility/variant) (C++17), [`std::bitset`](https://en.cppreference.com/w/cpp/utility/bitset) (C++98), and [`std::bit_cast`](https://en.cppreference.com/w/cpp/numeric/bit_cast) (C++20): the corresponding tools.
