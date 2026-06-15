---
chapter: 1
cpp_standard:
- 11
description: Gain a deep understanding of the memory model and practical use cases
  for multi-level pointers, distinguish between arrays of pointers and pointers to
  arrays, and master the cdecl declaration reading method and combinations of multi-level
  const pointers.
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
title: Multi-level Pointers and Declaration Reading
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/c_tutorials/08A-multi-level-pointers.md
  source_hash: 6e968d15e00e5bce8ca6401b44e88f3dad722376b4a53d4a256a7d4629c631ba
  token_count: 1726
  translated_at: '2026-05-26T10:30:25.011609+00:00'
---
# Multi-Level Pointers and Reading Declarations

In the previous chapter, we clarified the relationships between pointers, arrays, `nullptr`, and `NULL`. Now let's tackle the trickier parts of pointers—multi-level pointers (pointers to pointers), the "confusing twins" of pointer arrays and array pointers, and a method to keep your brain from crashing when you see declarations like `int (*(*fp)(int))[10]`.

Honestly, these concepts are easy to mix up when you're first learning. But in my experience, don't try to rote-memorize them. Once you master a methodology for reading declarations, you can break down even the most complex ones. More importantly, C++ features like `std::unique_ptr`, `std::shared_ptr`, and pointer transfers via move semantics are all built on these underlying mechanisms.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the memory model and practical use cases of multi-level pointers
> - [ ] Distinguish between pointer arrays and array pointers
> - [ ] Break down any C declaration using the cdecl reading method
> - [ ] Correctly read and write multi-level `const` pointer declarations

## Environment Setup

We will run all the following experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-std=c++17 -Wall -Wextra -g`

## Step 1 — Understand What Multi-Level Pointers Actually Point To

### Memory Model: Nested Links

If the address stored in a pointer points to another pointer, that's a multi-level pointer. `int *p1` points to `int`, `int **p2` points to `int *`, `int ***p3` points to `int **`, and so on. In memory, they form a chain:

```text
p3 ──→ p2 ──→ p1 ──→ int(42)
```

Each level stores the address of the next level. `*p3` yields `p2` (type `int **`), `**p3` yields `p1` (type `int *`), and `***p3` is the final `int`. Let's verify this:

```cpp
int val = 42;
int *p1 = &val;
int **p2 = &p1;
int ***p3 = &p2;

printf("p3  = %p\n", (void *)p3);
printf("*p3 = %p  (p2)\n", (void *)*p3);
printf("**p3 = %p  (p1)\n", (void *)**p3);
printf("***p3 = %d  (val)\n", ***p3);
```

```text
p3  = 0x7ffd12345680
*p3 = 0x7ffd12345678  (p2)
**p3 = 0x7ffd12345670  (p1)
***p3 = 42  (val)
```

Great, each level of dereferencing moves downstream along the chain, ultimately fetching the `val`.

### When to Use Multi-Level Pointers

Truth be told, situations requiring more than two levels are rare in normal projects. The most common scenario is: **when you want to modify a pointer variable itself inside a function** (not the data it points to), you need to pass the address of that pointer in:

```cpp
void alloc_int(int **out) {
    *out = (int *)malloc(sizeof(int));
    **out = 42;
}

int *p = NULL;
alloc_int(&p);  // Pass the address of p
```

C only supports pass-by-value. To modify the `p` variable itself, we must pass `&p`—which is of type `int **`.

> ⚠️ **Pitfall Warning**
> Multi-level pointers are not for showing off. Pointers with three or more levels should not appear in the vast majority of projects—if you find yourself writing `int ****p`, there's likely a flaw in your design. Use structs to encapsulate data instead of using raw multi-level pointers.

### argv — The Most Common Double Pointer

The `argv` parameter of the `main` function is an `char **`:

```cpp
int main(int argc, char *argv[]) { ... }
int main(int argc, char **argv) { ... }  // Exactly the same
```

`char *argv[]` in a parameter list decays to `char **argv`, so both forms are identical. `argv` points to a `char *` array, where each element points to a command-line argument string, terminated by a `NULL` sentinel:

```text
argv ──→ [ argv[0] ] ──→ "./program"
         [ argv[1] ] ──→ "hello"
         [ argv[2] ] ──→ "world"
         [ argv[3] ] ──→ NULL
```

## Step 2 — Distinguish Between Pointer Arrays and Array Pointers

`int *arr[10]` and `int (*arr)[10]` look like they only differ by a pair of parentheses, but their meanings are completely different. This is the most classic pair of "confusing twins" in C declaration syntax.

### Pointer Array: `int *arr[10]`

`int *arr[10]` declares an **array** containing 10 `int *` elements:

```cpp
int a = 1, b = 2, c = 3;
int *arr[3] = {&a, &b, &c};

printf("%d\n", *arr[0]);  // 1
printf("%d\n", *arr[1]);  // 2
printf("%d\n", *arr[2]);  // 3
```

Memory layout—the array contiguously stores three pointer values, and each pointer points to a different `int`:

```text
arr[0] ──→ int(1)
arr[1] ──→ int(2)
arr[2] ──→ int(3)
```

### Array Pointer: `int (*arr)[10]`

`int (*arr)[10]` declares a **pointer** that points to an entire row of an array containing 10 `int` elements. The most common use case is with 2D arrays:

```cpp
int matrix[3][10];
int (*arr)[10] = matrix;  // Points to the first row

arr[0][0] = 42;
arr++;  // Skip an entire row (10 ints = 40 bytes), point to the next row
arr[0][0] = 99;  // This is matrix[1][0]
```

`arr++` skips an entire row (10 `int`s = 40 bytes), pointing to the next row.

> ⚠️ **Pitfall Warning**
> `int *arr = matrix` is not the answer you want—the precedence of `[]` is higher than `*`, so `int *arr[3]` would first evaluate the array subscript and then dereference, leading to completely wrong results. The correct syntax requires parentheses: `int (*arr)[10]`. Precedence issues are one of the most common sources of bugs in C.

## Step 3 — Master the cdecl Reading Method

There is a systematic way to read any C declaration, called the "right-left rule" (also known as the spiral rule). The core principle: **start from the identifier, read to the right first, then to the left, and jump to the next level when you encounter parentheses**.

Take `int *a[10]` as an example:

1. Find the identifier `a`
2. Go right: `[10]` — "a is an array of 10 elements"
3. Go left: `*` — "of pointer type"
4. Go left: `int` — "to int"
5. Combined: **a is an array of 10 elements of type pointer to int (pointer array)**

Take `int (*a)[10]` as an example:

1. Identifier `a`
2. Blocked by parentheses going right, so go left first: `*` — "a is a pointer"
3. Exit parentheses, go right: `[10]` — "to an array of 10 elements"
4. Go left: `int` — "of type int"
5. Combined: **a is a pointer to an array of 10 ints (array pointer)**

Now let's look at a function pointer: `int (*func)(double)`

1. Identifier `func`
2. Blocked by parentheses, go left: `*` — "func is a pointer"
3. Exit parentheses, go right: `(double)` — "to a function taking a double parameter"
4. Go left: `int` — "returning int"
5. Combined: **func is a function pointer, pointing to a function that takes a double and returns an int**

You'll get the hang of this method after a few practice rounds, and you won't panic when you see any weird declaration in the future. You can also use the online tool [cdecl.org](https://cdecl.org/) to verify your reading.

> ⚠️ **Pitfall Warning**
> In the declaration `int *a, b;`, `a` is an `int *`, but `b` is just an `int`—not two pointers. The `*` follows the declarator, not the type. If you really want to declare two pointers, you must write `int *a, *b;`. This trap has tripped up countless people.

## Step 4 — Combinations of const and Multi-Level Pointers

The combinations of `const` and single-level pointers were covered in the previous chapter. Now let's look at multi-level cases—the core principle remains the same: **`const` modifies the type immediately to its left (if it's at the far left, it modifies the type to its right)**.

### Review: Single-Level const Pointers

```cpp
const int *p;       // Pointer to const int (can't modify data via p)
int *const p;       // Const pointer to int (can't change where p points)
const int *const p; // Const pointer to const int (can't do either)
```

### Multi-Level const Pointers

When `int **` appears, `const` can be added at different positions:

```cpp
const int **p;       // Pointer to (pointer to const int)
int *const *p;       // Pointer to (const pointer to int)
int **const p;       // Const pointer to (pointer to int)
const int *const *p; // Pointer to (const pointer to const int)
const int **const p; // Const pointer to (pointer to const int)
```

We still use the right-left rule to break it down layer by layer. Take `const int **const p` as an example: `p` is a const pointer → pointing to a pointer → pointing to a `const int`.

This kind of thing is indeed uncommon in practice, but understanding how to read it is very important—similar complex types frequently appear in C++ standard library function signatures and template error messages.

## Connecting to C++

The multi-level pointer mechanisms in C all have modern counterparts in C++. Understanding the underlying principles helps us better use these high-level tools.

`std::vector` automatically manages dynamic arrays, eliminating the need for manual `malloc`/`free`. The pain of manually managing 2D arrays with `int **` in C (allocating, freeing row by row, easily forgetting), can be done in a single line in C++:

```cpp
std::vector<std::vector<int>> matrix(3, std::vector<int>(10));
```

Move semantics are essentially pointer transfers—instead of copying data, the ownership of the resource is "stolen" and the source object is nullified. This is exactly the same as manually swapping pointers and nullifying them in C, except C++ has standardized this pattern.

`std::span` packages the classic C combination of "pointer + length" into a single type-safe object, removing the need to manually manage the length, and it can be automatically constructed from arrays, vectors, and arrays.

`std::reference_wrapper` provides rebindable reference semantics, which can replace multi-level pointers when storing "references" in containers.

We will dive deep into these topics in subsequent C++ tutorials. For now, just remember the core idea: **the philosophy of C++ is to use the type system to automatically manage resources, rather than relying on the programmer's discipline**.

## Summary

The core logic of multi-level pointers is actually quite simple: each level stores the address of the next level, and dereferencing means moving downstream along the chain. The real source of confusion is pointer arrays versus array pointers—just remember "look at the parentheses first, then read in the direction." The cdecl reading method is the most important practical skill in this chapter; practice it a few times and you'll be able to break down any declaration. For multi-level `const`, analyze it layer by layer using the right-left rule, don't try to read it all at once.

## Exercises

### Exercise: Allocation and Deallocation of a Dynamic 2D Array

Use multi-level pointers to implement the allocation, filling, and deallocation of a dynamic 2D array. Please implement the following three functions yourself:

```cpp
int **alloc_2d(int rows, int cols);
void fill_2d(int **matrix, int rows, int cols);
void free_2d(int **matrix, int rows);
```

Hint: When allocating, first allocate a pointer array (the dimension that `int **` points to), then `malloc` each row individually. When freeing, do the reverse order—free each row first, then free the pointer array itself.

## References

- [C declaration syntax - cppreference](https://en.cppreference.com/w/c/language/declarations)
- [cdecl: C declaration translator](https://cdecl.org/)
