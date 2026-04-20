---
chapter: 1
cpp_standard:
- 11
description: Deep dive into the memory layout of C arrays, multidimensional arrays,
  variable-length arrays, and their subtle relationship with pointers.
difficulty: beginner
order: 14
platform: host
prerequisites:
- 指针与数组、const 和空指针
reading_time_minutes: 23
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: Arrays In Depth
translation:
  source: documents/vol1-fundamentals/c_tutorials/10-arrays-deep-dive.md
  source_hash: 68cafa122521b4f5ac8765bd9beb1beb161954ae85c386774bd891976a67ef9f
  translated_at: '2026-04-20T03:27:31.661173+00:00'
  engine: anthropic
  token_count: 2966
---
# A Deeper Look at Arrays

In the quick start and pointer chapters, we touched on arrays, but honestly, we only scratched the surface of "knowing how to use them." Arrays seem simple to use—declare, initialize, access by index—who can't do that? But once you start asking questions like "how are multi-dimensional arrays actually laid out in memory?", "why can't we assign arrays directly?", and "when are arrays and pointers truly interchangeable versus when are they not?"—you'll find there are quite a few details worth breaking down. These aren't just theoretical nuances; understanding the memory model of arrays sets the stage for later C++ topics like `std::array`, `std::vector`, and `std::span`, where you'll see exactly what problems each one solves.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Master various initialization methods for one-dimensional arrays (including C99 designated initializers)
> - [ ] Understand the memory layout and row-major storage of multi-dimensional arrays
> - [ ] Understand the principles and limitations of Variable Length Arrays (VLA)
> - [ ] Understand the fundamental limitations of arrays
> - [ ] Precisely distinguish between arrays and pointers

## Environment Notes

All code in this chapter is based on the C99 standard, tested under GCC 13.x / Clang 17.x, and runs on Linux x86-64. Parts involving VLA require compiler support for C99 (`-std=c99` or `-std=c11`). If you are using MSVC, note that Microsoft's C compiler has incomplete C99 support, and some VLA features may be unavailable—we recommend using GCC or Clang.

## Step 1 — Master Various Array Initialization Methods

Everyone knows how to declare an array—`int arr[10];` and you're done. But the details of initialization are richer than many people realize. Let's start with the basics and work our way up to the designated initializers introduced in C99.

### Basic Initialization

```c
// 完全初始化——每个元素都给了值
int primes[] = {2, 3, 5, 7, 11};  // 大小自动推导为 5

// 部分初始化——没给值的元素自动填 0
int data[10] = {1, 2, 3};  // data[0]=1, data[1]=2, data[2]=3, data[3..9]=0

// 全零初始化——这是把数组清零最干净利落的写法
int zeros[100] = {0};  // 第一个元素显式为 0，其余自动填 0
```

The behavior of partial initialization is very important—the C standard dictates that as long as an array is initialized (even if only one element is explicitly set), all uninitialized elements are automatically zero-initialized to the appropriate type's zero value. Therefore, `{0}` has become the idiomatic way to zero out an array, much cleaner than writing a manual loop.

### Designated Initializers (C99)

C99 introduced a highly practical feature: the designated initializer. It allows you to specify "which index gets what value," with the rest automatically filled with zero. This is especially convenient when dealing with sparse arrays, configuration tables, or register mappings:

```c
// 只初始化特定的位置，其余自动为 0
int sparse[100] = {[5] = 10, [20] = 30, [99] = -1};
// sparse[5] = 10, sparse[20] = 30, sparse[99] = -1, 其余全部 0

// 可以乱序，也可以覆盖——后面的初始化覆盖前面的
int config[10] = {[3] = 100, [7] = 200, [3] = 999};
// config[3] = 999（被覆盖了）, config[7] = 200

// 指定初始化器之后可以跟连续的普通初始化
int seq[10] = {[3] = 10, 20, 30};
// seq[3] = 10, seq[4] = 20, seq[5] = 30, 其余 0
```

Honestly, designated initializers are extremely common in embedded development. For example, if you have an interrupt vector table or a command dispatch table where most entries are empty and only a few need to be filled, code written with designated initializers is both clean and error-resistant. C++ didn't officially support designated initializers until C++20 (and with some restrictions), so this feature has a clearer advantage in pure C code.

## Step 2 — Understand the Memory Layout of Multi-Dimensional Arrays

A multi-dimensional array is essentially "an array of arrays." `int matrix[3][4]` declares an array of three elements, where each element is itself an array of four `int`. This might sound like a tongue twister, but it precisely describes the memory layout.

### Row-Major Storage

C's multi-dimensional arrays are stored in **row-major** order in memory, meaning the rightmost subscript changes the fastest. For `int matrix[3][4]`, the memory arrangement looks like this:

```text
地址递增方向 →

matrix[0][0] matrix[0][1] matrix[0][2] matrix[0][3]   ← 第 0 行
matrix[1][0] matrix[1][1] matrix[1][2] matrix[1][3]   ← 第 1 行
matrix[2][0] matrix[2][1] matrix[2][2] matrix[2][3]   ← 第 2 行

整个数组是连续的 12 个 int，没有间隙
```

Let's verify this:

```c
#include <stdio.h>

int main(void) {
    int matrix[3][4] = {
        {0,  1,  2,  3},
        {10, 11, 12, 13},
        {20, 21, 22, 23}
    };

    // 用一维指针遍历整个二维数组
    int* flat = &matrix[0][0];
    for (int i = 0; i < 12; i++) {
        printf("%d ", flat[i]);
    }
    // 输出: 0 1 2 3 10 11 12 13 20 21 22 23

    return 0;
}
```

You can see that the memory is completely linear—`matrix[1][0]` sits right next to `matrix[0][3]`. Understanding this is crucial because many performance optimizations (like cache-friendly access) are built on this foundation: traversing by row is much faster than traversing by column, as contiguous memory accesses utilize CPU cache lines much more effectively.

### Initializing Multi-Dimensional Arrays

Initializing multi-dimensional arrays is similar to one-dimensional arrays, just with nested braces:

```c
// 完全初始化
int m1[2][3] = {
    {1, 2, 3},
    {4, 5, 6}
};

// 部分初始化——未给的元素自动为 0
int m2[2][3] = {
    {1},       // 第 0 行: {1, 0, 0}
    {4, 5}     // 第 1 行: {4, 5, 0}
};

// 也可以用指定初始化器
int m3[3][4] = {
    [0] = {1, 2, 3, 4},
    [2] = {20, 21, 22, 23}
    // 第 1 行全部为 0
};

// 甚至可以嵌套指定
int m4[3][4] = {
    [0] = {[1] = 99},
    [2] = {[0] = 88, [3] = 77}
};
```

### Passing Multi-Dimensional Arrays as Function Parameters

When passing a two-dimensional array to a function, the compiler must know the size of the second dimension (and all higher dimensions) to correctly calculate address offsets. This is because the address calculation formula for `matrix[i][j]` is `base + i * cols + j`, where `cols` is the size of the second dimension. If the compiler doesn't know `cols`, it cannot generate correct addressing code:

```c
// 必须指定列数
void print_matrix(int rows, int m[][4]) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < 4; j++) {
            printf("%3d ", m[i][j]);
        }
        printf("\n");
    }
}

// 等价写法——用数组指针
void print_matrix_v2(int rows, int (*m)[4]) {
    // 完全一样的效果
}
```

If you want a function to accept two-dimensional arrays with different column counts, you have to abandon the direct 2D array syntax and use a one-dimensional array with manual index calculation, or use an array of pointers. This is indeed a trade-off between flexibility and type safety.

## Step 3 — Understand the Pros and Cons of Variable Length Arrays (VLA)

C99 introduced Variable Length Arrays, allowing runtime variables to determine the array size. Note that "variable length" here doesn't mean the array size can change dynamically—once created, the size is fixed—but rather that the size determination is deferred to runtime:

```c
#include <stdio.h>

int main(void) {
    int n;
    printf("Enter array size: ");
    scanf("%d", &n);

    int vla[n];  // 大小在运行时确定
    for (int i = 0; i < n; i++) {
        vla[i] = i * i;
    }
    // ...
    return 0;
}
```

VLA can also be used in two-dimensional scenarios, which is particularly convenient in function parameters:

```c
// VLA 作为函数参数——行数和列数都是运行时确定的
void print_vla_matrix(int rows, int cols, int m[rows][cols]) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%3d ", m[i][j]);
        }
        printf("\n");
    }
}

int main(void) {
    int rows = 3, cols = 4;
    int matrix[rows][cols];  // VLA 二维数组
    // ... 填充数据
    print_vla_matrix(rows, cols, matrix);
    return 0;
}
```

See how the size of `m[rows][cols]` in the parameter list of `print_vla_matrix` depends on the preceding parameters `rows` and `cols`. This solves the "2D array parameters must have a fixed column count" problem mentioned earlier.

### Limitations and Controversies of VLA

VLA sounds great, but it has several issues that make it rather unpopular in industry.

First, VLA is allocated on the stack. Stack space is typically limited (Linux defaults to 8 MB, and embedded systems might only have a few KB). If a user inputs a very large number—say, `int vla[1000000]`—you might blow the stack directly, with no means of recovery. Unlike `malloc` returning `NULL` which you can handle, stack overflow is straight-up undefined behavior.

> ⚠️ **Pitfall Warning**
> VLA is allocated on the stack, its size is unpredictable, and allocation failure has no recovery mechanism—it's undefined behavior directly. In the embedded domain, MISRA-C explicitly prohibits the use of VLA. If you need an array whose size is determined at runtime, using `malloc` and checking the return value is the safe approach.

Second, C11 demoted VLA from a mandatory feature to an optional one—compilers can claim not to support VLA and indicate this using a macro `__STDC_NO_VLA__`. This means you cannot rely on VLA being available on all C11 compilers.

In the embedded domain, VLA is essentially prohibited. Static analysis tools (like MISRA-C) typically explicitly forbid VLA because its size is unpredictable, conflicting directly with the requirements of real-time performance and deterministic memory usage.

Our recommendation is to simply know that VLA exists and be able to read VLA code written by others. When writing your own code, prefer fixed-size arrays or `malloc`. In scenarios requiring flexible sizes where dynamic allocation is acceptable, `malloc` with bounds checking is much safer than VLA.

## Step 4 — Understand the Fundamental Limitations of Arrays

Arrays in C have several fundamental limitations, and understanding these is key to grasping the design motivations behind C++ containers later on.

### Arrays Are Not Assignable

After declaring two arrays, you cannot directly assign one to the other:

```c
int a[3] = {1, 2, 3};
int b[3];
// b = a;  // 编译错误！数组不能直接赋值
```

The reason is that in an assignment expression, the array name decays to a pointer to its first element, and the left side of the assignment operator must be a modifiable lvalue—the decayed pointer is an rvalue and cannot be assigned to. Therefore, to copy an array, you must copy element by element or use `memcpy`:

```c
#include <string.h>

int a[3] = {1, 2, 3};
int b[3];
memcpy(b, a, sizeof(a));  // 正确的数组拷贝方式
```

### Arrays Cannot Be Function Return Values

A function cannot return an array type. You can't write a signature like `int[10] foo(void)`. If you want to "return" an array from a function, there are three common approaches: return a pointer (pointing to a static or dynamically allocated array), pass an array out via a parameter, or wrap the array in a struct and return that. The last method is actually quite practical—C allows struct assignment and returning structs by value, and structs can contain arrays:

```c
typedef struct {
    int data[10];
} IntArray10;

IntArray10 make_array(void) {
    IntArray10 result = {.data = {1, 2, 3, 4, 5}};
    return result;  // 合法！结构体可以返回
}
```

You can also see this technique in the C standard library's math functions (returning complex numbers, structures like `div_t`, etc.).

### Array Size Must Be a Compile-Time Constant (Except for VLA)

The size of a regular array must be determined at compile time. `int arr[n]` (where `n` is a variable) is illegal in C89—only C99's VLA allows this. And VLA has the problems mentioned above. This means that in C89 or environments without VLA support, if you need to create arrays of different sizes based on runtime data, your only option is `malloc`.

## Step 5 — Precisely Distinguish Between Arrays and Pointers

In both the quick start and pointer chapters, we mentioned that "array names decay to pointers." This statement is fine, but it easily leads people to assume "arrays are pointers"—which is incorrect. Arrays are arrays, and pointers are pointers; they are only interchangeable in specific situations.

### When Array Names Decay to Pointers

Array names decay to pointers to their first element in the following scenarios: when passed as function arguments, when used in arithmetic operations, and when used in expressions (most cases). However, there are three exceptions—array names do not decay when used as operands of `sizeof`, `_Alignof` (C11), or the address-of operator `&`:

```c
int arr[10] = {0};

// sizeof 对数组名——得到整个数组的大小
printf("%zu\n", sizeof(arr));  // 40（10 * sizeof(int)，假设 int 为 4 字节）

// & 对数组名——得到指向整个数组的指针，类型是 int (*)[10]
int (*ptr_to_array)[10] = &arr;
// 注意：ptr_to_array + 1 跳过整个数组（40 字节）

// 数组名在表达式中——退化为 int*
int* p = arr;  // 等价于 int* p = &arr[0];
printf("%zu\n", sizeof(p));  // 8（指针本身的大小，64 位系统）
```

`sizeof(arr)` returns 40 while `sizeof(p)` returns 8—this is the most direct evidence that arrays are not pointers.

### Pointer Arithmetic vs. Array Subscripting

`arr[i]` and `*(arr + i)` are completely equivalent—C's array subscript operator `[]` is essentially syntactic sugar for pointer arithmetic. Furthermore, this equivalence is commutative: `arr[i]` is equivalent to `i[arr]`. Yes, `3[arr]` is valid C code, completely equivalent to `arr[3]`. Of course, don't use this in real projects—it has no benefits other than showing off, and it will make your colleagues' blood pressure spike.

### Two-Dimensional Arrays vs. Arrays of Pointers

This is a very classic point of confusion. `int m[3][4]` and `int** pp` might both seem to support access via `m[i][j]` and `pp[i][j]`, but their memory models are completely different:

```text
int m[3][4]:
  连续的 12 个 int
  m[i][j] 的地址 = base + i*4 + j

int** pp:
  pp → [ptr0, ptr1, ptr2]   ← 指针数组（不连续）
         │      │      │
         ▼      ▼      ▼
       [....] [....] [....]  ← 每行各自的内存
```

A two-dimensional array is a single contiguous block of memory, where the compiler calculates addresses directly using the row-column formula. An array of pointers adds a layer of indirection—you first find the pointer for row `i`, then use that pointer to find the element at index `j`. Therefore, `int m[3][4]` cannot be passed to a function accepting a `int**` parameter, and vice versa. Their types are incompatible, and forcing a cast will lead to undefined behavior.

> ⚠️ **Pitfall Warning**
> Although both `int m[3][4]` and `int** pp` can be accessed using `m[i][j]` / `pp[i][j]`, their memory models are completely different—the former is contiguous memory, while the latter has a layer of indirection. Never pass a 2D array to a `int**` parameter; the compiler might let it slide, but the address calculations at runtime will be completely wrong.

## Bridging to C++

Now that we understand the limitations of C arrays, let's look at how C++ solves them one by one.

### `std::array` — Assignable Fixed-Size Arrays

`std::array` is a fixed-size array container introduced in C++11. It allocates memory on the stack (just like C arrays) but fills in the features missing from C arrays: it supports assignment, copying, returning from functions, and knowing its own size:

```cpp
#include <array>
#include <algorithm>

int main() {
    std::array<int, 5> a = {1, 2, 3, 4, 5};
    std::array<int, 5> b;

    b = a;  // 直接赋值！C 数组做不到

    // 知道自己的大小
    for (std::size_t i = 0; i < b.size(); i++) {
        // b[i] ...
    }

    // 还支持 fill、swap、比较运算等
    b.fill(0);  // 全部填 0
}
```

`std::array` has zero overhead—it introduces no extra memory or runtime cost, and after compiler optimization, it is just as fast as a raw C array. If you need a fixed-size array in C++, there is no reason to use a raw array instead of `std::array`.

### `std::vector` — Dynamic-Size Arrays

`std::vector` solves the problem of "size not being known until runtime." It allocates memory on the heap, can grow and shrink dynamically, and automatically manages its memory lifecycle:

```cpp
#include <vector>

int main() {
    int n;
    std::cin >> n;

    std::vector<int> vec(n);  // 运行时确定大小，类似 VLA 但安全得多
    for (int i = 0; i < n; i++) {
        vec[i] = i * i;
    }

    vec.push_back(999);  // 还能追加元素
    // 离开作用域自动释放内存
}
```

`std::vector` can be seen as a safe alternative to VLA—its size is mutable, allocation failure throws an exception (rather than the undefined behavior of a stack overflow), it provides bounds checking (the `at()` method), and it automatically frees memory. The only cost is a small amount of heap allocation overhead, but in the vast majority of scenarios, this overhead is perfectly acceptable.

### Range-based for Loop Traversal

Traversing a C array requires either subscripts or pointer arithmetic, both of which require manual boundary management. The range-based for loop introduced in C++11 makes traversal very concise, and both `std::array` and `std::vector` support it:

```cpp
#include <array>
#include <vector>

int main() {
    std::array<int, 5> arr = {10, 20, 30, 40, 50};
    std::vector<int> vec = {1, 2, 3};

    // 范围 for——不需要管下标
    for (const auto& elem : arr) {
        // elem 是 arr 中每个元素的 const 引用
    }

    for (auto& elem : vec) {
        elem *= 2;  // 可以修改元素
    }
}
```

It's worth noting that the range-based for loop can also be used with raw C arrays (as long as the array's size is visible in the current scope), but its use cases are somewhat limited—once an array decays to a pointer, the size information is lost, and the range-based for loop can no longer be used. This is yet another advantage of `std::array` over raw arrays.

## Summary

The memory model of arrays isn't actually that complex—it's just a contiguous sequence of elements of the same type. One-dimensional arrays offer diverse initialization methods, and C99's designated initializers are particularly handy for sparse data. Multi-dimensional arrays are "arrays of arrays" stored in row-major order, and understanding this is important for performance optimization. VLA is convenient but carries the risk of stack overflow, and it is generally not recommended in industry or embedded domains. Arrays have a few fundamental limitations—they can't be assigned and can't be returned from functions—which are perfectly resolved by `std::array` in C++. And while arrays and pointers are interchangeable in most scenarios, they are fundamentally different types—`sizeof` and `&` are the places most likely to expose these differences. Understanding these low-level details means that when you learn C++ containers later, you'll be able to appreciate the motivation behind every design decision.

### Key Takeaways

- [ ] During partial initialization, unspecified elements are automatically filled with zero; `{0}` is the idiomatic way to zero out an array
- [ ] C99 designated initializers allow initialization by position, suitable for sparse data and configuration tables
- [ ] Multi-dimensional arrays are stored contiguously in row-major order; the address of `m[i][j]` is `base + i * cols + j`
- [ ] VLA is allocated on the stack, its size is unpredictable, and it was demoted to an optional feature in C11
- [ ] Arrays cannot be assigned or returned from functions, but wrapping them in a struct works around this
- [ ] Array names do not decay to pointers when used as operands of `sizeof` and `&`
- [ ] `std::array` is a zero-overhead fixed-size container that supports assignment and copying
- [ ] `std::vector` is a dynamic-size container and a safe alternative to VLA

## Exercises

### Exercise 1: Matrix Operations

Implement the following three functions to perform basic matrix operations. Matrices are represented using standard C two-dimensional arrays; please implement matrix transposition and matrix multiplication yourself:

```c
#define kMaxRows 10
#define kMaxCols 10

/// @brief 转置矩阵，将 src 的转置结果写入 dst
/// @param rows src 的行数
/// @param cols src 的列数
/// @param src 源矩阵
/// @param dst 目标矩阵（调用者保证大小为 cols x rows）
void matrix_transpose(int rows, int cols,
                      const int src[rows][cols],
                      int dst[cols][rows]);

/// @brief 矩阵乘法，计算 a x b 的结果写入 c
/// @param m a 的行数
/// @param n a 的列数 / b 的行数
/// @param p b 的列数
/// @param a 左矩阵 (m x n)
/// @param b 右矩阵 (n x p)
/// @param c 结果矩阵 (m x p)
void matrix_multiply(int m, int n, int p,
                     const int a[m][n],
                     const int b[n][p],
                     int c[m][p]);

/// @brief 打印矩阵
/// @param rows 行数
/// @param cols 列数
/// @param mat 矩阵
void matrix_print(int rows, int cols, const int mat[rows][cols]);
```

Hint: The core of transposition is `dst[j][i] = src[i][j]`. The core of multiplication is a triple loop—`c[i][j]` is the dot product of the `i`th row of `a` and the `j`th column of `b`. The function parameters here use VLA syntax so that the column count can be specified dynamically.

### Exercise 2: Comparing VLA and malloc

Write a program that allocates an integer array whose size is determined by user input, using both VLA and `malloc`, and then compare the behavioral differences between the two:

```c
#include <stdio.h>
#include <stdlib.h>

/// @brief 用 VLA 方式分配并填充数组
/// @param n 数组大小
/// @param out 输出数组的指针（VLA 版本需要调用者传入栈数组）
void fill_with_vla(int n, int arr[n]);

/// @brief 用 malloc 方式分配并填充数组
/// @param n 数组大小
/// @return 指向动态分配数组的指针，失败返回 NULL
int* fill_with_malloc(int n);
```

Please implement these two functions and the `main` function yourself. Consider the following questions: if the user inputs a very large number (like 100000000), what happens with each approach? Which approach can gracefully handle allocation failure? Which approach would you choose in an embedded system?

## References

- [Array declaration and initialization - cppreference](https://en.cppreference.com/w/c/language/array_initialization)
- [Variable length arrays - cppreference](https://en.cppreference.com/w/c/language/array#Variable-length_arrays)
- [std::array - cppreference](https://en.cppreference.com/w/cpp/container/array)
- [std::vector - cppreference](https://en.cppreference.com/w/cpp/container/vector)
