---
chapter: 1
cpp_standard:
- 11
description: Deep dive into C array memory layout, multidimensional arrays, variable-length
  arrays, and their subtle relationship with pointers.
difficulty: beginner
order: 14
platform: host
prerequisites:
- 指针与数组、const 和空指针
reading_time_minutes: 18
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: Deep Dive into Arrays
translation:
  source: documents/vol1-fundamentals/c_tutorials/10-arrays-deep-dive.md
  source_hash: 64faac23f36135a24055abf224194859ca0eadb38171dea7e1e5da34f6c22dca
  translated_at: '2026-06-16T03:35:41.026843+00:00'
  engine: anthropic
  token_count: 2966
---
# A Deep Dive into Arrays

In the previous crash course and pointer chapters, we touched on arrays, but honestly, we stayed at the "just using them" level. Arrays seem simple to use—declare, initialize, access by index—who doesn't know how? But once you start asking questions like "How are multi-dimensional arrays actually laid out?", "Why can't I assign arrays directly?", or "When are arrays and pointers the same and when are they different?"—you'll find there are quite a few details worth unpacking. These details aren't just theoretical; understanding the memory model of arrays will clarify exactly what problems C++'s `std::array`, `std::vector`, and `std::span` are solving later on.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Master various initialization methods for one-dimensional arrays (including C99 designated initializers).
> - [ ] Understand the memory layout of multi-dimensional arrays and row-major storage.
> - [ ] Understand the principles and limitations of Variable Length Arrays (VLA).
> - [ ] Grasp the fundamental limitations of arrays.
> - [ ] Precisely distinguish the differences between arrays and pointers.

## Environment Setup

All code in this chapter is based on the C99 standard, tested under GCC 13.x / Clang 17.x on a Linux x86-64 environment. Sections involving Variable Length Arrays (VLA) require compiler support for C99 (`-std=c99` or `-std=c11`). If you are using MSVC, note that Microsoft's C compiler has incomplete support for C99, and some VLA features may not be available—using GCC or Clang is recommended.

## Step 1 — Master Various Array Initialization Methods

Everyone knows how to declare an array, `int arr[10];` does the job. But the details of initialization are richer than many imagine. Let's start with the basics and work our way up to the designated initializers introduced in C99.

### Basic Initialization

```c
int a[5] = {1, 2, 3, 4, 5};       // Fully initialized
int b[5] = {1, 2, 3};             // Partial initialization, remaining are 0
int c[5] = {0};                   // All elements initialized to 0
int d[] = {1, 2, 3, 4, 5};        // Size inferred from initializer list
```

Partial initialization is a very important behavior—the C standard specifies that as long as an array is initialized (even if only one element is explicitly set), all elements not explicitly assigned are automatically initialized to the zero value for that type. Therefore, `int arr[10] = {0};` has become the idiomatic way to zero out an array, much cleaner than writing a loop manually.

### Designated Initializers (C99)

C99 introduced a very practical feature: the designated initializer. It allows you to specify "which position initializes to what value," with the remaining positions automatically filled with zero. This is particularly useful when dealing with sparse arrays, configuration tables, or register mappings:

```c
int config[10] = {
    [2] = 100,  // Index 2 is set to 100
    [5] = 200,  // Index 5 is set to 200
    [9] = 300   // Index 9 is set to 300
    // All other indices are 0
};
```

Honestly, designated initializers are used very frequently in embedded development. For example, if you have an interrupt vector table or a command dispatch table where most entries are empty and only a few need to be filled—code written with designated initializers is both clean and less error-prone. C++ only officially supported designated initializers in C++20 (and with some restrictions), so this feature has a more distinct advantage in pure C code.

## Step 2 — Understand the Memory Layout of Multi-dimensional Arrays

Multi-dimensional arrays are essentially "arrays of arrays." `int matrix[3][4]` declares an array of 3 elements, where each element is an array of 4 `int`s. This sounds like a tongue twister, but it accurately describes the memory layout.

### Row-Major Storage

C language multi-dimensional arrays are stored in **row-major** order in memory, meaning the rightmost subscript changes the fastest. For `int matrix[3][4]`, the memory arrangement looks like this:

```mermaid
graph LR
    subgraph Row0
    A0[matrix[0][0]] --> A1[matrix[0][1]] --> A2[matrix[0][2]] --> A3[matrix[0][3]]
    end
    subgraph Row1
    B0[matrix[1][0]] --> B1[matrix[1][1]] --> B2[matrix[1][2]] --> B3[matrix[1][3]]
    end
    subgraph Row2
    C0[matrix[2][0]] --> C1[matrix[2][1]] --> C2[matrix[2][2]] --> C3[matrix[2][3]]
    end

    A3 --> B0
    B3 --> C0
```

Let's verify this:

```c
#include <stdio.h>

int main(void) {
    int matrix[3][4] = {
        {1,  2,  3,  4},
        {5,  6,  7,  8},
        {9, 10, 11, 12}
    };

    // Print addresses to show linear layout
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            printf("&matrix[%d][%d] = %p, value = %d\n",
                   i, j, (void*)&matrix[i][j], matrix[i][j]);
        }
    }
    return 0;
}
```

You can see that the memory is completely linear—`matrix[0][3]` is immediately followed by `matrix[1][0]`. Understanding this is crucial because many performance optimizations (such as cache-friendly access) are built upon this foundation: traversing by rows is much faster than by columns because continuous memory access utilizes CPU cache lines better.

### Initializing Multi-dimensional Arrays

Initializing multi-dimensional arrays is similar to one-dimensional ones, just with nested braces:

```c
int matrix[3][4] = {
    {1, 2, 3, 4},
    {5, 6, 7, 8},
    {9, 10, 11, 12}
};

// Partial initialization
int sparse[3][4] = {
    {1},          // Only first element of row 0 is set
    {0, 5},       // First two elements of row 1
    {0, 0, 0, 9}  // Explicitly set row 2
};
```

### Multi-dimensional Arrays as Function Parameters

When passing a two-dimensional array to a function, the compiler must know the size of the second dimension (and higher dimensions) to correctly calculate address offsets. This is because the address calculation formula for `matrix[i][j]` is `base + i * N + j`, where `N` is the size of the second dimension. If the compiler doesn't know `N`, it cannot generate correct addressing code:

```c
// Correct: Column size is explicitly specified
void print_matrix(int rows, int cols, int matrix[rows][cols]);

// Incorrect: Compiler doesn't know how to calculate offset for matrix[i][j]
// void print_matrix(int rows, int cols, int matrix[][]);
```

If you want a function to accept two-dimensional arrays with different column counts, you have to abandon the direct 2D array syntax and use a one-dimensional array with manual index calculation, or use an array of pointers. This is indeed a trade-off between flexibility and type safety.

## Step 3 — Recognize the Pros and Cons of Variable Length Arrays (VLA)

C99 introduced Variable Length Arrays (VLA), allowing runtime variables to be used as the array size. Note that "variable length" here doesn't mean the array size can change dynamically—once created, the size is fixed—but rather that the determination of the size is delayed until runtime:

```c
void demo_vla(int n) {
    int arr[n]; // Size determined at runtime, allocated on stack

    for (int i = 0; i < n; i++) {
        arr[i] = i * 2;
    }
}
```

VLAs can also be used in two dimensions, which is particularly convenient in function parameters:

```c
// VLA in function parameters
void process_matrix(int rows, int cols, int matrix[rows][cols]) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            matrix[i][j] *= 2;
        }
    }
}
```

You see, in `process_matrix`, the size of `matrix` depends on the preceding parameters `rows` and `cols`. This solves the "2D array parameters must have fixed column count" problem mentioned earlier.

### Limitations and Controversies of VLA

VLAs sound great, but they have several issues that make them unpopular in industry.

First, VLAs are allocated on the stack. Stack space is usually limited (8MB default on Linux, maybe only a few KB in embedded systems). If the user inputs a very large number—say `int arr[1000000]`—you might blow the stack instantly, with no means of recovery. Unlike `malloc` returning `NULL` which you can handle, stack overflow is straight-up undefined behavior.

> ⚠️ **Warning**
> VLAs are allocated on the stack, have unpredictable sizes, and offer no recovery mechanism upon allocation failure—it is directly undefined behavior. In the embedded field, MISRA-C explicitly prohibits the use of VLAs. If you need an array with a size determined at runtime, using `malloc` and checking the return value is the safe approach.

Second, C11 demoted VLA from a mandatory feature to an optional one—compilers can claim not to support VLA and inform you via the macro `__STDC_NO_VLA__`. This means you cannot rely on VLAs being available on all C11 compilers.

In the embedded field, VLAs are basically prohibited. Static analysis tools (like MISRA-C) will typically explicitly ban VLAs because their unpredictable size conflicts entirely with the requirements for real-time performance and deterministic memory usage.

My advice is: know that VLAs exist and be able to read VLA code written by others, but when writing your own code, prioritize fixed-size arrays or `malloc`. In scenarios where flexible sizing is needed and dynamic allocation is acceptable, `malloc` + boundary checks are much safer than VLAs.

## Step 4 — Understand the Fundamental Limitations of Arrays

Arrays in C have several fundamental limitations. Understanding these limitations is key to grasping the design motivation behind C++ containers later.

### Arrays Are Not Assignable

After declaring two arrays, you cannot assign one array directly to another:

```c
int a[5] = {1, 2, 3, 4, 5};
int b[5];
a = b; // Error: invalid array assignment
```

The reason is that the array name in an assignment expression decays into a pointer to the first element, and the left side of the assignment operator must be a modifiable lvalue—the decayed pointer is an rvalue and cannot be assigned to. So to copy an array, you must copy element by element or use `memcpy`:

```c
memcpy(b, a, sizeof(a)); // Copies the entire array content
```

### Arrays Cannot Be Function Return Values

Functions cannot return array types. You cannot write a signature like `int[10] func()`. If you want to "return" an array from a function, there are three common approaches: return a pointer (pointing to a static array or a dynamically allocated array), pass an array out via a parameter, or wrap the array in a struct and return that. The last method is actually quite practical—C allows struct assignment and return values, and structs can contain arrays:

```c
struct MatrixWrapper {
    int data[16]; // 4x4 matrix
};

struct MatrixWrapper get_identity_matrix(void) {
    struct MatrixWrapper m = {0};
    for (int i = 0; i < 4; i++) {
        m.data[i * 4 + i] = 1; // Set diagonal to 1
    }
    return m; // The whole struct (including the array) is copied
}
```

This trick can also be seen in the C standard library's math functions (returning complex numbers, `struct div_t` return structures, etc.).

### Array Size Must Be a Compile-Time Constant (Except for VLA)

The size of a normal array must be determined at compile time. `int arr[n];` (where `n` is a variable) is illegal in C89—only C99 VLAs allow this. And VLAs have the issues mentioned above. This means that in C89 or environments without VLA support, if you want to create arrays of different sizes based on runtime data, you have to use `malloc`.

## Step 5 — Precisely Distinguish Between Arrays and Pointers

In the crash course and pointer chapters, we said "array names decay to pointers." This statement is fine, but it easily leads people to think "arrays are pointers"—which is incorrect. Arrays are arrays, pointers are pointers; they can only be converted to each other in specific situations.

### When Does an Array Name Decay to a Pointer?

An array name decays into a pointer to its first element in the following scenarios: when passed as a function argument, used in arithmetic operations, or used in an expression (most cases). But there are three exceptions—array names do **not** decay in `sizeof` (C11), `_Alignof` (C11), and as the operand of the address-of operator `&`:

```c
#include <stdio.h>

int main(void) {
    int arr[10];

    printf("%zu\n", sizeof(arr));    // 40 (10 * 4), does NOT decay
    printf("%zu\n", sizeof(&arr[0])); // 8 (pointer size on 64-bit), decayed
    printf("%p\n", (void*)&arr);      // Address of the whole array
    printf("%p\n", (void*)arr);       // Address of first element (same value, different type)

    return 0;
}
```

`sizeof(arr)` returns 40 while `sizeof(&arr[0])` returns 8—this is the most direct evidence that an array is not a pointer.

### Pointer Arithmetic vs Array Subscripting

`arr[i]` and `*(arr + i)` are completely equivalent—the C language array subscript operator `[]` is essentially syntactic sugar for pointer arithmetic. And this equivalence is commutative: `i[arr]` is equivalent to `arr[i]`. Yes, `3[arr]` is legal C code, completely equivalent to `arr[3]`. Of course, do not use this style in actual projects—it has no benefit other than showing off and will send your colleagues' blood pressure soaring.

### Two-dimensional Arrays vs Arrays of Pointers

This is a classic point of confusion. `int matrix[3][4]` and `int *rows[3]` might both seem accessible with `rows[i][j]` and `matrix[i][j]`, but their memory models are completely different:

```c
int matrix[3][4];     // Continuous memory, 3x4 integers
int *rows[3];         // Array of 3 pointers, each pointing to an array
```

A two-dimensional array is a single contiguous block of memory; the compiler calculates addresses directly via the row-column formula. An array of pointers introduces a level of indirection—first find the pointer for row `i`, then use that pointer to find the `j`-th element. Therefore, `matrix` cannot be passed to a function accepting an `int **` parameter, and vice versa. Their types are incompatible, and forcing a cast will lead to undefined behavior.

> ⚠️ **Warning**
> `int matrix[3][4]` and `int *rows[3]` may both be accessible with `[i][j]`, but their memory models are completely different—the former is contiguous memory, the latter has an indirection layer. Never pass a 2D array to an `int **` parameter; the compiler might let it slide, but the address calculation at runtime will be completely wrong.

## C++ Transition

Now that we understand the limitations of C arrays, let's see how C++ solves them one by one.

### `std::array` — Assignable Fixed-Size Array

`std::array` is a fixed-size array container introduced in C++11. It allocates memory on the stack (just like a C array) but patches up the missing features of C arrays: it can be assigned, copied, used as a function return value, and knows its own size:

```cpp
#include <array>
#include <algorithm>

void demo_std_array() {
    std::array<int, 5> a = {1, 2, 3, 4, 5};
    std::array<int, 5> b;

    b = a; // OK: Assignment is supported

    // Can be returned from functions
    auto get_array() -> std::array<int, 5> {
        return a;
    }

    // Knows its own size
    size_t sz = a.size(); // 5
}
```

`std::array` has zero overhead—it doesn't introduce extra memory or runtime costs, and compiler optimizations make it just as fast as a raw C array. If you need a fixed-size array in C++, there is no reason to use a raw array instead of `std::array`.

### `std::vector` — Dynamic Size Array

`std::vector` solves the "size determined only at runtime" problem. It allocates memory on the heap, can grow and shrink dynamically, and manages the memory lifecycle automatically:

```cpp
#include <vector>

void demo_vector() {
    // Size determined at runtime
    int n = 100;
    std::vector<int> vec(n);

    // Dynamic growth
    vec.push_back(42);

    // Automatic memory management
    // No need to manually free
}
```

`std::vector` can be seen as a safe alternative to VLAs—variable size, throws exceptions on allocation failure (instead of the undefined behavior of stack overflow), has boundary checks (`at` method), and automatic memory release. The only cost is a small amount of heap allocation overhead, but in the vast majority of scenarios, this cost is completely acceptable.

### Range-based For Loop

Traversing a C array requires either subscripts or pointer arithmetic, both requiring manual boundary management. The range-based for loop introduced in C++11 makes traversal very concise, and both `std::array` and `std::vector` support it:

```cpp
std::vector<int> vec = {1, 2, 3, 4, 5};

// Range-based for
for (int& val : vec) {
    val *= 2;
}
```

It is worth noting that the range-based for loop can also be used for raw C arrays (as long as the array size is visible in the current scope), but its use cases are limited—once an array decays to a pointer, the size information is lost, and the range-based for loop can no longer be used. This is another advantage of `std::array` over raw arrays.

## Summary

The memory model of arrays is actually not complex—it's just a contiguous arrangement of elements of the same type. One-dimensional arrays have diverse initialization methods, and C99's designated initializers are particularly useful for sparse data. Multi-dimensional arrays are "arrays of arrays," stored in row-major order; understanding this is important for performance optimization. While VLAs are convenient, they carry the risk of stack overflow and are generally not recommended in industry and embedded fields. Arrays have several fundamental limitations—cannot be assigned, cannot be used as function return values—limitations that `std::array` in C++ solves perfectly. While arrays and pointers are interchangeable in most scenarios, they are fundamentally different types—`sizeof` and `&` are where the differences are most exposed. Understanding these underlying details allows you to appreciate the motivation behind every design decision when learning C++ containers later.

### Key Takeaways

- [ ] Elements not specified during partial initialization are automatically filled with zero; `int arr[10] = {0};` is the idiomatic way to zero an array.
- [ ] C99 designated initializers allow initialization by position, suitable for sparse data and configuration tables.
- [ ] Multi-dimensional arrays are stored contiguously in row-major order; the address of `matrix[i][j]` is `base + i * N + j`.
- [ ] VLAs are allocated on the stack, have unpredictable sizes, and were demoted to an optional feature in C11.
- [ ] Arrays cannot be assigned or used as function return values, but wrapping them in a struct allows this.
- [ ] Array names do not decay to pointers in `sizeof` and `&` operands.
- [ ] `std::array` is a zero-overhead fixed-size container supporting assignment and copying.
- [ ] `std::vector` is a dynamic-size container and a safe alternative to VLAs.

## Exercises

### Exercise 1: Matrix Operations

Implement the following three functions to perform basic matrix operations. Matrices are represented using standard C two-dimensional arrays. Please implement matrix transposition and matrix multiplication yourself:

```c
#include <stdio.h>

// Transpose a matrix: result[j][i] = matrix[i][j]
void transpose(int rows, int cols, int matrix[rows][cols], int result[cols][rows]);

// Multiply two matrices: result = a * b
void multiply(int a_rows, int a_cols,
              int a[a_rows][a_cols],
              int b[a_rows][a_cols], // Assuming a_cols == b_rows
              int result[a_rows][a_cols]);

// Print a matrix
void print_matrix(int rows, int cols, int matrix[rows][cols]);
```

**Hint:** The core of transposition is `result[j][i] = matrix[i][j]`. The core of multiplication is a triple loop—`result[i][j]` is the dot product of the `i`-th row of `a` and the `j`-th column of `b`. The function parameters here use VLA syntax to allow column counts to be specified dynamically.

### Exercise 2: Compare VLA and malloc

Write a program that uses VLA and `malloc` respectively to allocate an integer array whose size is determined by user input, then compare the behavioral differences:

```c
#include <stdio.h>
#include <stdlib.h>

void test_vla(int n);
void test_malloc(int n);
void print_array(int *arr, int n);
```

Please implement these two functions and the `print_array` function yourself. Consider the following questions: If the user inputs a very large number (e.g., 100000000), what happens in each case? Which method can handle allocation failure gracefully? Which would you choose in an embedded system?

## References

- [Array declaration and initialization - cppreference](https://en.cppreference.com/w/c/language/array_initialization)
- [Variable length arrays - cppreference](https://en.cppreference.com/w/c/language/array#Variable-length_arrays)
- [std::array - cppreference](https://en.cppreference.com/w/cpp/container/array)
- [std::vector - cppreference](https://en.cppreference.com/w/cpp/container/vector)
