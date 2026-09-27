---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the declaration, initialization, and multidimensional use of C-style
  arrays, and understand array decay and its impact on passing arrays to functions.
difficulty: beginner
order: 1
platform: host
prerequisites:
- Smart Pointer Preview
reading_time_minutes: 13
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: C-Style Arrays
translation:
  source: documents/vol1-fundamentals/ch05/01-c-arrays.md
  source_hash: 6940fabd3d4bb448e235a87f147b1ed027bbb4721d9587225db03384655462de
  translated_at: '2026-09-25T10:51:43+00:00'
  engine: anthropic
  token_count: 4500
---
# C-Style Arrays: Raw, Direct, and No Protection Whatsoever

Up to this point, the way we have handled data has been "one variable holds one value." But real-world data rarely exists in isolation—a batch of sensor readings, a string of characters, a matrix, a grade sheet: these things are inherently "a bunch of values of the same type lined up in a row." The array is the most primitive mechanism C and C++ offer for storing this kind of "contiguous data of the same type."

C-style arrays come with plenty of problems: they cannot be assigned, cannot be returned, lose length information when passed as arguments, and have no bounds checking. But they are a superb entry point for understanding memory layout—only once we understand these pain points can we understand why C++ introduced `std::array`. Let's take C-style arrays apart from the inside out.

## Declaration and Initialization—What an Array Looks Like

To declare an array, the core syntax is a pair of square brackets after the variable name, containing the number of elements:

```cpp
int scores[5];  // 5 ints, uninitialized (the values are indeterminate)
```

This code tells the compiler: allocate space for 5 `int`s contiguously on the stack. Note that **an uninitialized local array holds garbage values**—not zeros. So we almost always initialize at the same time we declare.

```cpp
int scores[5] = {90, 85, 78, 92, 88};
```

These five values are poured into the array's five positions in order. If we supply fewer initializers than the array size, the remaining elements are automatically initialized to zero:

```cpp
int data[5] = {10, 20};  // data = {10, 20, 0, 0, 0}
```

The other way around—if we supply more initializers than the array size, compilation fails outright.

When the initializer list provides enough values, we can omit the size and let the compiler count them itself:

```cpp
int primes[] = {2, 3, 5, 7, 11, 13};  // the compiler deduces the size as 6
```

The benefit of this style: when we add or remove elements later on, there is no number inside the brackets that needs updating in lockstep.

When we want to know how many elements an array holds, there is a classic formula:

```cpp
int primes[] = {2, 3, 5, 7, 11, 13};
constexpr int kCount = sizeof(primes) / sizeof(primes[0]);  // kCount = 6
```

`sizeof(primes)` is the number of bytes the whole array occupies, `sizeof(primes[0])` is the number of bytes a single element occupies, and dividing the two gives the element count. This trick is everywhere in C code, but we will get to its limitations later.

## Accessing Elements: Subscripts Start at 0

First, let's burn this in: C++ array subscripts start at 0. For an array of size 5, the valid subscripts are 0 through 4. This design is not arbitrary: under the hood, `arr[i]` is equivalent to `*(arr + i)`—the position reached by offsetting `i` elements past the array's starting address.

```cpp
int scores[5] = {90, 85, 78, 92, 88};

std::cout << scores[0] << std::endl;  // 90 (the first element)
std::cout << scores[4] << std::endl;  // 88 (the last element)
```

C-style arrays perform no bounds checking whatsoever. Out-of-bounds accesses like `scores[5]`, `scores[100]`, or `scores[-1]` draw no compile-time error and throw no exception at runtime—they silently read and write memory beyond the array. This undefined behavior may happen to "look fine," may crash immediately, or may quietly change the values of other variables. Debugging this kind of issue is a genuine blood-pressure test.

We modify array elements through subscripts too:

```cpp
scores[2] = 80;  // change the third element from 78 to 80
```

There are several ways to traverse an array: the most traditional is a subscript loop, while the range-based `for` introduced in C++11 is more concise.

```cpp
// Range-based for traversal (only valid within the declaring scope)
for (int s : scores) {
    std::cout << s << " ";
}
// Output: 90 85 80 92 88
```

The range-based `for` only works on arrays that "know their own size"—once the array has been passed to a function, it stops working, and we will explain why later.

## Multidimensional Arrays: Stored Row by Row, Contiguously

C++ supports multidimensional arrays, which are essentially "arrays of arrays." The one we use most is the two-dimensional array, for representing matrices or tables:

```cpp
int matrix[3][4] = {
    {1,  2,  3,  4},
    {5,  6,  7,  8},
    {9, 10, 11, 12}
};
```

We have declared a matrix with 3 rows and 4 columns. `matrix[0]` is the first row (itself an array of 4 `int`s), and `matrix[0][2]` is the third element of the first row, with the value 3.

Now a key question: what does this matrix look like in memory? The answer is **row-major contiguous storage**—all the elements are packed tightly into one contiguous block of memory:

```text
Address:  low →→→→→→→→→→→→→→→→→→→→→→→ high
Content:  1 2 3 4 5 6 7 8 9 10 11 12
          ↑--- row 0 ---↑--- row 1 ---↑--- row 2 ---↑
```

`matrix[1][0]` sits in memory immediately after `matrix[0][3]`. This point is crucial when we study the relationship between pointers and arrays later on.

We traverse a two-dimensional array with nested loops:

```cpp
for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 4; ++j) {
        std::cout << matrix[i][j] << "\t";
    }
    std::cout << std::endl;
}
```

Output:

```text
1  2  3  4
5  6  7  8
9 10 11 12
```

Here is a performance detail: because memory is laid out row by row, iterating rows in the outer loop and columns in the inner loop is the most cache-friendly order. If we swap the two loops, every access the CPU makes jumps around in memory, cache hit rates plummet, and on large data sets the difference can reach several-fold.

## Passing Arrays to Functions: The Biggest Pitfall Lives Here

Now we arrive at the biggest pitfall of C-style arrays: when an array is passed to a function, it **decays**.

```cpp
void print_array(int arr[])
{
    std::cout << "sizeof(arr) = " << sizeof(arr) << std::endl;
}

int main()
{
    int data[5] = {1, 2, 3, 4, 5};
    std::cout << "sizeof(data) = " << sizeof(data) << std::endl;
    print_array(data);
    return 0;
}
```

Output:

```text
sizeof(data) = 20
sizeof(arr) = 8
```

Inside `main`, `sizeof(data)` is 20 (5 `int`s at 4 bytes each). But inside the function, `sizeof(arr)` has become 8—that is the size of a pointer on a 64-bit system, not the size of the array.

What we are seeing is array decay: when passed as an argument, an array automatically decays into a pointer to its first element, and `int arr[]` in a function signature is completely equivalent to `int* arr`.

Array decay means the function has completely lost the array's size information. We cannot compute the element count with `sizeof`, and we cannot traverse the array with a range-based `for` loop. If we write `sizeof(arr) / sizeof(arr[0])` inside the function, what we get is not the array length but the meaningless result of "one pointer divided by one int." That is why C-style functions almost always require us to pass the array length in as an extra parameter.

So the right approach is to pass the size explicitly:

```cpp
void print_array(const int arr[], int size)
{
    for (int i = 0; i < size; ++i) {
        std::cout << arr[i] << " ";
    }
    std::cout << std::endl;
}
```

We mark it `const` because the function only reads and never modifies—a good habit, since the compiler will then flag any accidental modification.

### Passing Multidimensional Arrays

Passing multidimensional arrays is even more troublesome: we must tell the compiler the size of the second (and any higher) dimension, otherwise it cannot compute element addresses:

```cpp
// The compiler needs to know the second dimension is 4 to compute the address of matrix[i][j] correctly
void print_matrix(int matrix[][4], int rows)
{
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < 4; ++j) {
            std::cout << matrix[i][j] << "\t";
        }
        std::cout << std::endl;
    }
}
```

This directly means the function only accepts arrays whose second dimension is exactly 4—hand it a 3x3 matrix and it is useless. This is one of the reasons C-style arrays are so painful to use in real projects.

## C Arrays vs. the Modern Replacements

After all this, we have felt the pain points of C-style arrays firsthand: they cannot be assigned directly (`int b[3] = a;` gets flat-out rejected by the compiler); they cannot be returned from functions, and returning a pointer to a local array is even more dangerous, because the memory is already dead once the stack frame is reclaimed; they decay into pointers and lose their size information; and their length must be fixed at compile time—no runtime sizing.

C-style arrays hold one more easily overlooked trap: we cannot deduce an array type with `auto`. `auto a = {1,2,3};` deduces `std::initializer_list<int>`, not an array. `auto b = arr;` (where `arr` is an array) deduces a pointer, not a copy of the array. These implicit behaviors are all tied to array decay—one moment of carelessness and we have written code that behaves nothing like what we expected.

These problems are exactly why C++11 introduced `std::array`: it allocates memory on the stack (just like a C array) but offers modern features such as assignment, comparison, range-based `for`, and `.size()`, and it never decays into a pointer. Understanding C-style arrays still matters, though, because you will keep running into them in legacy code, C libraries, and embedded code.

## Hands-On Practice—arrays.cpp

Let's fold this chapter's core points into a single program:

```cpp
// arrays.cpp
// A comprehensive tour of C-style arrays: initialization, traversal, function parameters, matrix operations

#include <iostream>

/// @brief Print a one-dimensional array
void print_array(const int arr[], int size)
{
    for (int i = 0; i < size; ++i) {
        std::cout << arr[i];
        if (i < size - 1) {
            std::cout << ", ";
        }
    }
    std::cout << std::endl;
}

/// @brief Compute the sum of the array elements
int array_sum(const int arr[], int size)
{
    int total = 0;
    for (int i = 0; i < size; ++i) {
        total += arr[i];
    }
    return total;
}

/// @brief Print a matrix (second dimension fixed at 4)
void print_matrix(const int matrix[][4], int rows)
{
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < 4; ++j) {
            std::cout << matrix[i][j] << "\t";
        }
        std::cout << std::endl;
    }
}

/// @brief Transpose a 3x4 matrix into a 4x3 matrix
void transpose_3x4(const int src[][4], int dst[][3])
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            dst[j][i] = src[i][j];
        }
    }
}

int main()
{
    // --- Initialization styles ---
    std::cout << "=== 初始化方式 ===" << std::endl;

    int full_init[5] = {10, 20, 30, 40, 50};
    std::cout << "完全初始化: ";
    print_array(full_init, 5);

    int partial_init[5] = {1, 2};  // the rest are filled with 0 automatically
    std::cout << "部分初始化: ";
    print_array(partial_init, 5);

    int zero_init[5] = {};  // all zeros
    std::cout << "零初始化:   ";
    print_array(zero_init, 5);

    int deduced[] = {2, 3, 5, 7, 11, 13};
    constexpr int kDeducedCount = sizeof(deduced) / sizeof(deduced[0]);
    std::cout << "大小推断:   ";
    print_array(deduced, kDeducedCount);
    std::cout << std::endl;

    // --- Traversal and summing ---
    std::cout << "=== 遍历与求和 ===" << std::endl;
    int scores[] = {90, 85, 78, 92, 88};
    constexpr int kScoreCount = sizeof(scores) / sizeof(scores[0]);

    std::cout << "成绩: ";
    print_array(scores, kScoreCount);

    int total = array_sum(scores, kScoreCount);
    double average = static_cast<double>(total) / kScoreCount;
    std::cout << "总分: " << total << std::endl;
    std::cout << "均分: " << average << std::endl;
    std::cout << std::endl;

    // --- Matrix operations ---
    std::cout << "=== 矩阵操作 ===" << std::endl;
    int matrix[3][4] = {
        {1,  2,  3,  4},
        {5,  6,  7,  8},
        {9, 10, 11, 12}
    };

    std::cout << "原始矩阵 (3x4):" << std::endl;
    print_matrix(matrix, 3);

    int transposed[4][3] = {};
    transpose_3x4(matrix, transposed);

    std::cout << std::endl << "转置矩阵 (4x3):" << std::endl;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 3; ++j) {
            std::cout << transposed[i][j] << "\t";
        }
        std::cout << std::endl;
    }

    return 0;
}
```

Compile and run: `g++ -std=c++17 -Wall -Wextra -o arrays arrays.cpp && ./arrays`

Expected output:

```text
=== 初始化方式 ===
完全初始化: 10, 20, 30, 40, 50
部分初始化: 1, 2, 0, 0, 0
零初始化:   0, 0, 0, 0, 0
大小推断:   2, 3, 5, 7, 11, 13

=== 遍历与求和 ===
成绩: 90, 85, 78, 92, 88
总分: 433
均分: 86.6

=== 矩阵操作 ===
原始矩阵 (3x4):
1  2  3  4
5  6  7  8
9  10 11 12

转置矩阵 (4x3):
1  5  9
2  6  10
3  7  11
4  8  12
```

Let's double-check: 90 + 85 + 78 + 92 + 88 = 433, average 86.6—checks out. After the transpose, row 0 became column 0—correct.

## Try It Yourself

Reading without practicing is the same as not learning—work through every exercise yourself.

### Exercise 1: Array Sum and Average

Write a program that declares an array of 10 integers and two functions that compute the total and the average respectively (the average returns a `double`). How to verify: add the numbers up by hand and compare with the program's output.

::: details Reference answer

```cpp
#include <iostream>

constexpr int sum(const int a[], int n)
{
    int total = 0;

    for (int i = 0; i < n; i++)
    {
        total += a[i];
    }

    return total;
}

constexpr double average(const int a[], int n)
{
    if (n == 0 || n < 0)
        return 0.0;

    return static_cast<double>(sum(a, n)) / n;
}

int main()
{
    constexpr int arr[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    constexpr int size = sizeof(arr) / sizeof(arr[0]);

    constexpr int total = sum(arr, size);
    constexpr double avg = average(arr, size);

    std::cout << "总和: " << total << std::endl;
    std::cout << "平均值: " << avg << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Result:

```text
总和: 55
平均值: 5.5
```

:::

### Exercise 2: Matrix Transpose

Write a function that transposes an N × M two-dimensional array into M × N. First implement it with fixed sizes (2×3 into 3×2), then think about this: if the row and column counts must be passed in at runtime, how should standard C++ represent that matrix memory?

::: details Reference answer

**2x3 transposed into 3x2**

```cpp
constexpr void transpose_2x3(const int (&matrix)[2][3], int (&result)[3][2])
{
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            result[j][i] = matrix[i][j];
        }
    }
}
```

We can also look at the C99 VLA approach (not standard C++):

```c
void transpose(int m, int n, const int matrix[m][n], int result[n][m])
{
    for (int i = 0; i < m; i++)
    {
        for (int j = 0; j < n; j++)
        {
            result[j][i] = matrix[i][j];
        }
    }
}
```

Note that the function parameters above use **variable-length array** (VLA) syntax: the bounds of `matrix[m][n]` and `result[n][m]` are determined by runtime parameters. This is a C99 feature (C11 made VLAs optional), but it is not standard C++ syntax; even when a C++ compiler accepts it, it is a compiler extension and must not go into portable C++ code.

**The standard C++ way to handle runtime sizes**

In standard C++, the bounds of a built-in array are part of its type, so we cannot express runtime sizes through ordinary function parameters. That is, "rows and columns as parameters" does not mean C-style arrays cannot take part in function calls at all—it means we cannot spell the parameter directly in VLA form as `int matrix[m][n]`. One simple, portable approach is to store the matrix row-contiguously in a one-dimensional array and pass the row and column counts alongside:

```cpp
void transpose(int rows, int cols, const int* matrix, int* result)
{
    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            result[col * rows + row] = matrix[row * cols + col];
        }
    }
}
```

The caller must guarantee that `matrix` holds at least `rows * cols` elements and that `result` holds at least as many. If we want to keep the two-dimensional subscript form, we can use templates to express compile-time-fixed row and column counts, or reach for types like `std::vector` or `std::span` that are better suited to runtime sizes.

:::

### Exercise 3: Fix an Out-of-Bounds Bug

The code below contains an out-of-bounds bug—find it and fix it:

```cpp
int data[5] = {10, 20, 30, 40, 50};
for (int i = 0; i <= 5; ++i) {  // Hint: look closely at the loop condition
    std::cout << data[i] << std::endl;
}
```

::: details Reference answer

```cpp
int data[5] = {10, 20, 30, 40, 50};
for (int i = 0; i < 5; ++i) {
    std::cout << data[i] << std::endl;
}
```

:::

This kind of off-by-one error is remarkably easy to write when we are just starting out.
