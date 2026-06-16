---
chapter: 1
cpp_standard:
- 11
description: Gain a deep understanding of the memory model and practical use cases
  for multi-level pointers, distinguish between pointer arrays and array pointers,
  and master `cdecl` declaration syntax and combinations of multi-level `const` pointers.
difficulty: beginner
order: 11
platform: host
prerequisites:
- 指针与数组、const 和空指针
reading_time_minutes: 9
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: Multilevel Pointers and Declaration Syntax
translation:
  source: documents/vol1-fundamentals/c_tutorials/08A-multi-level-pointers.md
  source_hash: 4cd00db0e2461eff0f5b3da303b40d4974aef54993b33e2441c7d7973b2c43c5
  translated_at: '2026-06-16T05:49:41.504780+00:00'
  engine: anthropic
  token_count: 1725
---
# Multi-level Pointers and Reading Declarations

In the previous post, we clarified the relationship between pointers, arrays, `const`, and `NULL`. Now, let's tackle the more convoluted parts of pointers—multi-level pointers (pointers to pointers), the "confusing twins" (pointer arrays vs. array pointers), and a method to keep your brain from freezing when seeing declarations like `const int* const *`.

Honestly, these concepts are easy to mix up when learning. However, my experience is: don't rote memorize. Once you master a methodology for reading declarations, you can deconstruct even the most complex ones. More importantly, C++ features like `unique_ptr<T[]>`, `std::span`, and pointer transfers via move semantics are all built upon these underlying mechanisms.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the memory model and practical use cases of multi-level pointers.
> - [ ] Distinguish between pointer arrays and array pointers.
> - [ ] Deconstruct any C declaration using the cdecl reading method.
> - [ ] Correctly read and write multi-level `const` pointer declarations.

## Environment Setup

We will conduct all subsequent experiments in the following environment:

- Platform: Linux x86_64 (WSL2 is also acceptable)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c17`

## Step 1 — Understanding What Multi-level Pointers Actually Point To

### Memory Model: Chains within Chains

If the address stored in a pointer points to another pointer, we have a multi-level pointer. `int*` points to `int`, `int**` points to `int*`, `int***` points to `int**`, and so on. In memory, they resemble a chain:

```text
int*** ppp  ──→  int** pp  ──→  int* p  ──→  int value = 42
  0x1000          0x2000         0x3000       0x4000
```

Each level of the pointer stores the address of the next level. `*ppp` yields `pp` (`int**`), `**ppp` yields `p` (`int*`), and `***ppp` is the final value `42`. Let's verify this:

```c
#include <stdio.h>

int main(void)
{
    int value = 42;
    int* p = &value;
    int** pp = &p;
    int*** ppp = &pp;

    printf("value 的地址 = %p\n", (void*)&value);
    printf("p   的值     = %p\n", (void*)p);
    printf("pp  解一次   = %p\n", (void*)*pp);
    printf("ppp 解三次   = %d\n", ***ppp);
    return 0;
}
```

```bash
gcc -Wall -Wextra -std=c17 multi_ptr.c -o multi_ptr && ./multi_ptr
```

**Output:**

```text
value 的地址 = 0x7ffd1234abcd
p   的值     = 0x7ffd1234abcd
pp  解一次   = 0x7ffd1234abcd
ppp 解三次   = 42
```

Excellent. Each level of dereferencing moves further down the chain, eventually yielding `42`.

### When to use multi-level pointers

Frankly, scenarios involving more than two levels are rare in typical projects. The most common scenario is: **when we need to modify a pointer variable itself (not the data it points to) inside a function**, we must pass the address of that pointer in:

```c
void allocate_buffer(int** out_ptr, int size)
{
    *out_ptr = (int*)malloc(size * sizeof(int));
    // 修改的是 out_ptr 指向的那个指针变量
}

int main(void)
{
    int* buffer = NULL;
    allocate_buffer(&buffer, 100);
    // 现在 buffer 指向了 malloc 分配的内存
    free(buffer);
    return 0;
}
```

C uses pass-by-value only. To modify the `buffer` variable itself, we must pass `&buffer`—which means using an `int**`.

> ⚠️ **Warning**
> Multi-level pointers are not for showing off. Pointers with three or more levels of indirection should not appear in most projects—if you find yourself writing `int****`, there is a high probability that your design is flawed. Use structs to encapsulate data instead of using raw multi-level pointers.

### argv—The Most Common Double Pointer

The `argv` parameter of the `main` function is a `char**`:

```c
int main(int argc, char *argv[]) { /* ... */ }
int main(int argc, char **argv)    { /* ... */ }  // 完全等价
```

In the parameter list, `char *argv[]` decays to `char**`, so the two forms are identical. `argv` points to an array of `char*`, where each element points to a command-line argument string, terminated by a `NULL` sentinel:

```text
argv
  │
  ▼
  ┌─────┐     ┌─────────────────┐
  │ ptr ├────→│ "./myprogram\0" │  argv[0]
  ├─────┤     └─────────────────┘
  │ ptr ├────→│ "hello\0"       │  argv[1]
  ├─────┤     └─────────────────┘
  │ ptr ├────→│ "world\0"       │  argv[2]
  ├─────┤     └─────────────────┘
  │ NULL │     argv[3] = NULL
  └─────┘
```

## Step Two — Distinguishing Pointer Arrays and Array Pointers

`int* a[10]` and `int (*a)[10]` differ only by a pair of parentheses, yet their meanings are completely different. This is the classic "confusing twins" of C declaration syntax.

### Pointer Array: `int* a[10]`

`int* a[10]` declares an **array** containing 10 `int*` elements:

```c
int x = 10, y = 20, z = 30;
int* arr[3] = {&x, &y, &z};

printf("%d %d %d\n", *arr[0], *arr[1], *arr[2]);
// 10 20 30
```

Memory layout — the array stores three pointer values contiguously, each pointing to a different `int`:

```text
arr[0]  arr[1]  arr[2]
  │        │       │
  ▼        ▼       ▼
 &x       &y      &z
```

### Array Pointer: `int (*a)[10]`

`int (*a)[10]` declares a **pointer** that points to an entire array of 10 `int` values. The most common use case is with two-dimensional arrays:

```c
int matrix[3][10] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
    {10, 11, 12, 13, 14, 15, 16, 17, 18, 19},
    {20, 21, 22, 23, 24, 25, 26, 27, 28, 29}
};

int (*row_ptr)[10] = matrix;  // 指向第一行
printf("%d\n", (*row_ptr)[2]);         // 2
printf("%d\n", (*(row_ptr + 1))[2]);   // 12，跳到第二行
```

`row_ptr + 1` skips an entire row (10 `int`s = 40 bytes) and points to the next row.

> ⚠️ **Warning**
> `*(row_ptr + 1)[2]` is not what you want—the precedence of `[]` is higher than `*`, so this evaluates `(row_ptr + 1)[2]` first before dereferencing, leading to incorrect results. The correct syntax requires parentheses: `(*(row_ptr + 1))[2]`. Operator precedence is one of the most common sources of bugs in C.

## Step 3 — Mastering the cdecl Reading Method

There is a systematic way to read any C declaration, known as the "Right-Left Rule" (also called the Spiral Rule). The core rule is: **Start from the identifier, read to the right, then read to the left, and jump to the next level when you encounter parentheses.**

Take `int* a[10]` as an example:

1. Find the identifier `a`.
2. Go right: `[10]` — "a is an array of 10 elements".
3. Go left: `int*` — "of type pointer to int".
4. Combined: **a is an array of 10 pointers to int (pointer array).**

Take `int (*a)[10]` as an example:

1. Find the identifier `a`.
2. Blocked by parentheses to the right, go left first: `*` — "a is a pointer".
3. Exit the parentheses, go right: `[10]` — "to an array of 10 elements".
4. Go left again: `int` — "of type int".
5. Combined: **a is a pointer to an array of 10 ints (array pointer).**

Now let's look at a function pointer: `int (*func)(double)`

1. Find the identifier `func`.
2. Blocked by parentheses, go left: `*` — "func is a pointer".
3. Exit the parentheses, go right: `(double)` — "to a function taking a double".
4. Go left: `int` — "returning int".
5. Combined: **func is a function pointer pointing to a function that takes a double and returns an int.**

You will get the hang of this method after a few practice runs, so you won't panic when you see complex declarations in the future. You can also use the online tool [cdecl.org](https://cdecl.org/) to verify your interpretation.

> ⚠️ **Warning**
> In the declaration `int* a, b`, `a` is an `int*`, but `b` is just an `int`—not two pointers. The `*` binds to the declarator, not the type specifier. If you truly want to declare two pointers, you must write `int *a, *b`. This pitfall has tripped up countless developers.

## Step 4 — Combining `const` with Multi-level Pointers

The combination of `const` and single-level pointers was covered in the previous article. Now let's look at multi-level situations—the core principle remains unchanged: **`const` modifies the type immediately to its left (if it is on the far left, it modifies the type to the right).**

### Review: Single-level `const` Pointers

```c
const int* p1;              // 指向 const int 的指针，不能通过 p1 改值，但 p1 可改方向
int* const p2 = &v;         // const 指针，p2 不能改方向，但可通过它改值
const int* const p3 = &v;   // 都锁死了
```

### Multi-level const Pointers

When we have `int**`, `const` can be placed in different positions:

```c
int value = 42;
int* ptr = &value;

// 底层 const：指向的指针是只读的
int* const* pp1 = &ptr;
// pp1 可以改，*pp1 不能改，**pp1 可以改

// 顶层 const：pp2 本身是只读的
int** const pp2 = &ptr;
// pp2 不能改，*pp2 可以改，**pp2 可以改

// 双重 const
const int* const* pp3 = &ptr;
// pp3 可以改，*pp3 不能改，**pp3 不能改
```

We still use the right-left rule to parse this layer by layer. Taking `const int* const* p` as an example: `p` is a pointer → to a `const` pointer → which points to a `const int`.

While this is indeed rare in practice, understanding how to read it is crucial—similar complex types frequently appear in C++ standard library function signatures and template error messages.

## C++ Bridge

C's multi-level pointer mechanism has modern equivalents in C++. Understanding the underlying principles helps us use these high-level tools more effectively.

`std::unique_ptr<T[]>` automatically manages dynamic arrays, eliminating the need for manual `malloc`/`free`. The pain of manually managing two-dimensional arrays in C using `int**` (allocation, row-by-row deallocation, and the ease of forgetting) can be resolved in C++ with a single line:

```cpp
auto matrix = std::make_unique<int[]>(rows * cols);
// 用 matrix[i * cols + j] 访问，离开作用域自动释放
```

Move semantics essentially boils down to pointer transfer—instead of copying data, we "steal" ownership of the resource and leave the source object empty. This is exactly like manually swapping pointers and then nullifying them in C, except C++ standardizes this pattern.

`std::span<const int>` bundles the classic C function combination of "pointer + length" into a type-safe object. It eliminates manual length management and can be automatically constructed from arrays, vectors, or `std::array`.

`std::reference_wrapper<int>` provides rebindable reference semantics, acting as a cleaner alternative to multi-level pointers when storing "references" in containers.

We will discuss these topics in depth in the upcoming C++ tutorials. For now, just remember the core philosophy: **C++ relies on the type system to automatically manage resources, rather than relying on programmer discipline.**

## Summary

The core logic of multi-level pointers is actually quite simple: each level stores the address of the next level, and dereferencing simply moves down the chain. The real source of confusion lies between pointer arrays and array pointers—just remember to "check the parentheses first, then read the direction." The cdecl reading method is the most important skill from this article; with a little practice, you can dissect any declaration. Analyze multi-level `const` layer by layer using the right-left rule, rather than trying to read it all at once.

## Exercises

### Exercise: Allocation and Deallocation of Dynamic 2D Arrays

Use multi-level pointers to implement the allocation, population, and deallocation of a dynamic two-dimensional array. Please implement the following three functions yourself:

```c
/// @brief 分配 rows x cols 的动态二维数组
/// @param rows 行数
/// @param cols 列数
/// @return 指向二维数组的二级指针，失败返回 NULL
int** allocate_matrix(int rows, int cols);

/// @brief 释放动态二维数组
/// @param matrix 二级指针
/// @param rows 行数（用于逐行释放）
void free_matrix(int** matrix, int rows);

/// @brief 将二维数组的所有元素填充为指定值
/// @param matrix 二级指针
/// @param rows 行数
/// @param cols 列数
/// @param value 填充值
void fill_matrix(int** matrix, int rows, int cols, int value);
```

**Hint:** When allocating, first allocate the pointer array (the dimension pointed to by `int**`), then `malloc` each row individually. When freeing, do the reverse—free each row first, then free the pointer array itself.

## Resources

- [C declaration syntax - cppreference](https://en.cppreference.com/w/c/language/declarations)
- [cdecl: C declaration translator](https://cdecl.org/)
