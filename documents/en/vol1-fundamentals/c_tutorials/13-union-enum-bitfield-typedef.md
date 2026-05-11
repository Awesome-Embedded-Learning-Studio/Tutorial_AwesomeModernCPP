---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master the use of unions, enums, bit fields, and typedef, understand
  techniques like type punning and hardware register mapping, and compare C++'s type-safe
  alternatives.
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
title: Unions, Enums, Bit Fields, and typedef
translation:
  source: documents/vol1-fundamentals/c_tutorials/13-union-enum-bitfield-typedef.md
  source_hash: a2e0b303d0c420a8ba0fe3fe86c928ed23c42cfa0d5b15619a59e239023c3a63
  translated_at: '2026-04-20T03:31:53.906452+00:00'
  engine: anthropic
  token_count: 2223
---
# Unions, Enums, Bit-Fields, and typedef

In the previous chapter, we completely dissected the memory layout of structs and figured out that compilers insert padding bytes between your fields. In this chapter, we look at four language features—unions, enums, bit-fields, and typedef—that might seem like "supporting actors" to structs, but each has an irreplaceable role to play. Unions let you perform tricks on the same block of memory, enums let you replace magic numbers with meaningful names, bit-fields let you precisely control memory layout down to the bit, and typedef lets you create aliases for types and clean up complex declarations.

These four features are almost inseparable in embedded development. If you look at the header files of any MCU (like STM32's `stm32f1xx.h`), you will find that register definitions are a combination of unions, structs, bit-fields, and typedef. Only by understanding them can you read those dense HAL (Hardware Abstraction Layer) code bases.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Understand the memory sharing mechanism of unions and type punning techniques
> - [ ] Master the definition, usage, and limitations of enums
> - [ ] Use bit-fields to define compact hardware register structures
> - [ ] Be proficient in using typedef to simplify complex type declarations
> - [ ] Combine these features to implement a tagged union and parse protocol frames
> - [ ] Understand the corresponding type-safe alternatives in C++

## Environment Setup

All code in this chapter has been verified under the following environment:

- **Operating System**: Linux (Ubuntu 22.04+) / WSL2 / macOS
- **Compiler**: GCC 11+ (confirm version via `gcc --version`)
- **Compiler flags**: `gcc -Wall -Wextra -std=c11` (enable warnings, specify C11 standard)
- **Verification**: All code can be directly compiled and run

## Step 1 — Performing Memory Tricks with Unions

### Understanding the Union Memory Model

The definition syntax of a union is almost identical to that of a struct; the only difference is the keyword changing from `struct` to `union`. However, their memory behaviors are worlds apart: each member of a struct occupies its own independent memory space, whereas all members of a union **share the exact same starting memory address**. The size of a union is equal to the size of its largest member (possibly plus some alignment padding).

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

The size of `IntUnion` is 4 bytes—determined by the largest member, `uint32_t`. The starting addresses of the three members `u8`, `u16`, and `u32` are exactly the same; writing to one will overwrite the others.

> ⚠️ **Pitfall Warning**: Only **one** member of a union is valid at any given time. Writing to one member and then reading another is undefined behavior (UB) in the C standard (except for the type punning exception). You must keep track of which member is currently active yourself; the compiler will not check this for you.

### Using Type Punning to View the Binary Representation of a Float

Although the C standard states that "reading a member other than the one last written is undefined behavior," there is an important exception: type punning via unions is **legal** in C99 and later. Type punning simply means interpreting the same block of memory as a different type:

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

This is perfectly legal in C. However, note that this is **undefined behavior (UB) in C++**—the C++ standard does not allow type punning through unions. If you need to do something similar in C++ code, you should use `memcpy` (which the compiler will optimize away) or `std::bit_cast` (C++20).

### Combining Unions and Structs to Implement a Variant Type

A union truly shines when combined with structs and enums. A union on its own isn't very useful—because you don't know which member is currently stored. But if you add a "tag" to record the current type, it becomes a meaningful variant type:

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

This "tag + union" combination pattern is called a **tagged union**, and it is the fundamental technique for implementing polymorphism in C.

## Step 2 — Naming Integers with Enums

### Understanding the Essence of Enums

Enums let you define a set of named integer constants. The syntax is straightforward:

```c
typedef enum {
    kColorRed,
    kColorGreen,
    kColorBlue
} Color;

Color c = kColorGreen;
printf("%d\n", c);  // 1
```

Enum values increment from 0 by default. You can explicitly specify values:

```c
typedef enum {
    kStatusOk         = 0,
    kStatusError      = 1,
    kStatusTimeout    = 2,
    kStatusBusy       = 3,
    kStatusInvalidArg = 4
} StatusCode;
```

### Beware of Enum Limitations

C enums have a characteristic that inspires both love and hate: **enum values are essentially ints**. This means you can assign any integer to an enum variable, and the compiler will not throw an error:

```c
Color c = 42;          // 合法！但 42 不是任何枚举值
int x = kColorRed;     // 合法！隐式转为 int
```

This leniency is considered "flexibility" in C, but from a type safety perspective, it is a disaster—the compiler has no way to help you check "is this value a valid enum value?" This is the fundamental reason C++ introduced `enum class`.

## Step 3 — Allocating Memory Bit by Bit with Bit-Fields

### Basic Syntax of Bit-Fields

Bit-fields allow you to allocate storage space in a struct in units of **bits**. The syntax is to add a colon and the number of bits after the field name:

```c
typedef struct {
    uint32_t enable    : 1;   // 1 位
    uint32_t mode      : 3;   // 3 位（可表示 0-7）
    uint32_t priority  : 4;   // 4 位（可表示 0-15）
    uint32_t reserved  : 24;  // 24 位保留
} ControlReg;  // 总计 32 位 = 4 字节
```

Accessing bit-field members is exactly the same as accessing normal struct members:

```c
ControlReg reg = {0};
reg.enable   = 1;
reg.mode     = 5;
reg.priority = 3;
```

### Mapping Hardware Registers with Bit-Fields

The most common application of bit-fields in embedded development is mapping hardware registers:

```c
typedef struct {
    volatile uint32_t enable     : 1;   // bit 0: 使能
    volatile uint32_t tickint    : 1;   // bit 1: 中断使能
    volatile uint32_t clksource  : 1;   // bit 2: 时钟源选择
    volatile uint32_t reserved   : 13;  // bit 15:3 保留
    volatile uint32_t countflag  : 1;   // bit 16: 计数标志
    volatile uint32_t reserved2  : 15;  // bit 31:17 保留
} SysTickCtrl;

volatile SysTickCtrl* systick_ctrl = (volatile SysTickCtrl*)0xE000E010;
systick_ctrl->enable    = 1;
systick_ctrl->tickint   = 1;
systick_ctrl->clksource = 1;
```

### Beware of Bit-Field Portability Traps

Bit-fields are satisfying to use, but they come with a cost you must face: **poor portability**. The C standard leaves several critical details unspecified—the allocation order of bit-fields (low-to-high or vice versa), alignment, and padding rules. All of these are left to the compiler implementation.

> ⚠️ **Pitfall Warning**: When using bit-fields to map hardware registers, always use the standard headers provided by the compiler (like STM32's CMSIS headers) as a reference. The register structs in those headers are verified by the vendor, and the bit-field allocation direction matches the platform. Hand-writing bit-field mappings for hardware registers is very likely to cause issues across different compilers.

### Bit-Fields vs. Hand-Written Bitmask Operations

Because of the portability issues with bit-fields, many embedded projects avoid them entirely, opting instead for hand-written bitmask operations:

```c
#define CTRL_ENABLE_MASK    (1U << 0)
#define CTRL_MODE_MASK      (0x7U << 1)

volatile uint32_t* ctrl_reg = (volatile uint32_t*)0xE000E010;
*ctrl_reg |= CTRL_ENABLE_MASK;
*ctrl_reg = (*ctrl_reg & ~CTRL_MODE_MASK) | (5U << 1);
```

The advantage of bitmask operations is complete portability and independence from compiler behavior, while the downside is poor code readability. In practice, the two are often mixed.

## Step 4 — Creating Type Aliases with typedef

### Basic Usage

The core function of typedef is simple—creating a new name for an existing type:

```c
typedef uint32_t Timestamp;
typedef struct { float x; float y; } Point2D;

Timestamp now = 1700000000;
Point2D origin = {0.0f, 0.0f};
```

### Simplifying Function Pointer Declarations

One of the most practical scenarios for typedef is simplifying function pointer declarations:

```c
// 不用 typedef：声明一个包含 8 个函数指针的数组
void (*handlers[8])(int);

// 用 typedef：清晰得多
typedef void (*EventHandler)(int);
EventHandler handlers[8];
```

### The Difference Between typedef and `#define`

typedef creates a **true type alias** handled by the compiler, whereas `#define` is merely a preprocessor text replacement:

```c
typedef char* CharPtr;
#define CHAR_PTR char*

CharPtr a, b;    // a 和 b 都是 char*
CHAR_PTR c, d;  // 展开后是 char* c, d; — 只有 c 是 char*，d 是 char！
```

> ⚠️ **Pitfall Warning**: A typedef name cannot be used in a forward declaration. The solution is to first write `typedef struct TagName TagName;` for the forward declaration, and then use `struct TagName { ... };` in the full definition later. This pattern is very common when implementing self-referential data structures like linked lists and trees. Additionally, do not overuse typedef—a good typedef should add information (e.g., `Timestamp` is more meaningful than `uint32_t`), rather than simply hiding information.

## C++ Connections

### enum class: Type-Safe Enums (C++11)

```cpp
enum class Color { kRed, kGreen, kBlue };
Color c = Color::kRed;       // 必须加作用域限定
int x = c;                    // 编译错误！不能隐式转 int
int y = static_cast<int>(c);  // OK，必须显式转换
```

`enum class` can also specify the underlying type:

```cpp
enum class StatusCode : uint8_t { kOk = 0, kError = 1 };
static_assert(sizeof(StatusCode) == 1);
```

### std::variant: Type-Safe Unions (C++17)

```cpp
#include <variant>
using Value = std::variant<int, float, const char*>;

Value v1 = 42;
int x = std::get<int>(v1);    // OK
// float f = std::get<float>(v1);  // 抛出 std::bad_variant_access
```

### Restricting union Usage in C++

If a union member has a non-trivial constructor, destructor, or copy operation (like `std::string`), you must manually manage the lifetime of these members. Therefore, in C++, prefer using `std::variant`.

### std::bitset: Replacing Manual Bit-Fields

```cpp
#include <bitset>
std::bitset<32> ctrl_reg(0);
ctrl_reg[0] = 1;   // enable
bool enabled = ctrl_reg[0];
```

### using as a Replacement for typedef (C++11)

```cpp
using EventHandler = void (*)(int);  // 比 typedef 更直观
```

## Summary

In this chapter, we covered four C language features in one breath—unions, enums, bit-fields, and typedef—along with their modern alternatives in C++. These four features share a common theme: they are all classic cases where C chooses "flexibility" over "safety." C++'s improvement approach is very clear: `enum class` constrains enums, `std::variant` automatically manages the active member of a union, `std::bitset` provides portable bit-set operations, and `using` provides a more intuitive alias syntax.

## Exercises

### Exercise 1: IEEE 754 Float Decomposition

Use a union to implement a tool that decomposes a `float` value into its IEEE 754 sign bit, exponent, and mantissa, and prints them out.

```c
#include <stdio.h>
#include <stdint.h>

// TODO: 定义一个联合体，包含 float 和 uint32_t
// TODO: 实现分解函数
// void print_float_bits(float f) {
//     // 提取符号位（1位）、指数（8位）、尾数（23位）
//     // 提示：用位运算 & 和 >>
// }

int main(void) {
    // TODO: 测试几个值：0.0f, -3.14f, 1.0f, 42.0f, 0.1f
    return 0;
}
```

### Exercise 2: 32-Bit Hardware Control Register

Use bit-fields to define a 32-bit hardware control register struct, and then write functions to manipulate it.

```c
#include <stdio.h>
#include <stdint.h>

// TODO: 定义 ControlRegister 位域结构体
// 位分配：
//   bit 0:     enable (1位)
//   bit 1:     interrupt_enable (1位)
//   bit 2:     dma_enable (1位)
//   bit 5:3    mode (3位)
//   bit 9:6    speed (4位)
//   bit 31:10  reserved (22位)

typedef union {
    // TODO: 位域结构体视图
    // TODO: uint32_t 整体视图
} ControlRegister;

// TODO: 实现 void print_register(ControlRegister reg)
// TODO: 实现 void set_mode(ControlRegister* reg, uint32_t mode)

int main(void) {
    ControlRegister reg = {0};
    // TODO: 测试各个操作
    return 0;
}
```

### Exercise 3: Simple Tagged Union

Use an enum and a union to implement a tagged union that can store a `int`, a `float`, or a string pointer.

```c
#include <stdio.h>
#include <stdint.h>

// TODO: 定义枚举类型标签
// TODO: 定义 tagged union 结构体
// TODO: 实现构造函数 make_int/make_float/make_string
// TODO: 实现 print_tagged_value 函数
// TODO: 实现 get_as_int/get_as_float/get_as_string 安全访问函数
//       （检查 tag 是否匹配，不匹配则打印错误信息）

int main(void) {
    // TODO: 创建三种类型的值，打印它们
    // TODO: 尝试用错误的 tag 访问，验证安全检查
    return 0;
}
```
