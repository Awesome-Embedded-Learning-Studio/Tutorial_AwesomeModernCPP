---
chapter: 1
cpp_standard:
- 11
description: Gain a deep understanding of the memory model and practical use cases
  for multi-level pointers, distinguish between arrays of pointers and pointers to
  arrays, and master the cdecl declaration reading convention and combinations of
  multi-level const pointers.
difficulty: beginner
order: 11
platform: host
prerequisites:
- 指针与数组、const 和空指针
reading_time_minutes: 12
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: Multi-Level Pointers and Declaration Reading
translation:
  source: documents/vol1-fundamentals/c_tutorials/08A-multi-level-pointers.md
  source_hash: 6e968d15e00e5bce8ca6401b44e88f3dad722376b4a53d4a256a7d4629c631ba
  translated_at: '2026-04-20T03:24:41.509579+00:00'
  engine: anthropic
  token_count: 1726
---
# Multi-Level Pointers and Reading Declarations

In the previous chapter, we clarified the relationships between pointers, arrays, `void*`, and `NULL`. Now let's tackle the trickier parts of pointers—multi-level pointers (pointers to pointers), the "confusing twins" of pointer arrays and array pointers, and a method for reading declarations like `int (*(*fp)(int))[10]` without your brain freezing.

Honestly, these concepts are easy to mix up when you're just starting out. But in my experience, don't try to memorize them blindly. Once you master a methodology for reading declarations, you can break down even the most complex ones. More importantly, C++ features like `unique_ptr`, `shared_ptr`, and pointer transfers via move semantics are all built on these underlying mechanisms.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the memory model and practical use cases of multi-level pointers
> - [ ] Distinguish between pointer arrays and array pointers
> - [ ] Use the cdecl method to break down any C declaration
> - [ ] Correctly read and write multi-level `const` pointer declarations

## Environment Setup

We will run all the following experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-std=c++17 -Wall -Wextra -g`

## Step 1 — Understand What Multi-Level Pointers Actually Point To

### Memory Model: Nested Layers

If the address stored in a pointer points to another pointer, that's a multi-level pointer. `int**` points to `int*`, `int***` points to `int**`, and so on. In memory, they form a chain:

![Multi-level pointer memory model](images/multi-level-pointer.svg)

Each level stores the address of the next level. Dereferencing `pp` yields `p` (an `int*`), dereferencing `p` yields `val` (an `int`), and only `*p` gives us the final value. Let's verify this:

```cpp
int val = 42;
int* p = &val;
int** pp = &p;

std::cout << "val  = " << val << "\n";   // 42
std::cout << "*p   = " << *p << "\n";    // 42
std::cout << "**pp = " << **pp << "\n";  // 42

std::cout << "&val = " << &val << "\n";  // 0x...
std::cout << "p    = " << p << "\n";     // same as &val
std::cout << "*pp  = " << *pp << "\n";   // same as p
std::cout << "pp   = " << pp << "\n";    // address of p itself
```

Output:

```text
val  = 42
*p   = 42
**pp = 42
&val = 0x7ffd1234
p    = 0x7ffd1234
*pp  = 0x7ffd1234
pp   = 0x7ffd1240
```

Great, each level of dereferencing moves downstream along the chain, ultimately fetching the value `42`.

### When to Use Multi-Level Pointers

Truth be told, going beyond two levels is rare in normal projects. The most common scenario is: **when you need to modify a pointer variable itself inside a function** (not the data it points to), you must pass the address of that pointer in:

```cpp
void alloc_buffer(char** out, size_t size) {
    *out = static_cast<char*>(malloc(size));
}

char* buf = nullptr;
alloc_buffer(&buf, 1024);  // Pass the address of buf
```

C only supports pass-by-value. To modify the `buf` variable itself, we must pass `&buf`—which is a `char**`.

> ⚠️ **Watch Out**
> Multi-level pointers are not for showing off. Pointers with three or more levels should not appear in the vast majority of projects—if you find yourself writing `int****`, there is likely a design flaw. Use structs to encapsulate data instead of using raw multi-level pointers.

### argv — The Most Common Double Pointer

The `argv` parameter of the `main` function is a `char**`:

```cpp
int main(int argc, char* argv[]) { /* ... */ }
```

`char* argv[]` in the parameter list decays to `char** argv`, so the two forms are identical. `argv` points to a `char*` array, where each element points to a command-line argument string, terminated by a `nullptr` sentinel:

![argv memory layout](images/argv-layout.svg)

## Step 2 — Distinguish Between Pointer Arrays and Array Pointers

`int* arr[10]` and `int (*arr)[10]` look like they only differ by a pair of parentheses, but their meanings are completely different. This is the most classic pair of "confusing twins" in C declaration syntax.

### Pointer Array: `int* arr[10]`

`int* arr[10]` declares an **array** containing 10 `int*` elements:

```cpp
int a = 1, b = 2, c = 3;
int* arr[3] = {&a, &b, &c};  // Array of 3 pointers

for (int i = 0; i < 3; ++i)
    std::cout << *arr[i] << " ";  // 1 2 3
```

Memory layout—the array contiguously stores three pointer values, and each pointer points to a different `int`:

![Pointer array memory layout](images/pointer-array.svg)

### Array Pointer: `int (*arr)[10]`

`int (*arr)[10]` declares a **pointer** that points to an entire row of an array containing 10 `int` elements. The most common use case is with 2D arrays:

```cpp
int matrix[3][10];
int (*arr)[10] = matrix;  // Points to the first row

arr++;  // Points to the second row
```

`arr++` skips an entire row (10 `int`s = 40 bytes), pointing to the next row.

> ⚠️ **Watch Out**
> `int *arr[10]` is not the answer you want—`[]` has higher precedence than `*`, so this evaluates `arr[10]` first before dereferencing, yielding a completely wrong result. The correct form must include parentheses: `int (*arr)[10]`. Precedence issues are one of the most common sources of bugs in C.

## Step 3 — Master the cdecl Reading Method

There is a systematic way to read any C declaration, called the "right-left rule" (also known as the spiral rule). The core principle: **start from the identifier, read to the right, then read to the left, and jump to the next level when you encounter parentheses.**

Take `int* a[10]` as an example:

1. Find the identifier `a`
2. Go right: `[10]` — "a is an array of 10 elements"
3. Go left: `*` — "of type pointer to int"
4. Combined: **a is an array of 10 elements of type pointer to int (pointer array)**

Take `int (*a)[10]` as an example:

1. Identifier `a`
2. Blocked by parentheses, go left first: `*` — "a is a pointer"
3. Exit parentheses, go right: `[10]` — "to an array of 10 elements"
4. Go left: `int` — "of type int"
5. Combined: **a is a pointer to an array of 10 int elements (array pointer)**

Now let's look at a function pointer: `int (*func)(double)`

1. Identifier `func`
2. Blocked by parentheses, go left: `*` — "func is a pointer"
3. Exit parentheses, go right: `(double)` — "to a function accepting a double parameter"
4. Go left: `int` — "returning int"
5. Combined: **func is a function pointer, pointing to a function that accepts a double and returns an int**

You'll get the hang of this method after a few practice rounds, and you won't panic when you see any weird declaration in the future. You can also use the online tool [cdecl.org](https://cdecl.org/) to verify your reading.

> ⚠️ **Watch Out**
> In the declaration `int* a, b;`, `a` is `int*`, but `b` is just `int`—not two pointers. The `*` follows the declarator, not the type. If you really want to declare two pointers, you must write `int *a, *b;`. This trap has tripped up countless people.

## Step 4 — Combinations of const and Multi-Level Pointers

Combinations of `const` and single-level pointers were covered in the previous chapter. Now let's look at multi-level cases—the core principle remains the same: **`const` modifies the type immediately to its left (if it's at the far left, it modifies the type to its right)**.

### Review: Single-Level const Pointers

```cpp
const int* p1;       // Pointer to const int (can't modify data via p1)
int* const p2;       // Const pointer to int (can't change where p2 points)
const int* const p3; // Const pointer to const int (can't do either)
```

### Multi-Level const Pointers

When `const` appears in `int**`, `const` can be added at different positions:

```cpp
const int** p1;       // Pointer to (pointer to const int)
int* const* p2;       // Pointer to (const pointer to int)
int** const p3;       // Const pointer to (pointer to int)
const int* const* p4; // Pointer to (const pointer to const int)
const int** const p5; // Const pointer to (pointer to const int)
int* const* const p6; // Const pointer to (const pointer to int)
const int* const* const p7; // The ultimate locked-down pointer
```

We still use the right-left rule to break it down layer by layer. Take `const int** p1` as an example: `p1` is a pointer → to a pointer → to `const int`.

This kind of thing is indeed uncommon in practice, but understanding how to read it is important—similar complex types frequently appear in C++ standard library function signatures and template error messages.

## Connecting to C++

C's multi-level pointer mechanisms all have modern equivalents in C++. Understanding the underlying principles helps you better use these high-level tools.

`std::vector` automatically manages dynamic arrays, eliminating the need for manual `malloc`/`free`. The pain of manually managing 2D arrays with `int**` in C (allocating, freeing row by row, easily forgetting), can be done in a single line in C++:

```cpp
std::vector<std::vector<int>> matrix(rows, std::vector<int>(cols, 0));
```

Move semantics are essentially pointer transfers—instead of copying data, they "steal" the ownership of a resource and nullify the source object. This is exactly the same as manually swapping pointers and nullifying in C, except C++ standardized this pattern.

`std::span` packages the classic C combination of "pointer + length" into a type-safe object, eliminating the need to manually manage the length, and it can be automatically constructed from arrays, vectors, and `std::array`.

`std::reference_wrapper` provides rebindable reference semantics, and can replace multi-level pointers when storing "references" in containers.

We will dive into these topics in later C++ tutorials. For now, just remember the core idea: **C++'s philosophy is to use the type system to automatically manage resources, rather than relying on the programmer's discipline**.

## Summary

The core logic of multi-level pointers is actually quite simple: each level stores the address of the next level, and dereferencing means moving downstream along the chain. The real source of confusion is pointer arrays versus array pointers—just remember "look at the parentheses first, then read in the direction." The cdecl reading method is the most important practical skill in this chapter; practice it a few times and you'll be able to break down any declaration. For multi-level `const`, use the right-left rule to analyze layer by layer, don't try to read it all at once.

## Exercises

### Exercise: Allocating and Freeing a Dynamic 2D Array

Use multi-level pointers to implement the allocation, filling, and freeing of a dynamic 2D array. Please implement the following three functions yourself:

```cpp
int** create_2d_array(int rows, int cols);
void fill_2d_array(int** arr, int rows, int cols);
void free_2d_array(int** arr, int rows);
```

Hint: When allocating, first allocate a pointer array (the dimension that `int**` points to), then `new` each row individually. When freeing, do the reverse order—free each row first, then free the pointer array itself.

## References

- [C declaration syntax - cppreference](https://en.cppreference.com/w/c/language/declarations)
- [cdecl: C declaration translator](https://cdecl.org/)
