---
chapter: 1
cpp_standard:
- 11
description: Gain a deep understanding of the mechanism of array name decay to pointers,
  the four combinations of `const` and pointers, and how to prevent `NULL` pointers
  and wild pointers, laying a foundation for learning C++ references and smart pointers.
difficulty: beginner
order: 10
platform: host
prerequisites:
- 指针入门：地址的世界
reading_time_minutes: 11
tags:
- host
- cpp-modern
- beginner
- 入门
title: Pointers, Arrays, const, and Null Pointers
translation:
  source: documents/vol1-fundamentals/c_tutorials/07B-pointers-arrays-const.md
  source_hash: f6c070ed1e6b103d72987545db43b7d7b7c228fb4c7a721d97d4854264715a35
  translated_at: '2026-06-16T03:34:09.899396+00:00'
  engine: anthropic
  token_count: 1746
---
# Pointers, Arrays, const, and Null Pointers

In the previous chapter, we mastered the basics of pointers—declaration, initialization, taking addresses, dereferencing, and pointer arithmetic. Now, let's tackle a few more complex but crucial applications: what is the actual relationship between arrays and pointers, how many meanings do `const` and pointers have when combined, and why `NULL` pointers and wild pointers are so dangerous.

Don't worry, we'll take this one step at a time. There's a lot to cover, but the core logic is actually quite clear.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Understand the mechanism of array name decay to pointers and the two exceptional cases.
> - [ ] Correctly read and write the four combined declarations of `const` and pointers.
> - [ ] Distinguish between `NULL` pointers and wild pointers, and master defensive methods.

## Environment Setup

We will run all our experiments in the following environment:

- Platform: Linux x86\_64 (WSL2 is acceptable)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-std=c17 -Wall -Wextra -pedantic`

## Step 1 — What Exactly is an Array Name?

### "Decay" — A Core Rule

There is a very important rule in C: **In most contexts, an array name automatically decays into a pointer to its first element**. This rule sounds academic, but it's actually quite easy to understand—the array name `arr` itself represents a whole contiguous block of memory. However, when you assign it to a pointer or pass it to a function, the compiler only passes the starting address of that block, and the length information of the array is "lost".

```c
int arr[5] = {10, 20, 30, 40, 50};
int *p = arr; // Equivalent to: int *p = &arr[0];
```

`arr` itself is of type `int[5]` (an array containing 5 `int`s), but when assigned to a pointer, it automatically converts to `int *` (a pointer to the first element). This means `arr[i]` and `p[i]` are completely equivalent—the subscript operator `[]` is essentially syntactic sugar for pointer arithmetic.

Because of this, we can use pointers to traverse arrays:

```c
#include <stdio.h>

int main(void) {
    int arr[5] = {10, 20, 30, 40, 50};
    int *p = arr;

    for (int i = 0; i < 5; i++) {
        printf("Element %d: %d (Address: %p)\n", i, *p, (void *)p);
        p++; // Move to the next integer
    }

    return 0;
}
```

Let's verify this by compiling and running:

```bash
gcc -std=c17 -Wall -Wextra main.c -o main
./main
```

Output:

```text
Element 0: 10 (Address: 0x7ffc12345600)
Element 1: 20 (Address: 0x7ffc12345604)
Element 2: 30 (Address: 0x7ffc12345608)
Element 3: 40 (Address: 0x7ffc1234560c)
Element 4: 50 (Address: 0x7ffc12345610)
```

### However — An Array is Not a Pointer

Here is the key point: an array name only "frequently decays to a pointer", **an array itself is not a pointer**. There are two scenarios where the array name does not decay:

First, the `sizeof` operator. `sizeof(arr)` returns the total byte size of the entire array (5 × 4 = 20 bytes), not the size of a pointer (4 or 8 bytes). This is the technique we used in the last chapter to calculate the number of array elements: `n = sizeof(arr) / sizeof(arr[0])`.

Second, the `&` (address-of) operator. The type of `&arr` is "pointer to the entire array" (`int (*)[5]`), not "pointer to a pointer" (`int **`). It has the same numeric value as `arr` (both are the address of the first byte of the array), but the type is different, so the step size in pointer arithmetic is also different.

Let's verify these differences:

```c
#include <stdio.h>

int main(void) {
    int arr[5] = {10, 20, 30, 40, 50};

    printf("sizeof(arr) = %zu\n", sizeof(arr));       // 20 bytes
    printf("sizeof(&arr) = %zu\n", sizeof(&arr));     // 8 bytes (pointer size)
    printf("arr       : %p\n", (void *)arr);
    printf("&arr      : %p\n", (void *)&arr);
    printf("arr + 1   : %p (diff: %td bytes)\n",
           (void *)(arr + 1), (char *)(arr + 1) - (char *)arr);
    printf("&arr + 1  : %p (diff: %td bytes)\n",
           (void *)(&arr + 1), (char *)(&arr + 1) - (char *)&arr);

    return 0;
}
```

Output:

```text
sizeof(arr) = 20
sizeof(&arr) = 8
arr       : 0x7ffc12345600
&arr      : 0x7ffc12345600
arr + 1   : 0x7ffc12345604 (diff: 4 bytes)
&arr + 1  : 0x7ffc12345614 (diff: 20 bytes)
```

Excellent, `arr` and `&arr` have the same numeric value, but `arr + 1` only skipped 4 bytes (one `int`), while `&arr + 1` skipped 20 bytes (the entire array). This is "different types, different step sizes".

> ⚠️ **Pitfall Warning**
> Once an array is passed to a function, it will definitely decay to a pointer—inside the function, `sizeof` returns the size of the pointer, not the size of the array. Therefore, if you need to know the array length inside a function, you must pass a length parameter as well.

## Step 2 — Four Combinations of const and Pointers

The combination of `const` and pointers is a classic interview question and something frequently used in actual coding. There are four combinations in total. Let's break them down one by one, starting with the most intuitive.

### 1. Non-const Pointer to const Data

```c
const int *p1; // Read-only data, pointer can move
```

`const int *p1` means "the `int` pointed to by `p1` is read-only"—you cannot modify that value through `p1`, but `p1` itself can point to other variables. Note that the variable itself doesn't strictly have to be `const`; you are just promising not to modify it via the `p1` path. This usage is extremely common in function parameters—`void func(const int *p)` tells the caller "don't worry, I promise not to touch your data".

### 2. const Pointer to Non-const Data

```c
int * const p2; // Read-only pointer, data is mutable
```

The pointer itself is `const`—once initialized, it will always point to the same address, but you can modify the data in that memory block through it. This usage is common in embedded development, for example, for hardware register mapping at a fixed address:

```c
int * const GPIO_ODR = (int * const)0x40020014; // Fixed address
*GPIO_ODR = 0xFF; // OK: Writing data
// GPIO_ODR = ...; // Error: Cannot modify pointer
```

The value of the pointer (the address) is fixed, but the register can be read or written through it.

### 3. const Pointer to const Data

```c
const int * const p3; // Both read-only
```

Both sides are locked—the pointer cannot change direction, and the data cannot be modified through the pointer. This is typically used for accessing read-only hardware registers or constant lookup tables.

### 4. Ordinary Pointer

```c
int *p4; // Both mutable
```

This is the most ordinary `int *`. Both sides can be changed, with no special constraints.

### How to Read These Declarations

A practical reading technique: look at whether `const` appears to the left or right of the `*`.

- `const` on the **left** of `*`: modifies the **pointed-to data** (data is immutable).
- `const` on the **right** of `*`: modifies the **pointer itself** (direction is immutable).
- Both sides: both are immutable.

> ⚠️ **Pitfall Warning**
> Reading declarations from right to left is also a good method: `const int *p` → "p is a pointer to int const" (pointer to a const int); `int * const p` → "p is a const pointer to int" (const pointer to an int).

## Step 3 — NULL Pointers and Wild Pointers

### NULL — "I'm Not Pointing to Anything"

`NULL` is a macro with a value of `((void *)0)`, indicating that it does not point to any valid memory address. Dereferencing a `NULL` pointer is undefined behavior (UB)—on most systems it triggers a segmentation fault (SIGSEGV), crashing the program immediately.

A segmentation fault sounds bad, but it's actually a "good crash"—the problem is exposed immediately. You just attach a debugger, and you know right away it's a null pointer dereference. In contrast, the wild pointer discussed below is the truly scary thing.

### Wild Pointers — Ticking Time Bombs in Code

A wild pointer is a pointer that points to invalid memory. It usually comes from three sources:

The first is **uninitialized pointers**—declared but not assigned, containing random values on the stack; this address could point anywhere. The second is **dangling pointers**—the pointer once pointed to valid memory, but that memory has been freed (using the pointer after `free`). The third is **out-of-bounds access**—pointer arithmetic has gone beyond the legal range.

```c
int *p;             // Wild: Uninitialized
int *q = malloc(4);
free(q);
*q = 10;            // Wild: Dangling pointer (use-after-free)
int arr[5];
int *r = arr + 10;  // Wild: Out of bounds
```

The scary thing about wild pointers is that they don't necessarily crash immediately—they might happen to point to a writable block of memory. Your program "seems" to run normally, but some unrelated variable has been quietly overwritten by you. The symptoms of such bugs may be far removed from the cause, making debugging a frustrating experience.

> ⚠️ **Pitfall Warning**
> Wild pointers create "Schrödinger's bugs"—in your program, everything might seem normal until one day you switch compilers or turn on optimizations, and it suddenly crashes. Moreover, the crash location is often far from the real bug, making troubleshooting extremely painful.

### Three Rules of Defense

The best defensive measures are actually very simple. Just remember these three rules:

1. **Initialize immediately when declaring pointers**—even if you initialize them to `NULL`.
2. **Set to `NULL` immediately after `free`**—to prevent subsequent misuse.
3. **Check if it is `NULL` before using the pointer**—add a layer of protection.

```c
int *p = NULL; // Rule 1
p = malloc(sizeof(int));

if (p != NULL) { // Rule 3
    *p = 10;
}

free(p);
p = NULL; // Rule 2
```

These three rules can help you avoid the vast majority of pointer-related disasters. I sincerely suggest: burn these three rules into your muscle memory, and you will save yourself a lot of hair loss later in your coding career.

## C++ Transition

C raw pointers are powerful, but the responsibility lies entirely with the programmer. C++ builds on this by doing a few very critical things.

First is the **reference**. A reference `T&` is essentially a const pointer that the compiler automatically dereferences—it must be initialized when declared, cannot be rebound once bound, and doesn't need `*` when used, syntactically acting like direct manipulation of the original variable. A reference cannot be `NULL` (well, strictly speaking, you can construct a dangling reference, but that's intentionally asking for trouble), and it cannot point to uninitialized memory. In C++, passing references is preferred over passing pointers for function parameters.

Then there are **smart pointers**. `std::unique_ptr` and `std::shared_ptr` use the RAII mechanism to automatically manage memory lifecycles—memory is automatically released when the pointer goes out of scope, fundamentally eliminating memory leaks and dangling pointer issues caused by manual `new`/`delete`.

```cpp
std::unique_ptr<int> p(new int(42)); // C++ style
// No need to manually delete, automatic cleanup
```

We will discuss these in depth in the subsequent C++ tutorials. For now, just know the core philosophy: **C++ uses the type system and object lifecycles for automatic management, rather than relying on programmer self-discipline**.

## Summary

Let's review the core points of this chapter. An array name decays to a pointer to its first element in most contexts, but `sizeof` and `&` are two exceptions—in these scenarios, the array name retains its "array" identity. `const` and pointers have four combinations; just remember "const on the left of `*` modifies the data, on the right modifies the pointer itself". While `NULL` pointers cause segmentation faults, that is a "good crash"; wild pointers are the real time bombs. Remembering the three rules of defense (initialize on declaration, set to `NULL` after `free`, check before use) will help you avoid the vast majority of disasters.

At this point, we have built a solid foundation in pointers. Next, we will learn about functions—how to organize code to make it more reusable and maintainable.

## Exercises

### Exercise 1: Linear Search with Pointers

Implement a linear search function that returns a pointer to the first occurrence of the target value in the array. If not found, return `NULL`.

```c
int *find_int(int *arr, int size, int target) {
    // TODO: Iterate using pointer arithmetic
    // Return pointer if found, NULL otherwise
}
```

### Exercise 2: Array Reversal with Pointers

Implement a function that reverses an array in-place, using only pointer arithmetic (two pointers moving from both ends towards the middle), without using array subscripts:

```c
void reverse(int *arr, int size) {
    // TODO: Swap elements using two pointers
}
```

### Exercise 3: const Practice

For each of the following declarations, determine which operations are legal and which will result in a compilation error:

```c
int x = 10;
int y = 20;
const int *p1 = &x;
int * const p2 = &x;
const int * const p3 = &x;

// Test cases:
// *p1 = 15;  // ?
// p1 = &y;   // ?
// *p2 = 15;  // ?
// p2 = &y;   // ?
// *p3 = 15;  // ?
// p3 = &y;   // ?
```

## References

- [cppreference: Pointer Declaration](https://en.cppreference.com/w/c/language/pointer)
- [cppreference: NULL](https://en.cppreference.com/w/c/types/NULL)
- [cppreference: Array-to-pointer Decay](https://en.cppreference.com/w/c/language/conversion)
