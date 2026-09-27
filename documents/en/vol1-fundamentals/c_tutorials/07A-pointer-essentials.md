---
chapter: 1
cpp_standard:
  - 11
description: Understand C pointers from scratch — memory model intuition, declaration
  and initialization, the address-of and dereference operators, and pointer arithmetic
  and distance calculation
difficulty: beginner
order: 9
platform: host
prerequisites:
  - Data Type Basics: Integers and Memory
  - Operator Basics: Making Data Move
reading_time_minutes: 10
tags:
  - host
  - cpp-modern
  - beginner
  - 入门
title: 'Pointer Basics: The World of Addresses'
translation:
  source: documents/vol1-fundamentals/c_tutorials/07A-pointer-essentials.md
  source_hash: 87e8e241628694cd84d0af49bb476a713d805b5fce6d4296e606af2c53609aad
  translated_at: '2026-09-25T12:58:47+00:00'
  engine: anthropic
  token_count: 2500
---
# Pointer Basics: The World of Addresses

Pointers are probably the most famous feature of C—and the one most likely to scare beginners off. If you're coming from Python or Java, you may be used to thinking of "a variable as the object itself"—the variable holds the data directly. C adds a key concept on top of that: every variable lives at some location in memory, and that location has a number (an address). A pointer is a variable for storing and manipulating these addresses.

Honestly, pointers do take some time to build intuition for when you're first starting out. But don't be scared off just yet—we won't touch multilevel pointers, function pointers, or any of that complicated stuff today. Today we settle exactly one thing: **a pointer is an address, and an address is a locker number**. Once you understand that, every advanced pointer feature you meet later has solid ground to stand on.

## Step 1 — Understanding What an "Address" Is

### The Locker Model

Before we get into pointer syntax, let's build an intuition first. Picture your program's memory as one very, very long row of storage lockers. Every locker has a number (that's the **address**), and every locker can hold something (that's the **data**). When you declare a variable, the compiler allocates a few consecutive lockers for you, and the variable name is the label you attach to those lockers.

```c
int value = 42;
```

That line does two things: it allocates 4 consecutive lockers in memory (because `int` takes 4 bytes) and puts the value `42` inside them. `value` is the name you gave those 4 lockers, but the lockers themselves have a starting number—`0x7ffd1234`, say. That number is the address.

A pointer is a variable whose specialty is storing "locker numbers". An ordinary variable stores data (the contents of the locker); a pointer stores an address (the locker's number).

### Let's Verify — Taking a Look at a Variable's Address

Let's write the simplest possible program and take an actual look at what a variable's address looks like:

```c
#include <stdio.h>

int main(void)
{
    int value = 42;
    int other = 100;

    printf("value 的值:   %d\n", value);
    printf("value 的地址: %p\n", (void*)&value);
    printf("other 的地址: %p\n", (void*)&other);

    return 0;
}
```

Compile and run:

```bash
gcc -Wall -Wextra -std=c17 addr_demo.c -o addr_demo && ./addr_demo
```

The output (the addresses differ on every run, which is normal):

```text
value 的值:   42
value 的地址: 0x7ffd3a2b1c4c
other 的地址: 0x7ffd3a2b1c48
```

`%p` is the format specifier for printing pointer addresses, and `&value` takes the address of `value`. The two variables' addresses sit right next to each other (just 4 bytes apart), because both are allocated consecutively on the stack. The addresses change on every run of the program—that's the operating system's address space layout randomization (ASLR) security mechanism, and it doesn't get in the way of understanding the concept.

## Step 2 — Declaring Your First Pointer

### Pointer Declaration Syntax

The declaration syntax for a pointer variable is `type* name`. The `*` sitting next to the type says "this is a pointer to that type". The style we adopt writes the `*` attached to the type name on the left, i.e. `int* p`, so that "p is an int pointer" is visible at a glance.

```c
int value = 42;
int* ptr = &value;  // ptr stores the address of value
```

`&` is the address-of operator; it returns the memory address of its operand. `ptr` now holds the address of `value`, and we say "ptr points to value".

### Never Forget to Initialize

Here is a habit of vital importance: **always initialize a pointer when you declare it**. An uninitialized pointer stores a random value—it may point anywhere in memory. If you accidentally dereference an uninitialized pointer, at best you read garbage data, at worst you crash on the spot with a segmentation fault, and in the more insidious cases the program "looks normal" while the data has been quietly rewritten.

```c
int* good_ptr = NULL;     // Good: explicitly states "points to nothing"
int* bad_ptr;             // Dangerous: holds a random address; dereferencing it is undefined behavior
```

`int* p, q;` declares one `int*` and one `int`—not two pointers! The `*` only modifies the variable name immediately after it, `p`. To declare two pointers, you must write `int *p, *q;`. This is a classic trap of C's declaration syntax.

Initializing a pointer you're not using yet to `NULL` is a good habit. `NULL` is a special pointer value meaning "points to no valid memory address". Dereferencing `NULL` also causes a segmentation fault, of course, but at least this error is predictable and easy to debug—unlike a wild pointer, which manufactures Schrödinger's bugs for you.

## Step 3 — Playing with Addresses Using `&` and `*`

### A Pair of Inverse Operations

`&` (address-of) and `*` (dereference) are a pair of inverse operators: `&` takes a variable and gives you its address; `*` takes an address and gives you back the variable.

```c
int value = 42;
int* ptr = &value;     // &value → take value's address and assign it to ptr

printf("value 的地址: %p\n", (void*)ptr);    // print the address
printf("ptr 指向的值: %d\n", *ptr);          // *ptr → dereference, yields 42
```

Dereferencing `*ptr` means "follow the address stored in ptr and fetch the value from that piece of memory". If we can read, we can also write:

```c
*ptr = 100;
printf("value = %d\n", value);  // prints 100 — the original variable was modified through the pointer
```

This is exactly the power of pointers: holding an address lets you operate directly on the data in that memory—whether that memory sits in the current function's stack frame, on the heap, or in a memory-mapped hardware register region.

To verify, let's chain the operations above into a single run:

```c
#include <stdio.h>

int main(void)
{
    int value = 42;
    int* ptr = &value;

    printf("初始: value = %d, *ptr = %d\n", value, *ptr);
    printf("地址: &value = %p, ptr = %p\n", (void*)&value, (void*)ptr);

    *ptr = 100;
    printf("修改后: value = %d, *ptr = %d\n", value, *ptr);

    return 0;
}
```

The output:

```text
初始: value = 42, *ptr = 42
地址: &value = 0x7ffd1234abcd, ptr = 0x7ffd1234abcd
修改后: value = 100, *ptr = 100
```

Great—the address held by `ptr` and `&value` match exactly, and `*ptr = 100` really did change the value of `value`.

### The `*` Symbol Pulls Double Duty

One spot that easily trips up beginners is that `*` holds two jobs: in a declaration it says "this is a pointer type"; in an expression it means "dereference". These are two different things—don't confuse them.

- In `int* p = &x;`, the `*` is part of the type declaration, telling the compiler "p is an int pointer"
- In `*p = 10;`, the `*` is the dereference operator, meaning "follow the address in p and write data there"

They look the same but mean completely different things. The trick to telling them apart is context: if `*` appears after a type name and before a variable name, it's a declaration; if it appears before a variable name in a statement, it's a dereference.

## Step 4 — Pointers Can Do Addition and Subtraction Too

### Stepping by Type Size

Pointers don't just store addresses—they also support a limited amount of arithmetic. But this "addition and subtraction" is not the same thing as ordinary integer arithmetic: pointer arithmetic steps by the **size of the pointed-to type**.

By way of analogy: you're standing in front of a row of lockers, each 40 cm wide. When you say "move forward 1 slot", you actually move 40 cm, not 1 cm. Pointer addition and subtraction is exactly this kind of "slot-by-slot" movement—the compiler knows each `int` occupies 4 bytes, so `p + 1` actually adds 4 to the address.

```c
int arr[5] = {10, 20, 30, 40, 50};
int* p = arr;     // p points to arr[0]

p++;              // p now points to arr[1]
                 // the address increased by sizeof(int), i.e. 4 bytes

int val = *(p + 2);  // p+2 skips two ints, landing on arr[3]; val = 40
```

`p + 2` doesn't add 2 to the address value—it adds `2 * sizeof(int)`. The design is beautifully clever: it makes pointer arithmetic a natural fit for array subscript offsets.

### The Distance Between Pointers

Two pointers into the same array can be subtracted, and the result is the number of elements between them (the distance), not the byte difference of the addresses:

```c
int arr[5] = {10, 20, 30, 40, 50};
int* start = &arr[1];
int* end   = &arr[4];

ptrdiff_t distance = end - start;   // 3, not 12
```

`ptrdiff_t` is a type defined in `<stddef.h>` specifically to represent pointer distances.

Pointer arithmetic is only meaningful when the pointers point into the same array (or the same contiguously allocated block of memory). Subtracting two completely unrelated pointers is undefined behavior. The compiler won't raise an error, but the result is unpredictable.

Let's verify the effect of pointer arithmetic:

```c
#include <stdio.h>
#include <stddef.h>

int main(void)
{
    int arr[5] = {10, 20, 30, 40, 50};
    int* p = arr;

    printf("arr[0] = %d, *p = %d\n", arr[0], *p);
    p++;
    printf("p++ 后: *p = %d (arr[1])\n", *p);
    printf("*(p+2) = %d (arr[3])\n", *(p + 2));

    int* start = &arr[1];
    int* end = &arr[4];
    printf("end - start = %td 个元素\n", end - start);

    return 0;
}
```

The output:

```text
arr[0] = 10, *p = 10
p++ 后: *p = 20 (arr[1])
*(p+2) = 40 (arr[3])
end - start = 3 个元素
```

Everything came out exactly as we expected.

## Bridging to C++

C++ makes two key improvements on top of pointers. The first is the **reference**: `int& r = value` is essentially a const pointer that the compiler dereferences automatically—it must be initialized at declaration, cannot be rebound once bound, and needs no `*` at the point of use; syntactically it's as if you were operating on the original variable directly. References are much safer than pointers, and C++ prefers pass-by-reference for function parameters.

The second is **smart pointers**: `std::unique_ptr` and `std::shared_ptr` use RAII to manage the memory's lifetime automatically—when the pointer goes out of scope, the memory is released, which eliminates at the root the memory leaks and dangling pointers that come from manual `free`. We'll discuss these in depth later; for now, all you need to know is that C++'s core idea is "let the type system and object lifetimes do the management automatically".

## Exercises

### Exercise 1: Addresses and Values

**Difficulty: Basic** · observe &, *, sizeof, and address spacing

Write a program that declares three variables of different types (`int`, `double`, `char`) and prints their values, addresses, and `sizeof` results. Observe whether the spacing between the addresses matches each type's size.

::: details Reference solution

```c
#include <stdio.h>

int main(void) {

    int value_int = 0;
    double value_double = 0.0;
    char value_char = '0';

    printf("(int) value:%d         address:%p    size:%zu\n", value_int, (void*)&value_int, sizeof(int));
    printf("(double) value:%.2f    address:%p    size:%zu\n", value_double, (void*)&value_double, sizeof(double));
    printf("(char) value:%d        address:%p    size:%zu\n", value_char, (void*)&value_char, sizeof(char));
    return 0;
}

```

The output may look like this (the addresses change on every run):

```text
(int) value:0        address:0x7ffd8cecdbec    size:4
(double) value:0.00  address:0x7ffd8cecdbf0    size:8
(char) value:48      address:0x7ffd8cecdbeb    size:1
```

Two things are worth noting:

- The `char` `value` displays as `48`, because the ASCII code of `'0'` is exactly 48. In C, a `char` is at heart a small integer, and printing it with `%d` shows its integer value (to see the character `'0'` itself, just switch the format specifier to `%c`).
- The gaps between the three addresses do not equal the respective type sizes. In declaration order it's `int`(4) → `double`(8) → `char`(1), but the actual addresses came out laid out as `char` → `int` → `double`, and the neighboring differences aren't 4, 8, 1 either. There are two reasons: the compiler reorders local variables and inserts padding bytes for memory alignment; and stack layout never promises to place variables in declaration order in the first place. So the intuition that "address spacing exactly equals type size" usually doesn't hold with a real compiler—which is exactly what this exercise wanted you to see with your own eyes.

:::

### Exercise 2: Walking an Array with Pointers

**Difficulty: Intermediate** · walk an array with pointer arithmetic

Traverse an `int` array with pointer arithmetic and print all its elements. The requirement: no `[]` operator—only pointer addition/subtraction and dereference:

```c
/// @brief Traverse an int array with pointer arithmetic and print every element
/// @param data address of the array's first element
/// @param count number of elements
void print_int_array(const int* data, size_t count);
```

::: details Reference solution

```c
#include <stdio.h>
#include <stddef.h>   // size_t

void print_int_array(const int* data, size_t count);

int main(void) {
    int arr[] = {1, 2, 3, 4, 5};
    print_int_array(arr, 5);
    return 0;
}

void print_int_array(const int* data, size_t count) {
    for (size_t i = 0; i < count;i++) {
        printf("data[%zu] = %d\n", i, *(data + i));
    }
}

```

The output should be:

```text
data[0] = 1
data[1] = 2
data[2] = 3
data[3] = 4
data[4] = 5
```

:::

## References

- [cppreference: Pointer declaration](https://en.cppreference.com/w/c/language/pointer)
- [cppreference: Pointer arithmetic](https://en.cppreference.com/w/c/language/operator_arithmetic#Pointer_arithmetic)
