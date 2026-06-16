---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the declaration, initialization, and multidimensional usage of
  C-style arrays, and understand array decay and its impact on function parameter
  passing.
difficulty: beginner
order: 1
platform: host
prerequisites:
- 智能指针预告
reading_time_minutes: 10
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: C-style array
translation:
  source: documents/vol1-fundamentals/ch05/01-c-arrays.md
  source_hash: dd254ebd95f09c207595816fc7b28f9450282e00098ce8b95cc72653bfa0d60e
  translated_at: '2026-06-16T03:43:28.087762+00:00'
  engine: anthropic
  token_count: 2099
---
# C-Style Arrays

So far, we have handled data in a "one variable, one value" manner. However, real-world data rarely exists in isolation—a set of sensor readings, a string of characters, a matrix, a grade table—these things are naturally "a pile of data of the same type lined up in a row." The array is the most primitive mechanism provided by C and C++ for storing this "contiguous homogeneous data."

C-style arrays have many issues—they cannot be assigned, cannot be returned, lose length information when passed as arguments, and have no bounds checking—but they are an excellent entry point for understanding memory layout. Only by understanding these pain points can we understand why C++ introduced `std::array`. In this chapter, we will dissect C-style arrays inside out.

## Declaration and Initialization — What Does an Array Look Like

To declare an array, the core syntax is to add square brackets after the variable name, specifying the number of elements inside:

```cpp
int scores[5];
```

This code tells the compiler: allocate space for 5 `int`s contiguously on the stack. Note that **uninitialized local arrays contain garbage values**—not zero. Therefore, we almost always initialize at the same time as declaration.

```cpp
int scores[5] = {90, 85, 78, 92, 88};
```

These five values are filled into the five positions of the array in order. If there are fewer initial values than the array size, the remaining elements are automatically initialized to zero:

```cpp
int scores[5] = {90, 85}; // {90, 85, 0, 0, 0}
```

Conversely, if the initial values exceed the array size, the compiler will error directly.

If the initialization list provides enough values, the array size can be omitted, letting the compiler count itself:

```cpp
int scores[] = {90, 85, 78, 92, 88}; // Size is 5
```

The benefit of this approach is that you don't need to synchronize the number in the square brackets when adding or removing elements later.

To know how many elements an array has, there is a classic formula:

```cpp
size_t n = sizeof(scores) / sizeof(scores[0]);
```

`sizeof(scores)` is the total bytes occupied by the whole array, and `sizeof(scores[0])` is the bytes occupied by a single element. Dividing them gives the number of elements. This trick is everywhere in C code, but we will discuss its limitations later.

## Accessing Elements — A Zero-Based World

C++ array indices start at 0. For an array of size 5, the valid indices are 0 to 4. This is not an arbitrary design choice—`scores[i]` is equivalent at the low level to `*(scores + i)`, which means the position offset by `i` elements from the array's starting address.

```cpp
int first = scores[0]; // 90
int third = scores[2]; // 78
```

> **Pitfall Warning**: C-style arrays perform no bounds checking. `scores[-1]`, `scores[5]`, `scores[100]`—these out-of-bounds accesses produce no errors at compile time and throw no exceptions at runtime—they silently read/write memory outside the array. This undefined behavior might coincidentally "look normal," might crash immediately, or might silently modify the values of other variables. Your blood pressure will really spike when debugging such issues.

Modifying array elements is also done via indices:

```cpp
scores[0] = 95; // Change the first score to 95
```

There are several ways to traverse an array. The most traditional is an index loop, but the range-based `for` loop introduced in C++11 is more concise:

```cpp
for (int val : scores) {
    std::cout << val << " ";
}
```

The range-based `for` loop can only be used for arrays that "know their own size"—it stops working once passed to a function, and we will explain why later.

## Multidimensional Arrays — The Memory Truth of Matrices

C++ supports multidimensional arrays, which are essentially "arrays of arrays." The most common is the two-dimensional array, used to represent matrices or tables:

```cpp
int matrix[3][4] = {
    {1, 2, 3, 4},
    {5, 6, 7, 8},
    {9, 10, 11, 12}
};
```

This code declares a matrix with 3 rows and 4 columns. `matrix[0]` is the first row (itself an array containing 4 `int`s), and `matrix[0][2]` is the third element of the first row, with a value of 3.

Key question: What does this matrix look like in memory? The answer is **stored contiguously by row** (row-major), with all elements tightly packed in a single contiguous block of memory:

```cpp
// Memory layout visualization:
// 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
```

`matrix[0][3]` is immediately adjacent to `matrix[1][0]` in memory. Understanding this is crucial for grasping the relationship between pointers and arrays later.

Traverse a 2D array with nested loops:

```cpp
for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 4; ++j) {
        std::cout << matrix[i][j] << " ";
    }
    std::cout << "\n";
}
```

Output:

```text
1 2 3 4
5 6 7 8
9 10 11 12
```

Here is a performance detail: because memory is stored by row, traversing rows in the outer loop and columns in the inner loop is the most cache-friendly approach. If you swap the inner and outer loops, the CPU will jump around in memory on every access, cache hit rates will plummet, and the difference in large-scale data can be several times.

## Passing Arrays — The Start of All Nightmares

Now we come to the biggest pitfall of C-style arrays: when passing an array to a function, it undergoes **decay**.

```cpp
#include <iostream>

void print_size(int arr[5]) {
    std::cout << "Inside function: " << sizeof(arr) << "\n";
}

int main() {
    int arr[5] = {1, 2, 3, 4, 5};
    std::cout << "In main: " << sizeof(arr) << "\n";
    print_size(arr);
}
```

Output:

```text
In main: 20
Inside function: 8
```

In `main`, `sizeof(arr)` is 20 (5 `int`s, 4 bytes each). But inside the function, `sizeof(arr)` becomes 8—this is the size of a pointer on a 64-bit system, not the size of the array.

This is array decay: when passed as an argument, an array automatically decays into a pointer to its first element. The function signatures `void func(int arr[5])` and `void func(int *arr)` are completely equivalent.

> **Pitfall Warning**: Array decay means the function completely loses the size information of the array. You can't use `sizeof` to calculate the number of elements, nor can you use a range-based `for` loop to traverse it. If you write `sizeof(arr) / sizeof(arr[0])` inside the function, you don't get the array length, but a meaningless result of "a pointer divided by an int." This is why C-style functions almost always require you to pass the array length as an extra argument.

So the correct way is to explicitly pass the size:

```cpp
void print_array(const int arr[], size_t size) {
    for (size_t i = 0; i < size; ++i) {
        std::cout << arr[i] << " ";
    }
}
```

We use `const` because the function only reads and does not modify, which is a good habit—the compiler will error if you accidentally modify it.

### Passing Multidimensional Arrays

Passing multidimensional arrays is more troublesome—you must tell the compiler the size of the second dimension (and higher), otherwise the compiler cannot calculate element addresses:

```cpp
void print_matrix(int matrix[][4], size_t rows) {
    // ...
}
```

This directly limits the function to only accept arrays where the second dimension is exactly 4; a 3x3 matrix won't work. This is one of the reasons why C-style arrays are very difficult to use in actual projects.

## C Arrays vs Modern Alternatives

Having said all that, we have felt the various pain points of C-style arrays. They cannot be assigned directly—`int a[5] = b;` is rejected by the compiler; they cannot be used as function return values—returning a pointer to a local array is even more dangerous because the memory is invalid after the stack frame is reclaimed; they decay to pointers and lose size information; their length must be determined at compile time, supporting no runtime dynamic sizing.

> **Pitfall Warning**: C-style arrays have another easily overlooked trap—you cannot use `auto` to deduce an array type. `auto arr = array` deduces to `int*`, not an array. `template<typename T> void foo(T t)` (where `T` is an array) deduces `T` as a pointer, not a copy of the array. These implicit behaviors are all related to array decay; if you are not careful, you will write code that behaves completely differently from expectations.

These problems are exactly the reason C++11 introduced `std::array`—it allocates memory on the stack (just like C arrays), but provides modern features like assignment, comparison, range-based `for` loops, and `.size()`, and it does not decay to a pointer. But understanding C-style arrays remains important because you will constantly encounter them in legacy code, C language libraries, and embedded code.

## Practical Exercise — arrays.cpp

Integrate the core knowledge points of this chapter into one program:

```cpp
#include <iostream>
#include <stddef.h> // for size_t

// Calculate average
double calculate_average(const int arr[], size_t size) {
    if (size == 0) return 0.0;
    long sum = 0;
    for (size_t i = 0; i < size; ++i) {
        sum += arr[i];
    }
    return static_cast<double>(sum) / size;
}

// Transpose a 3x4 matrix to 4x3
void transpose_matrix(const int src[3][4], int dst[4][3]) {
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 4; ++j) {
            dst[j][i] = src[i][j];
        }
    }
}

int main() {
    // 1. Basic array initialization
    int scores[] = {90, 85, 78, 92, 88};
    size_t n = sizeof(scores) / sizeof(scores[0]);

    // 2. Calculate average
    double avg = calculate_average(scores, n);
    std::cout << "Average score: " << avg << "\n";

    // 3. Matrix transposition
    int matrix[3][4] = {
        {1, 2, 3, 4},
        {5, 6, 7, 8},
        {9, 10, 11, 12}
    };
    int transposed[4][3];
    transpose_matrix(matrix, transposed);

    std::cout << "Transposed matrix:\n";
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            std::cout << transposed[i][j] << " ";
        }
        std::cout << "\n";
    }

    return 0;
}
```

Compile and run: `g++ -std=c++17 arrays.cpp && ./a.out`

Expected output:

```text
Average score: 86.6
Transposed matrix:
1 5 9
2 6 10
3 7 11
4 8 12
```

Verify: 90 + 85 + 78 + 92 + 88 = 433, average 86.6, correct. After matrix transposition, row 0 becomes column 0, correct.

## Try It Yourself

Reading without practicing is like not learning. It is recommended to write each question by hand.

### Exercise 1: Array Sum and Average

Write a program that declares an array containing 10 integers, and write two functions to calculate the sum and the average (return the average as `double`). Verification method: manually add them up once and compare with the program output.

### Exercise 2: Matrix Transposition

Write a function to transpose an N x M two-dimensional array into M x N. First implement it with a fixed size (2x3 transposed to 3x2), then think: can C-style arrays handle it if the number of rows and columns also needs to be parameters?

### Exercise 3: Fix the Out-of-Bounds Bug

The following code has an out-of-bounds access bug. Find it and fix it:

```cpp
int arr[5] = {1, 2, 3, 4, 5};
for (int i = 0; i <= 5; ++i) { // Bug here
    std::cout << arr[i] << "\n";
}
```

This off-by-one error is very common in beginner code.

## Summary

In this chapter, we dissected C-style arrays. Arrays are stored contiguously in memory, indices start at 0, and `sizeof` can get the number of elements (but only valid within the declaration scope). Multidimensional arrays are stored contiguously by row, and row-major traversal is more cache-friendly. Arrays decay to pointers when passed as arguments, losing size information. They cannot be assigned, returned, or bounds-checked—these pain points are the very reason `std::array` exists.

In the next chapter, we will look at `std::array`—the modern alternative that retains the performance advantages of C arrays while fixing all the shortcomings.
