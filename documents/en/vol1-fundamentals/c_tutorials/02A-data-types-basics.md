---
chapter: 1
cpp_standard:
- 11
description: Understand the C integer family from scratch — the difference between signed and unsigned, fixed-width types, and the sizeof operator — building a type-system foundation for everything that follows
difficulty: beginner
order: 2
platform: host
prerequisites:
- Program Structure and Compilation Basics
reading_time_minutes: 12
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: 'Data Type Basics: Integers and Memory'
translation:
  source: documents/vol1-fundamentals/c_tutorials/02A-data-types-basics.md
  source_hash: 5ae73b81d7e5368bee002b7e38f80bc5f38b4ca23450d40830baed5a50fa738f
  translated_at: '2026-09-25T12:38:24+00:00'
  engine: anthropic
  token_count: 3900
---
# Data Type Basics: Integers and Memory

If you have touched Python before, you probably remember that writing `x = 42` just works—you don't have to tell Python whether this `x` is an integer or a decimal; the interpreter guesses for itself. C plays by different rules: the moment each variable is born, we must tell the compiler explicitly "what type this thing actually is". At first glance this looks like pointless ceremony, but this very act of "declaring a type" is the foundation of C's performance—precisely because the compiler knows how much memory every variable occupies and how the data is stored, it can generate the most efficient machine code.

The ultimate goal of this entire C tutorial is to pave the way for C++, and C++ has done a great deal to strengthen C's type system. Once you understand "where C's types tend to go wrong", learning "how C++ fixes these problems" later on will feel completely natural. So let's honestly get C's type system thoroughly under our belt first, starting from the most basic thing there is: integers.

All the code is standard C and depends on no platform-specific APIs. If you are on macOS or MinGW on Windows, most of the experiments will still run—only the byte counts of certain types may differ slightly. We will come back to that specifically later.

## Step 1 — Figuring Out How C Stores Integers

### Understanding Data Types Through "Boxes"

We can picture memory as a long row of numbered boxes. Each box holds one byte of data. When you declare a variable, the compiler allocates a few consecutive boxes for you, and the variable name is the label stuck on those boxes. The **data type** decides two things: how many boxes this variable takes up, and how the 0s and 1s inside the boxes are to be interpreted.

Take the most intuitive example: `int` takes 4 boxes on most modern platforms (4 bytes = 32 bits) and can store integers roughly in the range of plus or minus 2.1 billion. `char` takes only 1 box (1 byte = 8 bits); the range of numbers it can hold is much smaller, but it saves space.

### A Family Portrait of the Integer Types

C provides five standard integer types, ordered here from the smallest representable range to the largest:

| Type | Minimum Bits Guaranteed by the Standard | Typical Actual Bits (32/64-bit platforms) |
|------|-----------------------------------|------------------------------------------|
| `char` | 8 bits | 8 bits |
| `short` | 16 bits | 16 bits |
| `int` | 16 bits | 32 bits |
| `long` | 32 bits | 32 bits (Windows) / 64 bits (Linux/macOS) |
| `long long` | 64 bits | 64 bits |

Note one key point: the C standard only specifies the **minimum guaranteed bits** for each type; a compiler may give you more, never less. This is why the same code may behave differently on different platforms—the `long` you write is 32 bits on Windows but 64 bits on Linux, and if your program relies on the exact width of `long`, you will most likely step in it when going cross-platform.

The width of `long` differs across operating systems—32 bits on Windows, 64 bits on Linux/macOS. If your code needs precise control over integer widths, do not use `long`; use the fixed-width types we will discuss shortly.

Another detail worth noting: `sizeof(char)` is always equal to 1—that is mandated by the standard. On some exotic DSP platforms, however, a "byte" may not be 8 bits. On the x86 and ARM platforms we use every day, a byte is always 8 bits, so there is no need to fret over this for now.

### Let's Verify — How Many Bytes Each Type Actually Takes

Let's write a small program and see how big each type actually is on your machine:

```c
#include <stdio.h>

int main(void)
{
    printf("char:      %zu 字节\n", sizeof(char));
    printf("short:     %zu 字节\n", sizeof(short));
    printf("int:       %zu 字节\n", sizeof(int));
    printf("long:      %zu 字节\n", sizeof(long));
    printf("long long: %zu 字节\n", sizeof(long long));

    return 0;
}
```

Compile and run:

```bash
gcc -Wall -Wextra -std=c17 sizeof_demo.c -o sizeof_demo && ./sizeof_demo
```

Our results on Linux x86\_64:

```text
char:      1 字节
short:     2 字节
int:       4 字节
long:      8 字节
long long: 8 字节
```

If you run this on Windows, the `long` line will very likely read `4 字节`—that is exactly the cross-platform difference we just talked about.

## Step 2 — Signed or Unsigned

### What "Signed" Means

Every member of the integer family (except `char`, which is a special case) comes in two variants: `signed` and `unsigned`. The "sign" here is the plus/minus sign—signed types can store both positive and negative numbers, while unsigned types can only store non-negative ones, but the same amount of memory then represents twice the range.

An analogy: take the same 8 light bulbs in a row. If we agree that "the first bulb lit means negative", the remaining 7 bulbs can represent numbers from -128 to 127; if we don't need a negative sign, all 8 bulbs are used for the number itself, and the range becomes 0 to 255.

```c
int signed_num = -42;           // signed: can store negative numbers
unsigned int unsigned_num = 42; // unsigned: can only store non-negative values
```

### The Signedness of `char`

`char` is a bit special—the standard does not pin down whether it is signed or unsigned; that depends on the compiler. On ARM platforms `char` is usually unsigned; on x86 it is usually signed. The difference looks unremarkable, but if you use `char` as a "small integer", you may step in it when crossing platforms:

```c
char c = 200;           // if char is signed, what is actually stored is -56
unsigned char uc = 200; // the value is 200 regardless of platform
```

When you need a "small integer" (in the 0\~255 range), use `unsigned char`, not `char`. The signedness of `char` depends on the compiler and the platform; use it as an integer and sooner or later it will bite you.

### Wraparound of Unsigned Integers

Unsigned integers follow one well-defined rule: on overflow, they **wrap around**. That is, if you store an unsigned number and adding 1 pushes it past its maximum, it starts over from 0. For example, the maximum of an 8-bit unsigned number is 255, and `255 + 1 = 0`.

Signed integer overflow, however, is dangerous—it is **undefined behavior** (Undefined Behavior, UB for short). The simple reading: the standard says "you may not do this", and if your program does it anyway, the compiler is allowed to handle it in any way—it may look normal, it may produce wrong results, it may crash outright. More insidiously, while optimizing, the compiler may assume "overflow never happens" and quietly delete the overflow check you wrote. We will devote a proper discussion to UB in the article on operators.

Signed integer overflow is undefined behavior. The result of `INT_MAX + 1` is unpredictable—it does not "wrap around to a negative number". Never rely on the behavior of signed overflow.

## Step 3 — Going Cross-Platform? Fixed-Width Types to the Rescue

### Where the Problem Lies

We just saw the problem of `long` being 32 bits on Windows and 64 bits on Linux. If you are writing a program that needs precise control over data widths—say, when talking to hardware and you must ensure a variable is exactly 32 bits—using `int` or `long` directly is unsafe, because their actual widths vary from platform to platform.

The solution given by the C99 standard is the `<stdint.h>` header. It provides a set of type aliases whose names carry the bit count directly:

```c
#include <stdint.h>

int8_t   i8  = -128;          // exactly 8 bits, signed
uint8_t  u8  = 255;           // exactly 8 bits, unsigned
int16_t  i16 = -32768;        // exactly 16 bits, signed
uint16_t u16 = 65535;         // exactly 16 bits, unsigned
int32_t  i32 = -2147483648;   // exactly 32 bits, signed
uint32_t u32 = 4294967295U;   // exactly 32 bits, unsigned
int64_t  i64 = 9223372036854775807LL;  // exactly 64 bits, signed
uint64_t u64 = 18446744073709551615ULL; // exactly 64 bits, unsigned
```

The beauty of these types is what-you-see-is-what-you-get—`int32_t` is exactly 32 bits on any platform that supports it, and `uint8_t` is always 8-bit unsigned. They are practically a must in embedded development and cross-platform code.

Note that the standard does not guarantee every platform provides all of the exact-width types. For instance, some DSPs may have no 8-bit addressing capability, in which case `int8_t` does not exist—the compiler will simply error out. On the x86 and ARM platforms we use every day, though, all exact-width types are available.

### `size_t` — The Guy You Meet Everywhere in the Standard Library

Before going further, we need to meet a type that shows up all over the standard library: `size_t`. It is the return type of the `sizeof` operator, and it is also the type used by functions such as `strlen` and `malloc`. `size_t` is unsigned, and its size follows the platform—32 bits on a 32-bit platform, 64 bits on a 64-bit platform.

```c
#include <stddef.h>

size_t len = 100;       // large enough to represent the size of any object
```

We will be working with `size_t` a lot from here on. For now, remember just one thing: **when you need to express a "count" or a "size", `size_t` is the right choice**.

### Let's Verify — The Sizes of the Fixed-Width Types

```c
#include <stdio.h>
#include <stdint.h>

int main(void)
{
    printf("int8_t:    %zu 字节\n", sizeof(int8_t));
    printf("uint8_t:   %zu 字节\n", sizeof(uint8_t));
    printf("int32_t:   %zu 字节\n", sizeof(int32_t));
    printf("uint32_t:  %zu 字节\n", sizeof(uint32_t));
    printf("int64_t:   %zu 字节\n", sizeof(int64_t));
    printf("size_t:    %zu 字节\n", sizeof(size_t));

    return 0;
}
```

Compile and run:

```bash
gcc -Wall -Wextra -std=c17 stdint_demo.c -o stdint_demo && ./stdint_demo
```

The result:

```text
int8_t:    1 字节
uint8_t:   1 字节
int32_t:   4 字节
uint32_t:  4 字节
int64_t:   8 字节
size_t:    8 字节
```

Great—the byte count of every type matches what we expected.

## Step 4 — `sizeof`: The Ruler That Measures Memory

### `sizeof` Is Not a Function

`sizeof` is a compile-time operator, not a function. It finishes the calculation at compile time, so there is zero runtime overhead. Its return type is `size_t`; use the `%zu` format specifier when printing it.

```c
int x = 42;
printf("%zu\n", sizeof(x));     // a variable: prints 4 (on platforms where int is 4 bytes)
printf("%zu\n", sizeof(int));   // a type name: also prints 4
```

`sizeof` has one classic use on arrays—counting the number of elements:

```c
int arr[] = {10, 20, 30, 40, 50};
size_t count = sizeof(arr) / sizeof(arr[0]);  // 20 / 4 = 5
printf("数组有 %zu 个元素\n", count);
```

The principle is simple: `sizeof(arr)` is the total number of bytes the whole array occupies, `sizeof(arr[0])` is the byte count of a single element, and dividing one by the other gives the element count.

This "use sizeof to count elements" trick **works only within the scope where the array is defined**. Once the array is passed to a function, it decays into a pointer, and `sizeof` returns the size of the pointer (4 or 8), not the size of the array:

```c
void bad_sizeof(int arr[])
{
    // arr is already a pointer here!
    printf("%zu\n", sizeof(arr));  // prints 4 or 8 (the pointer size), not the array size
}
```

We will expand on the mechanism of arrays decaying into pointers in the article on pointers. For now, just remember the conclusion: "an array passed to a function becomes a pointer".

## Bridging to C++

C++ fully inherits all of C's integer types, and it does a few important things to make the type system safer.

First, C++11 introduced the `<cstdint>` header (note, no `.h` suffix); it matches C's `<stdint.h>` in functionality, but the types are placed inside the `std` namespace. Second, `{}` initialization in C++ forbids "narrowing conversions"—you cannot initialize a variable with a value that falls outside the target type's range:

```cpp
int x = 3.14;      // allowed in both C and C++; implicitly truncated to 3 (the compiler may warn)
int y{3.14};        // C++ compile error! Narrowing conversions are forbidden
uint8_t z{1000};    // C++ compile error! 1000 is out of range for uint8_t
```

This feature is remarkably effective at wiping out an entire class of implicit-conversion bugs. If you write C++ code down the road, we strongly recommend getting into the habit of initializing with `{}`.

## Exercises

### Exercise 1: The Type Probe

**Difficulty: Basic** · Use sizeof to map out the size of every type

Write a program that prints the `sizeof` value of all the types below, then check them against the standard to see whether they meet the minimum guarantees:

```c
// Fill in the code: print sizeof for all of the types below
// char, short, int, long, long long
// int8_t, uint8_t, int32_t, uint32_t, int64_t
// size_t
```

Hint: you can use a macro to cut down the repetition.

::: details Reference Solution

```c
#include <stdio.h>
#include <stdint.h>
//#include <stddef.h>

int main() {
    // Basic types
    printf("sizeof(char)      = %zu bytes\n", sizeof(char));
    printf("sizeof(short)     = %zu bytes\n", sizeof(short));
    printf("sizeof(int)       = %zu bytes\n", sizeof(int));
    printf("sizeof(long)      = %zu bytes\n", sizeof(long));
    printf("sizeof(long long) = %zu bytes\n", sizeof(long long));

    // Fixed-width integer types (requires <stdint.h>)
    printf("sizeof(int8_t)    = %zu bytes\n", sizeof(int8_t));
    printf("sizeof(uint8_t)   = %zu bytes\n", sizeof(uint8_t));
    printf("sizeof(int32_t)   = %zu bytes\n", sizeof(int32_t));
    printf("sizeof(uint32_t)  = %zu bytes\n", sizeof(uint32_t));
    printf("sizeof(int64_t)   = %zu bytes\n", sizeof(int64_t));

    // size_t (requires <stdio.h>, <stddef.h>, or <stdlib.h>)
    printf("sizeof(size_t)    = %zu bytes\n", sizeof(size_t));

    return 0;
}

```

```text
sizeof(char)      = 1 bytes
sizeof(short)     = 2 bytes
sizeof(int)       = 4 bytes
sizeof(long)      = 4 bytes
sizeof(long long) = 8 bytes
sizeof(int8_t)    = 1 bytes
sizeof(uint8_t)   = 1 bytes
sizeof(int32_t)   = 4 bytes
sizeof(uint32_t)  = 4 bytes
sizeof(int64_t)   = 8 bytes
sizeof(size_t)    = 8 bytes
```

Notice that `sizeof(long)` is 4 here, while `sizeof(size_t)` is already 8—which tells us this output comes from an LLP64 environment (64-bit Windows, for example): in that model, pointers are 8 bytes, yet `long` is only 4. Move to 64-bit Linux or macOS (LP64), and `long` becomes 8 bytes. If you see `sizeof(long) = 8` on your own machine, the program is not wrong—it is a difference in data models.

:::

### Exercise 2: Watching Overflow

**Difficulty: Basic** · Watch signed-overflow UB versus unsigned wraparound

Run overflow experiments on a signed `int` and an unsigned `unsigned int`, respectively:

```c
#include <stdio.h>
#include <limits.h>

int main(void)
{
    int i = INT_MAX;
    unsigned int u = UINT_MAX;

    printf("INT_MAX  = %d,  INT_MAX + 1  = %d\n", i, i + 1);
    printf("UINT_MAX = %u, UINT_MAX + 1 = %u\n", u, u + 1);

    return 0;
}
```

Compile and run it, and observe how the two behave differently. Then recompile with the `-fsanitize=undefined` option and see what changes.

::: details Reference Solution

Assume the file is named overflow.c

After compiling and running with gcc overflow.c -o overflow && ./overflow, you will most likely see output like the following:

```text
INT_MAX  = 2147483647,  INT_MAX + 1  = -2147483648
UINT_MAX = 4294967295, UINT_MAX + 1 = 0
```

After compiling and running with gcc -fsanitize=undefined overflow.c -o overflow_ubsan && ./overflow_ubsan, you will see output like the following:

```text
INT_MAX  = 2147483647,  INT_MAX + 1  = -2147483648
overflow.c:9:54: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
UINT_MAX = 4294967295, UINT_MAX + 1 = 0
```

Why?
In fact, the C standard does not define overflow of signed integers at all. In other words, the operation of adding 1 to INT_MAX is, strictly speaking, undefined behavior.
(That said, overflow wraparound is quite convenient in practice, and most compilers support it by default.)

:::

## Reference Resources

- [cppreference: C integer types](https://en.cppreference.com/w/c/language/integer_constant)
- [cppreference: Fixed-width integer types](https://en.cppreference.com/w/c/types/integer)
- [Summary of C/C++ integer rules](https://www.nayuki.io/page/summary-of-c-cpp-integer-rules)
