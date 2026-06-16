---
chapter: 1
cpp_standard:
- 11
description: Understanding C Pointers from Scratch — Memory Model Intuition, Declaration
  and Initialization, Address-of and Dereference Operators, Pointer Arithmetic, and
  Distance Calculation
difficulty: beginner
order: 9
platform: host
prerequisites:
- 数据类型基础：整数与内存
- 运算符基础：让数据动起来
reading_time_minutes: 10
tags:
- host
- cpp-modern
- beginner
- 入门
title: 'Pointer Basics: The World of Addresses'
translation:
  source: documents/vol1-fundamentals/c_tutorials/07A-pointer-essentials.md
  source_hash: f0b1efa872a871a6c0f010e99d280c55982fc9ab28bdd758bf0e9d33981770e8
  translated_at: '2026-06-16T03:33:52.430612+00:00'
  engine: anthropic
  token_count: 1577
---
# Pointers 101: The World of Addresses

Pointers are likely the most famous, yet intimidating, feature in C. If you are coming from Python or Java, you might be used to the idea that "a variable is the object itself"—the variable holds the data directly. In C, however, a key concept emerges: every variable resides at a specific location in memory, and this location has a number (an **address**). Pointers are variables designed to store and manipulate these addresses.

Admittedly, building intuition for pointers takes some time. But don't panic—we won't touch complex topics like multi-level pointers or function pointers just yet. Today, we focus on one thing: **a pointer is an address, and an address is just a locker number**. Once you grasp this, you will have a solid foundation for all advanced pointer-related features.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Use the "locker" model to understand the relationship between memory and addresses.
> - [ ] Correctly declare and initialize pointer variables.
> - [ ] Understand the inverse operations of address-of (`&`) and dereference (`*`).
> - [ ] Master pointer arithmetic and distance calculation.

## Environment Setup

We will conduct all experiments in the following environment:

- Platform: Linux x86\_64 (WSL2 is acceptable)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-std=c17 -Wall -Wextra -pedantic`

## Step 1 — Understanding "Addresses"

### The Locker Model

Before diving into syntax, let's build an intuition. Imagine program memory as a very long row of lockers. Each locker has a number (this is the **address**), and items can be placed inside (this is the **data**). When you declare a variable, the compiler allocates a series of consecutive lockers for you. The variable name is simply the label you attach to these lockers.

```c
int value = 42;
```

This line does two things: it allocates 4 consecutive lockers in memory (because `int` takes 4 bytes) and places the value `42` inside them. `value` is the label you gave these lockers, but the lockers themselves have a starting number—like `0x7ffc3a8`. This number is the address.

A pointer is a variable specifically designed to store these "locker numbers." A normal variable stores data (the contents of the locker), while a pointer stores an address (the number on the locker).

### Let's Verify — Inspecting Variable Addresses

Let's write a simple program to see what variable addresses actually look like:

```c
#include <stdio.h>

int main(void) {
    int a = 10;
    int b = 20;

    printf("Address of a: %p\n", (void*)&a);
    printf("Address of b: %p\n", (void*)&b);

    return 0;
}
```

Compile and run:

```bash
gcc -std=c17 -Wall -Wextra -pedantic main.c -o main
./main
```

Result (addresses will vary on each run, which is normal):

```text
Address of a: 0x7ffc3a8
Address of b: 0x7ffc3a4
```

`%p` is the format specifier for printing a pointer address, and `&` takes the address of `a`. The addresses of the two variables are close (4 bytes apart) because they are allocated contiguously on the stack. The addresses change every run due to the OS's Address Space Layout Randomization (ASLR) security mechanism, but this doesn't affect our understanding of the concept.

## Step 2 — Declaring Your First Pointer

### Pointer Declaration Syntax

The syntax for declaring a pointer variable is `type *name`. The `*` appearing next to the type indicates "this is a pointer to this type." We prefer the style where `*` is placed next to the type name, i.e., `int *ptr`, so it is immediately clear that "ptr is an int pointer."

```c
int value = 42;
int *ptr = &value;
```

`&` is the address-of operator; it returns the memory address of its operand. `ptr` now holds the address of `value`, and we say "ptr points to value."

### Don't Forget Initialization

Here is a critical habit: **always initialize pointers when declaring them.** An uninitialized pointer contains a random value—it could point anywhere in memory. If you accidentally dereference an uninitialized pointer, you might read garbage data, cause a segmentation fault, or worse—corrupt data silently while the program "looks" fine.

```c
int *p1 = NULL;      // Good: Explicitly initialized
int *p2;             // Bad: Uninitialized (contains garbage)
```

> ⚠️ **Common Pitfall**
> `int *p1, p2;` declares an `int *` and an `int`—not two pointers! The `*` only modifies the variable name immediately following it (`p1`). To declare two pointers, you must write `int *p1, *p2;`. This is a classic trap in C declaration syntax.

Initializing unused pointers to `NULL` is a good habit. `NULL` is a special pointer value representing "points to no valid memory address." While dereferencing `NULL` will also cause a segmentation fault, at least the error is predictable and easy to debug—unlike wild pointers which create Schrödinger's bugs.

## Step 3 — Manipulating Addresses with `&` and `*`

### A Pair of Inverse Operations

`&` (address-of) and `*` (dereference) are inverse operators: `&` gets the address from a variable, and `*` gets the variable from the address.

```c
int value = 42;
int *ptr = &value;  // ptr holds the address of value

printf("Value via ptr: %d\n", *ptr); // Read: Follow ptr to get the value
```

Dereferencing `ptr` means "follow the address stored in ptr to that memory location and retrieve the value." Since we can read, we can naturally write:

```c
*ptr = 100; // Write: Follow ptr to modify the value
```

This is the power of pointers: holding an address allows you to directly manipulate data at that memory location, whether it's on the current function's stack, on the heap, or in a hardware register mapped region.

Let's verify this by chaining these operations:

```c
#include <stdio.h>

int main(void) {
    int value = 42;
    int *ptr = &value;

    printf("Address of value: %p\n", (void*)&value);
    printf("Address held by ptr: %p\n", (void*)ptr);
    printf("Initial value: %d\n", *ptr);

    *ptr = 100;

    printf("Modified value: %d\n", value);

    return 0;
}
```

Result:

```text
Address of value: 0x7ffc3a8
Address held by ptr: 0x7ffc3a8
Initial value: 42
Modified value: 100
```

Excellent, the addresses of `value` and `ptr` are identical, and we successfully modified `value` through `ptr`.

### The Dual Role of the `*` Symbol

A common point of confusion for beginners is that `*` serves two purposes: in a declaration, it indicates "this is a pointer type"; in an expression, it means "dereference." These are two different things—don't mix them up.

- `int *p = &value;` — Here, `*` is part of the type declaration, telling the compiler "p is an int pointer".
- `*p = 10;` — Here, `*` is the dereference operator, meaning "write data to the address held by p".

They look the same but have entirely different meanings. The trick to distinguishing them is context: if `*` appears after a type name and before a variable name, it's a declaration; if it appears before a variable name in a statement, it's a dereference.

## Step 4 — Pointers Can Do Math Too

### Stepping by Type Size

Pointers aren't just for storing addresses; they support limited arithmetic operations. However, this "addition and subtraction" differs from integer arithmetic—pointer arithmetic steps by the **size of the pointed-to type**.

An analogy: You stand in front of a row of lockers, each 40 cm wide. Saying "move forward 1 locker" means you physically move 40 cm, not 1 cm. Pointer arithmetic is this "locker-based" movement—the compiler knows each `int` is 4 bytes, so `ptr + 1` actually adds 4 to the address.

```c
int arr[3] = {10, 20, 30};
int *ptr = arr; // Points to arr[0]

// ptr + 1 doesn't add 1 to the address value, it adds sizeof(int)
// (ptr + 2) points to arr[2]
```

`ptr + 2` doesn't add 2 to the address value, it adds `2 * sizeof(int)`. This design is ingenious—it makes pointer arithmetic naturally align with array index offsets.

### Distance Between Pointers

Two pointers pointing to elements within the same array can be subtracted. The result is the number of elements (distance) between them, not the difference in bytes:

```c
int arr[5] = {10, 20, 30, 40, 50};
int *p1 = &arr[0];
int *p2 = &arr[4];

ptrdiff_t dist = p2 - p1; // Result is 4
```

`ptrdiff_t` is a type defined in `<stddef.h>` specifically for representing pointer distances.

> ⚠️ **Common Pitfall**
> Pointer arithmetic is only meaningful if the pointers point to elements within the same array (or the same contiguous memory block). Subtracting two unrelated pointers is undefined behavior. The compiler won't error, but the result is unpredictable.

Let's verify the effect of pointer arithmetic:

```c
#include <stdio.h>

int main(void) {
    int arr[] = {10, 20, 30, 40, 50};
    int *ptr = arr; // Points to arr[0]

    printf("First element: %d\n", *ptr);       // 10
    printf("Second element: %d\n", *(ptr + 1)); // 20
    printf("Third element: %d\n", *(ptr + 2));  // 30

    return 0;
}
```

Result:

```text
First element: 10
Second element: 20
Third element: 30
```

Everything works as expected.

## C++ Transition

C++ makes two key improvements on top of C pointers. The first is the **reference**. A reference `T&` is essentially a const pointer that the compiler automatically dereferences—it must be initialized when declared and cannot be rebound once set. You don't use the `*` operator when using it; syntactically, it acts like the original variable. References are much safer than pointers, and passing by reference is preferred for C++ function parameters.

The second is **smart pointers**. `std::unique_ptr` and `std::shared_ptr` use the RAII mechanism to automatically manage memory lifecycles—memory is released when the pointer goes out of scope, fundamentally eliminating memory leaks and dangling pointers caused by manual `new`/`delete`. We will discuss these in depth later; for now, just know that the core philosophy of C++ is "using the type system and object lifecycles for automatic management."

## Summary

Today we established a basic understanding of pointers: a pointer is a variable that stores a memory address. `&` takes the address, `*` dereferences it—they are inverse operations. Pointer arithmetic steps by the size of the pointed-to type, naturally fitting array traversal. Pointers must be initialized (even if just to `NULL`); uninitialized pointers are dangerous.

We have only laid the "foundation" for pointers so far. Next, we will tackle questions like: What is the exact relationship between arrays and pointers? How do we distinguish between `*ptr++` and `(*ptr)++? What is the difference between NULL pointers and wild pointers? We will discuss these in the next article.

## Exercises

### Exercise 1: Addresses and Values

Write a program that declares three variables of different types (`int`, `double`, `char`), prints their values, addresses, and the result of dereferencing their pointers. Observe if the spacing between addresses matches the size of each type.

### Exercise 2: Traversing Arrays with Pointers

Use pointer arithmetic to traverse an `int` array and print all elements. Do not use the `[]` operator; use only pointer addition and dereference:

```c
int arr[] = {1, 2, 3, 4, 5};
// Your code here
```

## References

- [cppreference: Pointer Declaration](https://en.cppreference.com/w/c/language/pointer)
- [cppreference: Pointer Arithmetic](https://en.cppreference.com/w/c/language/operator_arithmetic#Pointer_arithmetic)
