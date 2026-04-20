---
chapter: 1
cpp_standard:
- 11
description: Understanding C pointers from scratch—memory model intuition, declaration
  and initialization, address-of and dereference operators, pointer arithmetic, and
  distance calculation
difficulty: beginner
order: 9
platform: host
prerequisites:
- 数据类型基础：整数与内存
- 运算符基础：让数据动起来
reading_time_minutes: 12
tags:
- host
- cpp-modern
- beginner
- 入门
title: 'Pointer Basics: The World of Addresses'
translation:
  source: documents/vol1-fundamentals/c_tutorials/07A-pointer-essentials.md
  source_hash: c5ae5266d83b12c2ca20c96f0c8fd7ecb48aafd78b5580467c8165f931b797ec
  translated_at: '2026-04-20T03:21:28.871435+00:00'
  engine: anthropic
  token_count: 1577
---
# Pointer Basics: The World of Addresses

Pointers are arguably the most famous—and most intimidating—feature in C. If you come from Python or Java, you might be used to thinking of variables as the objects themselves—the variable holds the data directly. But in C, there is a crucial new concept: every variable resides at a specific location in memory, and that location has a number (an address). Pointers are simply variables that store and manipulate these addresses.

Admittedly, building intuition for pointers takes some time upfront. But don't panic just yet—we won't touch multi-level pointers or function pointers today. We will focus on one single idea: **a pointer is an address, and an address is a locker number**. Once you grasp this, you will have a solid foundation for all the advanced pointer features down the road.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Use the "storage locker" model to understand the relationship between memory and addresses
> - [ ] Correctly declare and initialize pointer variables
> - [ ] Understand the inverse relationship between the address-of (`&`) and dereference (`*`) operators
> - [ ] Master pointer arithmetic (addition, subtraction) and distance calculation

## Environment Setup

We will run all the following experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c17`

## Step 1 — Understand What an "Address" Is

### The Storage Locker Model

Before diving into pointer syntax, let's build some intuition. Imagine your program's memory as a very long row of storage lockers. Each locker has a number (this is the **address**), and you can put things inside it (this is the **data**). When you declare a variable, the compiler allocates a few consecutive lockers for you, and the variable name is simply the label you put on those lockers.

```c
int value = 42;
```

This line does two things: it allocates four consecutive lockers in memory (because an `int` takes up 4 bytes) and places the value `42` inside them. `value` is the label you gave these four lockers, but the lockers themselves have a starting number—for example, `0x7ffd1234`. This number is the address.

A pointer is simply a variable designed to store "locker numbers." A regular variable stores data (the contents of the locker), while a pointer stores an address (the locker's number).

### Let's Verify — Look at a Variable's Address

Let's write a minimal program to see what a variable's address actually looks like:

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

Output (the addresses will differ each run, which is perfectly normal):

```text
value 的值:   42
value 的地址: 0x7ffd3a2b1c4c
other 的地址: 0x7ffd3a2b1c48
```

`%p` is the format specifier for printing pointer addresses, and `&value` takes the address of `value`. The two addresses are very close together (differing by only 4 bytes) because they are allocated contiguously on the stack. The addresses change on every run due to the operating system's Address Space Layout Randomization (ASLR) security mechanism, but this doesn't affect our understanding of the concepts.

## Step 2 — Declare Your First Pointer

### Pointer Declaration Syntax

The syntax for declaring a pointer variable is `类型* 变量名`. The `*` next to the type indicates "this is a pointer to that type." We prefer the style where `*` sticks to the type name—i.e., `int* p`—so you can see at a glance that "p is an int pointer."

```c
int value = 42;
int* ptr = &value;  // ptr 存储了 value 的地址
```

`&` is the address-of operator, which returns the memory address of its operand. `ptr` now holds the address of `value`, and we say "ptr points to value."

### Never Forget to Initialize

Here is a crucial habit: **always initialize a pointer when you declare it**. An uninitialized pointer contains a random value—it could point to anywhere in memory. If you accidentally dereference an uninitialized pointer, the best-case scenario is reading garbage data, the worst is an immediate segmentation fault, and in the most insidious cases, the program "looks fine" but its data has been silently corrupted.

```c
int* good_ptr = NULL;     // 好：明确表示"不指向任何东西"
int* bad_ptr;             // 危险：包含随机地址，解引用是未定义行为
```

> ⚠️ **Gotcha Warning**
> `int* p, q;` declares an `int*` and an `int`—not two pointers! The `*` only modifies the variable name immediately following it, `p`. To declare two pointers, you must write `int *p, *q;`. This is a classic trap in C declaration syntax.

Initializing an unused pointer to `NULL` is a good habit. `NULL` is a special pointer value meaning "does not point to any valid memory address." While dereferencing `NULL` will also cause a segmentation fault, at least the error is predictable and easy to debug—unlike a wild pointer, which creates Schrödinger's bugs.

## Step 3 — Manipulate Addresses with `&` and `*`

### A Pair of Inverse Operations

`&` (address-of) and `*` (dereference) are inverse operators: `&` gets the address from a variable, and `*` gets the variable from the address.

```c
int value = 42;
int* ptr = &value;     // &value → 取得 value 的地址，赋给 ptr

printf("value 的地址: %p\n", (void*)ptr);    // 打印地址
printf("ptr 指向的值: %d\n", *ptr);          // *ptr → 解引用，得到 42
```

Dereferencing `*ptr` means "follow the address stored in ptr and fetch the value from that memory location." Since we can read, we can naturally write, too:

```c
*ptr = 100;
printf("value = %d\n", value);  // 输出 100——通过指针修改了原始变量
```

This is the true power of pointers: if you hold an address, you can directly manipulate the data at that memory location—whether that memory is in the current function's stack frame, on the heap, or in a hardware register's memory-mapped region.

Let's verify this by chaining the operations above and running them:

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

Output:

```text
初始: value = 42, *ptr = 42
地址: &value = 0x7ffd1234abcd, ptr = 0x7ffd1234abcd
修改后: value = 100, *ptr = 100
```

Great, the addresses of `ptr` and `&value` are exactly identical, and modifying through `*ptr = 100` indeed changed the value of `value`.

### The `*` Symbol Wears Two Hats

A point of confusion for beginners is that the `*` symbol serves two purposes: in a declaration, it means "this is a pointer type"; in an expression, it means "dereference." These are two entirely different things—don't mix them up.

- The `*` in `int* p = &x;` is part of the type declaration, telling the compiler "p is an int pointer"
- The `*` in `*p = 10;` is the dereference operator, meaning "follow p's address and write data there"

They look identical but mean completely different things. The trick to telling them apart is context: if `*` appears after a type name and before a variable name, it's a declaration; if it appears before a variable name inside a statement, it's a dereference.

## Step 4 — Pointers Can Do Math, Too

### Stepping by Type Size

Pointers don't just store addresses—they also support a limited set of arithmetic operations. But this "addition and subtraction" is not the same as integer arithmetic—pointer math steps by **the size of the pointed-to type**.

Think of it this way: you are standing in front of a row of lockers, each 40 centimeters wide. If you say "move forward 1 locker," you actually move 40 centimeters, not 1 centimeter. Pointer arithmetic works exactly like this "locker-based" movement—the compiler knows each `int` takes 4 bytes, so `p + 1` actually adds 4 to the address.

```c
int arr[5] = {10, 20, 30, 40, 50};
int* p = arr;     // p 指向 arr[0]

p++;              // p 现在指向 arr[1]
                 // 地址增加了 sizeof(int)，即 4 字节

int val = *(p + 2);  // p+2 跳过两个 int，指向 arr[3]，val = 40
```

`p + 2` does not add 2 to the raw address value; it adds `2 * sizeof(int)`. This design is incredibly elegant—it makes pointer arithmetic naturally align with array index offsets.

### Distance Between Pointers

Two pointers pointing to elements within the same array can be subtracted, and the result is the number of elements between them (the distance), not the byte difference of their addresses:

```c
int arr[5] = {10, 20, 30, 40, 50};
int* start = &arr[1];
int* end   = &arr[4];

ptrdiff_t distance = end - start;   // 3，不是 12
```

`ptrdiff_t` is a type defined in `<stddef.h>` specifically for representing pointer distances.

> ⚠️ **Gotcha Warning**
> Pointer arithmetic is only meaningful when the pointers point into the same array (or the same contiguous block of allocated memory). Subtracting two completely unrelated pointers is undefined behavior. The compiler won't flag it as an error, but the result is unpredictable.

Let's verify the effects of pointer arithmetic:

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

Output:

```text
arr[0] = 10, *p = 10
p++ 后: *p = 20 (arr[1])
*(p+2) = 40 (arr[3])
end - start = 3 个元素
```

Everything works exactly as we expected.

## Bridging to C++

C++ makes two key improvements on top of C pointers. The first is the **reference**, where `int& r = value` is essentially a const pointer that the compiler dereferences automatically—it must be initialized at declaration, cannot be rebound once bound, and doesn't require writing `*` when used, making it syntactically identical to operating on the original variable. References are much safer than pointers, and C++ prefers passing function parameters by reference.

The second is the **smart pointer**. `std::unique_ptr` and `std::shared_ptr` use the RAII mechanism to automatically manage memory lifecycles—memory is freed automatically when the pointer goes out of scope, fundamentally eliminating the memory leaks and dangling pointer problems caused by manual `free`. We will dive deep into these topics later; for now, just know that C++'s core philosophy is to "use the type system and object lifecycles for automatic management."

## Summary

Today we built a foundational understanding of pointers: a pointer is simply a variable that stores a memory address. `&` takes the address, `*` dereferences it, and they are inverse operations. Pointer arithmetic steps by the size of the pointed-to type, naturally fitting array traversal. Pointers must be initialized (even if just to `NULL`), and uninitialized pointers are dangerous.

We have only laid the "foundation" for pointers so far. The next questions arise: what exactly is the relationship between arrays and pointers? How do we distinguish between `const int* p` and `int* const p`? What is the difference between a NULL pointer and a wild pointer? These are exactly what we will discuss in the next article.

## Exercises

### Exercise 1: Addresses and Values

Write a program that declares three variables of different types (`int`, `double`, `char`), and prints their values, addresses, and `sizeof` results. Observe whether the gaps between addresses match the sizes of their respective types.

### Exercise 2: Traversing an Array with Pointers

Use pointer arithmetic to traverse an `int` array and print all elements. You must not use the `[]` operator—only pointer addition, subtraction, and dereference:

```c
/// @brief 使用指针算术遍历并打印 int 数组
/// @param data 数组首元素地址
/// @param count 元素个数
void print_int_array(const int* data, size_t count);
```

## References

- [cppreference: Pointer declarations](https://en.cppreference.com/w/c/language/pointer)
- [cppreference: Pointer arithmetic](https://en.cppreference.com/w/c/language/operator_arithmetic#Pointer_arithmetic)
