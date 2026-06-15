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
reading_time_minutes: 11
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: C-style arrays
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/ch05/01-c-arrays.md
  source_hash: 11eb714f9ef673a96f34c3bae81fc3cb6cf8b4ac8e081c9845538c04c7288308
  token_count: 2103
  translated_at: '2026-05-26T10:49:09.692572+00:00'
---
# C-Style Arrays

So far, we have handled data in a "one variable, one value" fashion. But real-world data rarely exists in isolation—a set of sensor readings, a string of characters, a matrix, or a grade table are all naturally "a bunch of same-type data lined up in a row." Arrays are the most primitive mechanism provided by C and C++ for storing this kind of homogeneous, contiguous data.

C-style arrays come with many problems—they cannot be assigned, cannot be returned, lose their length information when passed as arguments, and lack bounds checking. However, they serve as an excellent entry point for understanding memory layout. Only by grasping these pain points can we understand why C++ introduced `std::array`. In this chapter, we will take C-style arrays apart from the inside out.

## Declaration and Initialization—What an Array Looks Like

To declare an array, the core syntax is adding square brackets after the variable name, with the number of elements inside:

```cpp
int scores[5];  // 5 个 int，未初始化（值是不确定的）
```

This code tells the compiler to allocate space for five `int`s contiguously on the stack. Note that **uninitialized local arrays contain garbage values**—not zeros. Therefore, we almost always initialize an array at the same time we declare it.

```cpp
int scores[5] = {90, 85, 78, 92, 88};
```

These five values are filled into the array's five positions in order. If there are fewer initial values than the array size, the remaining elements are automatically initialized to zero:

```cpp
int data[5] = {10, 20};  // data = {10, 20, 0, 0, 0}
```

Conversely, if there are more initial values than the array size, the compiler will directly report an error.

If the initialization list provides enough values, the array size can be omitted, letting the compiler count for itself:

```cpp
int primes[] = {2, 3, 5, 7, 11, 13};  // 编译器推断大小为 6
```

The benefit of this approach is that we don't need to synchronously modify the number in the square brackets when adding or removing elements later.

There is a classic formula to find out how many elements an array has:

```cpp
int primes[] = {2, 3, 5, 7, 11, 13};
constexpr int kCount = sizeof(primes) / sizeof(primes[0]);  // kCount = 6
```

`sizeof(primes)` is the total number of bytes occupied by the entire array, and `sizeof(primes[0])` is the number of bytes occupied by a single element. Dividing the two yields the element count. This trick is everywhere in C code, but we will discuss its limitations later.

## Accessing Elements—A Zero-Indexed World

C++ array indices start at 0. For an array of size five, the valid indices are 0 through 4. This is not an arbitrary design choice—`arr[i]` is equivalent to `*(arr + i)` at the底层 level, meaning the position offset backward by `i` elements from the array's starting address.

```cpp
int scores[5] = {90, 85, 78, 92, 88};

std::cout << scores[0] << std::endl;  // 90（第一个元素）
std::cout << scores[4] << std::endl;  // 88（最后一个元素）
```

> **Pitfall Warning**: C-style arrays do not perform any bounds checking. Out-of-bounds accesses like `scores[5]`, `scores[100]`, and `scores[-1]` trigger no compiler errors and throw no exceptions at runtime—they silently read and write memory outside the array. This undefined behavior might coincidentally "seem to work fine," it might crash immediately, or it might quietly modify the values of other variables. Debugging such issues will truly send your blood pressure through the roof.

Modifying array elements also uses indices:

```cpp
scores[2] = 80;  // 把第三个元素从 78 改成 80
```

There are several ways to iterate over an array. The most traditional is an index-based loop, and the range `for` introduced in C++11 is more concise:

```cpp
// 范围 for 遍历（只在声明作用域内有效）
for (int s : scores) {
    std::cout << s << " ";
}
// 输出: 90 85 80 92 88
```

The range `for` can only be used with arrays that "know their own size"—it stops working once the array is passed to a function, and we will explain why later.

## Multidimensional Arrays—The Memory Truth of Matrices

C++ supports multidimensional arrays, which are essentially "arrays of arrays." The most common is the two-dimensional array, used to represent matrices or tables:

```cpp
int matrix[3][4] = {
    {1,  2,  3,  4},
    {5,  6,  7,  8},
    {9, 10, 11, 12}
};
```

This code declares a matrix with three rows and four columns. `matrix[0]` is the first row (which is itself an array containing four `int`s), and `matrix[0][2]` is the third element of the first row, with a value of 3.

The key question: what does this matrix look like in memory? The answer is **contiguous row-major storage**, where all elements are tightly packed in a single contiguous block of memory:

```text
地址:   低地址 →→→→→→→→→→→→→→→→→→→→→→→ 高地址
内容: 1 2 3 4 5 6 7 8 9 10 11 12
       ↑--- 行0 ---↑--- 行1 ---↑--- 行2 ---↑
```

`matrix[1][0]` is immediately adjacent to `matrix[0][3]` in memory. Understanding this is crucial for grasping the relationship between pointers and arrays later on.

We use nested loops to iterate over a two-dimensional array:

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

Here is a performance detail: because memory is stored by row, iterating over rows in the outer loop and columns in the inner loop is the most cache-friendly approach. If we swap the inner and outer loops, the CPU will jump around in memory on every access, causing the cache hit rate to plummet. For large-scale data, the performance difference can be several-fold.

## Passing Arrays as Arguments—The Start of All Nightmares

Now we arrive at the biggest pitfall of C-style arrays: when we pass an array to a function, it undergoes **decay**.

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

In `main`, `sizeof(data)` is 20 (five `int`s, each 4 bytes). But inside the function, `sizeof(arr)` becomes 8—which is the size of a pointer on a 64-bit system, not the size of the array.

This is array decay: when an array is passed as an argument, it automatically decays into a pointer to its first element. The function signatures `int arr[]` and `int* arr` are completely equivalent.

> **Pitfall Warning**: Array decay means the size information of the array is completely lost inside the function. You cannot use `sizeof` to calculate the number of elements, nor can you use a range `for` loop to iterate over it. If you write `sizeof(arr) / sizeof(arr[0])` inside the function, you don't get the array length—you get the meaningless result of "a pointer divided by an int." This is why C-style functions almost always require you to pass the array length as an additional parameter.

So the correct approach is to explicitly pass the size:

```cpp
void print_array(const int arr[], int size)
{
    for (int i = 0; i < size; ++i) {
        std::cout << arr[i] << " ";
    }
    std::cout << std::endl;
}
```

We use the `const` modifier because the function only reads and does not modify the data. This is a good habit—the compiler will report an error if you accidentally modify it.

### Passing Multidimensional Arrays

Passing multidimensional arrays is even more troublesome—you must tell the compiler the size of the second (and higher) dimensions, otherwise the compiler cannot calculate element addresses:

```cpp
// 编译器需要知道第二维是 4 才能正确计算 matrix[i][j] 的地址
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

This directly means the function can only accept arrays whose second dimension is exactly 4; a 3x3 matrix won't work. This is another reason why C-style arrays are very difficult to use in real-world projects.

## C Arrays vs. Modern Alternatives

After all this, we have felt the various pain points of C-style arrays. They cannot be directly assigned—the compiler outright rejects `int b[3] = a;`; they cannot be used as function return values—returning a pointer to a local array is even more dangerous because the memory becomes invalid once the stack frame is reclaimed; they decay into pointers and lose size information; and their length must be determined at compile time, with no support for dynamic runtime sizing.

> **Pitfall Warning**: C-style arrays have another easily overlooked trap—you cannot use `auto` to deduce an array type. `auto a = {1,2,3};` deduces to `std::initializer_list<int>`, not an array. `auto b = arr;` (where `arr` is an array) deduces to a pointer, not a copy of the array. These implicit behaviors are all related to array decay, and if you aren't careful, you will write code that behaves completely differently from your expectations.

These problems are exactly why C++11 introduced `std::array`—it allocates memory on the stack (just like C arrays), but provides modern features like assignment, comparison, range `for`, and `.size()`, and it does not decay into a pointer. However, understanding C-style arrays remains important because you will constantly encounter them in legacy code, C libraries, and embedded code.

## Hands-On Practice—arrays.cpp

Let's integrate the core knowledge points from this chapter into a single program:

```cpp
// arrays.cpp
// C 风格数组综合演练：初始化、遍历、函数传参、矩阵操作

#include <iostream>

/// @brief 打印一维数组
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

/// @brief 计算数组元素之和
int array_sum(const int arr[], int size)
{
    int total = 0;
    for (int i = 0; i < size; ++i) {
        total += arr[i];
    }
    return total;
}

/// @brief 打印矩阵（第二维固定为 4）
void print_matrix(const int matrix[][4], int rows)
{
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < 4; ++j) {
            std::cout << matrix[i][j] << "\t";
        }
        std::cout << std::endl;
    }
}

/// @brief 将 3x4 矩阵转置为 4x3 矩阵
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
    // --- 初始化方式展示 ---
    std::cout << "=== 初始化方式 ===" << std::endl;

    int full_init[5] = {10, 20, 30, 40, 50};
    std::cout << "完全初始化: ";
    print_array(full_init, 5);

    int partial_init[5] = {1, 2};  // 后面自动填 0
    std::cout << "部分初始化: ";
    print_array(partial_init, 5);

    int zero_init[5] = {};  // 全部填 0
    std::cout << "零初始化:   ";
    print_array(zero_init, 5);

    int deduced[] = {2, 3, 5, 7, 11, 13};
    constexpr int kDeducedCount = sizeof(deduced) / sizeof(deduced[0]);
    std::cout << "大小推断:   ";
    print_array(deduced, kDeducedCount);
    std::cout << std::endl;

    // --- 遍历与求和 ---
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

    // --- 矩阵操作 ---
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

Let's verify: 90 + 85 + 78 + 92 + 88 = 433, and the average is 86.6. Everything checks out. After transposing the matrix, row 0 becomes column 0, which is correct.

## Try It Yourself

Reading without practicing is like not learning at all. We recommend writing out the code for each exercise.

### Exercise 1: Array Sum and Average

Write a program that declares an array containing 10 integers, and write two functions to calculate the sum and the average (the average should return a `double`). Verification method: manually add the numbers and compare with the program's output.

### Exercise 2: Matrix Transposition

Write a function that transposes an N x M two-dimensional array into an M x N array. First, implement it with fixed sizes (transpose a 2x3 array into a 3x2 array), then consider: if the number of rows and columns also needs to be parameters, can C-style arrays handle it?

### Exercise 3: Fix the Out-of-Bounds Bug

The following code has an out-of-bounds access bug. Find it and fix it:

```cpp
int data[5] = {10, 20, 30, 40, 50};
for (int i = 0; i <= 5; ++i) {  // 提示：仔细看循环条件
    std::cout << data[i] << std::endl;
}
```

This kind of off-by-one error is extremely common in beginner code.

## Summary

In this chapter, we dissected C-style arrays. Arrays are stored contiguously in memory, indices start at 0, and `sizeof(arr) / sizeof(arr[0])` can retrieve the element count (but this is only valid within the declaration's scope). Multidimensional arrays are stored contiguously by row, and row-major traversal is more cache-friendly. When passed as arguments, arrays decay to pointers and lose their size information. They cannot be assigned, cannot be returned, and lack bounds checking—these pain points are exactly the reason `std::array` exists.

In the next chapter, we will look at `std::array`—the modern alternative that maintains the performance advantages of C arrays while filling in all the shortcomings.
