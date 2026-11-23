---
chapter: 1
cpp_standard:
- 11
description: Gain a solid understanding of C array memory layout, multidimensional arrays,
  variable-length arrays, and their subtle relationship with pointers.
difficulty: beginner
order: 14
platform: host
prerequisites:
- Pointers, Arrays, const, and Null Pointers
reading_time_minutes: 18
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: A Deep Dive into Arrays
translation:
  source: documents/vol1-fundamentals/c_tutorials/10-arrays-deep-dive.md
  source_hash: 023b3a93541decc5813b825597d3aefafab50005c4faf8ac0f7a70de961171cb
  translated_at: '2026-11-22T16:39:43+00:00'
  engine: glm-5.3-refresh
  token_count: 16464
---
# A Deep Dive into Arrays

We already ran into arrays in the earlier crash-course and pointer chapters, but honestly we never got past the "able to use them" level. Arrays are simple to use—declare, initialize, access by index—who can't do that? But the moment you start asking "how exactly is a multidimensional array laid out", "why can't arrays be assigned directly", or "when are arrays and pointers the same thing and when are they not", you'll find plenty of details worth unpacking. And these details aren't just theoretical: once you understand the memory model of arrays, you'll know exactly what problem each of `std::array`, `std::vector`, and `std::span` in C++ is solving when you meet them later.

All code in this chapter is based on C99, tested with GCC 13.x / Clang 17.x on Linux x86-64. The parts involving variable-length arrays (VLAs) require a compiler with C99 support (`-std=c99` or `-std=c11`). If you are on MSVC, note that Microsoft's C compiler has incomplete C99 support and some VLA features may be unavailable—GCC or Clang is recommended.

## Step 1 — Master the Various Ways to Initialize Arrays

Everyone knows how to declare an array—`int arr[10];` and you're done. But the details of initialization are richer than most people imagine. We'll start with the most basic forms and work our way up to the designated initializers introduced in C99.

### Basic Initialization

```c
// 完全初始化——每个元素都给了值
int primes[] = {2, 3, 5, 7, 11};  // 大小自动推导为 5

// 部分初始化——没给值的元素自动填 0
int data[10] = {1, 2, 3};  // data[0]=1, data[1]=2, data[2]=3, data[3..9]=0

// 全零初始化——这是把数组清零最干净利落的写法
int zeros[100] = {0};  // 第一个元素显式为 0，其余自动填 0
```

This partial-initialization behavior matters a lot—the C standard says that once an array is initialized (even if only a single element is), every element not explicitly assigned is automatically initialized to the zero value of its type. That's why `{0}` has become the idiomatic way to zero out an array, far cleaner than hand-writing a loop.

### Designated Initializers (C99)

C99 introduced a very practical feature: designated initializers. They let you specify "which position gets which value", and the remaining positions are filled with zero automatically. This is especially handy for sparse arrays, configuration tables, and register maps:

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

Honestly, designated initializers see heavy use in embedded development. Say you have an interrupt vector table or a command dispatch table where most entries are empty and only a few need filling—code written with designated initializers is both cleaner and less error-prone. C++ only officially gained designated initializers in C++20 (with some restrictions), so the advantage of this feature is even more pronounced in pure C code.

## Step 2 — Understand the Memory Layout of Multidimensional Arrays

A multidimensional array is essentially an "array of arrays". `int matrix[3][4]` declares an array with 3 elements, each of which is itself an array of 4 `int`s. That sentence sounds like a tongue twister, but it describes the memory layout precisely.

### Row-Major Storage

Multidimensional arrays in C are stored in **row-major** order, meaning the rightmost subscript varies fastest. For `int matrix[3][4]`, the memory arrangement looks like this:

```text
地址递增方向 →

matrix[0][0] matrix[0][1] matrix[0][2] matrix[0][3]   ← 第 0 行
matrix[1][0] matrix[1][1] matrix[1][2] matrix[1][3]   ← 第 1 行
matrix[2][0] matrix[2][1] matrix[2][2] matrix[2][3]   ← 第 2 行

整个数组是连续的 12 个 int，没有间隙
```

Let's verify:

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

As you can see, the memory is completely linear—`matrix[1][0]` sits immediately after `matrix[0][3]`. Grasping this matters, because many performance optimizations (cache-friendly access among them) build on it: traversing by row is much faster than traversing by column, because consecutive memory accesses make better use of CPU cache lines.

### Initializing Multidimensional Arrays

Multidimensional arrays are initialized much like one-dimensional ones, just with extra nested braces:

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

### Multidimensional Arrays as Function Parameters

When you pass a 2D array to a function, the compiler must know the size of the second (and any higher) dimension to compute address offsets correctly. The reason: the address of `matrix[i][j]` follows the formula `base + i * cols + j`, where `cols` is the size of the second dimension. Without knowing `cols`, the compiler has no way to generate correct addressing code:

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

If you want a function to accept 2D arrays with different column counts, you have to give up the direct 2D array syntax and switch to a 1D array plus manual index arithmetic, or use an array of pointers. It's a genuine trade-off between flexibility and type safety.

## Step 3 — Weigh the Pros and Cons of Variable-Length Arrays (VLAs)

C99 introduced variable-length arrays (VLAs), which allow a runtime variable to be used as the array size. Note that "variable-length" does not mean the size can change dynamically—once created, the size is fixed—it means the size determination is deferred until runtime:

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

VLAs also work in two dimensions, and they are especially convenient as function parameters:

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

Notice how, in `print_vla_matrix`'s parameter list, the size of `m[rows][cols]` depends on the preceding parameters `rows` and `cols`. That resolves the "2D array parameters must have a fixed column count" problem from earlier.

### VLA Limitations and Controversy

VLAs sound lovely, but a few issues have made them rather unpopular in industry.

First, VLAs live on the stack. Stack space is usually limited (8 MB by default on Linux, possibly just a few KB on embedded systems), so if the user enters a very large number—say `int vla[1000000]`—you can blow the stack outright, with no way to recover. Unlike a `malloc` that returns `NULL`, which you can at least handle, stack overflow is straight-up undefined behavior.

VLAs are allocated on the stack, their sizes are unpredictable, and allocation failure offers no recovery path—it is undefined behavior, plain and simple. In the embedded field, MISRA-C explicitly forbids VLAs. If you need an array whose size is determined at runtime, using `malloc` and checking the return value is the safe approach.

Second, C11 demoted VLAs from a mandatory feature to an optional one—compilers may declare that they don't support VLAs by defining the macro `__STDC_NO_VLA__`. That means you cannot count on VLAs being available on every C11 compiler.

In the embedded domain, VLAs are essentially banned. Static analysis tooling (such as MISRA-C) usually prohibits them explicitly, because their unpredictable sizes clash completely with the demands of real-time behavior and deterministic memory usage.

Our advice: know that VLAs exist and be able to read VLA code written by others, but when writing your own code, prefer fixed-size arrays or `malloc`. In scenarios that need flexible sizes and can accept dynamic allocation, `malloc` plus bounds checking is far safer than a VLA.

## Step 4 — Understand the Fundamental Limitations of Arrays

Arrays in C carry a few fundamental limitations, and these limitations are the key to understanding the motivation behind C++ container design later on.

### Arrays Cannot Be Assigned

After declaring two arrays, you cannot assign one directly to the other:

```c
int a[3] = {1, 2, 3};
int b[3];
// b = a;  // 编译错误！数组不能直接赋值
```

The reason is that in an assignment expression the array name decays into a pointer to its first element, and the left side of an assignment must be a modifiable lvalue—the decayed pointer is an rvalue and cannot be assigned to. So to copy an array, your only options are element-by-element copying or `memcpy`:

```c
#include <string.h>

int a[3] = {1, 2, 3};
int b[3];
memcpy(b, a, sizeof(a));  // 正确的数组拷贝方式
```

### Arrays Cannot Be Function Return Values

Functions cannot return array types. You can't write a signature like `int[10] foo(void)`. If you want to "return" an array from a function, there are three common approaches: return a pointer (to a static array or a dynamically allocated one), pass the array out through a parameter, or wrap the array in a struct and return that. The last one is actually quite practical—C allows struct assignment and returning structs by value, and a struct can contain an array:

```c
typedef struct {
    int data[10];
} IntArray10;

IntArray10 make_array(void) {
    IntArray10 result = {.data = {1, 2, 3, 4, 5}};
    return result;  // 合法！结构体可以返回
}
```

You can find this trick in the C standard library's math functions too (returning complex numbers, or structs like `div_t`).

### Array Sizes Must Be Compile-Time Constants (Except VLAs)

The size of an ordinary array must be settled at compile time. `int arr[n]` (with `n` a variable) is illegal in C89—only C99's VLAs allow it, and VLAs come with all the problems described above. This means that under C89, or in environments without VLA support, `malloc` is your only option if you want to create differently sized arrays from runtime data.

## Step 5 — Tell Arrays and Pointers Apart, Precisely

Both the crash-course and pointer chapters said "an array name decays to a pointer". That statement is fine, but it tempts people into thinking "an array *is* a pointer"—which is wrong. An array is an array, a pointer is a pointer; they merely convert into each other under specific circumstances.

### When an Array Name Decays to a Pointer

An array name decays into a pointer to its first element in these situations: being passed as a function argument, being used in arithmetic, and being used in expressions (most of the time). But there are three exceptions—an array name does not decay as the operand of `sizeof`, of `_Alignof` (C11), or of the address-of operator `&`:

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

`sizeof(arr)` returns 40 while `sizeof(p)` returns 8—that is the most direct evidence that an array is not a pointer.

### Pointer Arithmetic vs Array Subscripting

`arr[i]` and `*(arr + i)` are fully equivalent—C's array subscript operator `[]` is essentially syntactic sugar over pointer arithmetic. And the equivalence is commutative: `arr[i]` equals `i[arr]`. Yes, `3[arr]` is legal C code, completely equivalent to `arr[3]`. Of course, never write like this in a real project—it has no benefit beyond showing off, and it will send your coworkers' blood pressure through the roof.

### 2D Arrays vs Arrays of Pointers

This is a truly classic point of confusion. `int m[3][4]` and `int** pp` both look like they can be accessed with `m[i][j]` and `pp[i][j]`, but their memory models are completely different:

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

A 2D array is one contiguous block of memory, and the compiler computes addresses directly from the row-column formula. An array of pointers adds a level of indirection—first find the pointer to row `i`, then use that pointer to reach element `j`. That's why `int m[3][4]` cannot be passed to a function taking `int**`, and the reverse holds too. The types are incompatible, and forcing a conversion leads to undefined behavior.

Although both `int m[3][4]` and `int** pp` can be accessed with `m[i][j]` / `pp[i][j]`, their memory models are completely different—the former is contiguous memory, the latter has an indirection layer. Never pass a 2D array to an `int**` parameter; the compiler might let it slide, but the runtime address computation will be completely wrong.

## Bridging to C++

With these limitations of C arrays in mind, let's see how C++ solves them one by one.

### `std::array` — An Assignable Fixed-Size Array

`std::array` is the fixed-size array container introduced in C++11. It allocates on the stack (just like a C array) while filling in everything C arrays lack: it can be assigned, copied, and returned from functions, and it knows its own size:

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

`std::array` is zero-overhead—it introduces no extra memory or runtime cost, and after compiler optimization it is just as fast as a bare C array. If you need a fixed-size array in C++, there is no reason to use a raw array instead of `std::array`.

### `std::vector` — A Dynamically Sized Array

`std::vector` solves the "size only known at runtime" problem. It allocates on the heap, can grow and shrink dynamically, and manages its memory lifetime automatically:

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

`std::vector` can be seen as the safe replacement for VLAs—resizable, throwing an exception on allocation failure (instead of the undefined behavior of stack overflow), offering bounds-checked access (the `at()` method), and releasing memory automatically. The only cost is a little heap-allocation overhead, which is perfectly acceptable in the vast majority of scenarios.

### Range-Based for Loops

Iterating a C array means either indices or pointer arithmetic, both of which require managing the bounds yourself. The range-based for loop introduced in C++11 makes traversal wonderfully concise, and both `std::array` and `std::vector` support it:

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

It's worth noting that range-based for also works with bare C arrays (as long as the array's size is visible in the current scope), but its use cases there are fairly limited—once an array decays into a pointer, the size information is lost and range-based for no longer works. That's one more advantage `std::array` holds over bare arrays.

## Exercises

### Exercise 1: Matrix Operations

**Difficulty: Intermediate** · Transposing and multiplying 2D arrays

Implement the following three functions to complete basic matrix operations. The matrices are represented as plain C 2D arrays; implement the matrix transpose and matrix multiplication yourself:

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

Hints: the heart of the transpose is `dst[j][i] = src[i][j]`. The heart of multiplication is a triple loop—`c[i][j]` is the dot product of row `i` of `a` and column `j` of `b`. The function parameters here use VLA syntax so that the column count can be specified dynamically.

::: details Reference Solution

First, pin down what the two sets of dimensions mean: `rows`/`cols` describe the source
matrix's shape, while `m`/`n`/`p` describe the shapes of `a (m x n)`, `b (n x p)`, and
`c (m x p)` in the multiplication. The parameter `int a[m][n]` is adjusted at the call
into "a pointer to an array of `n` ints", so the second dimension `n` cannot be omitted;
it determines the stride between rows in `a[i][k]`. The implementation below assumes the
caller passes positive dimensions, non-null pointers, and destination matrices with
enough space.

```c
#include <stdio.h>

// 将 src 的第 i 行第 j 列写到 dst 的第 j 行第 i 列。
void matrix_transpose(int rows, int cols,
                      const int src[rows][cols],
                      int dst[cols][rows])
{
    for (int i = 0; i < rows; ++i)
    {
        for (int j = 0; j < cols; ++j)
        {
            dst[j][i] = src[i][j];
        }
    }
}
// c[i][j] 是 a 的第 i 行和 b 的第 j 列的点积。
void matrix_multiply(int m, int n, int p,
                     const int a[m][n],
                     const int b[n][p],
                     int c[m][p])
{
    for (int i = 0; i < m; ++i)
    {
        for (int j = 0; j < p; ++j)
        {
            c[i][j] = 0;
            for (int k = 0; k < n; ++k)
            {
                c[i][j] += a[i][k] * b[k][j];
            }
        }
    }
}
// 打印一个 rows x cols 的矩阵。
void matrix_print(int rows, int cols, const int mat[rows][cols])
{
    for (int i = 0; i < rows; ++i)
    {
        for (int j = 0; j < cols; ++j)
        {
            printf("%d ", mat[i][j]);
        }
        printf("\n");
    }
}
int main(void)
{
    const int rows = 2;
    const int cols = 3;
    const int product_cols = 2;

    const int source[2][3] = {
        {1, 2, 3},
        {4, 5, 6},
    };
    int transposed[3][2] = {0};
    const int multiplier[3][2] = {
        {7, 8},
        {9, 10},
        {11, 12},
    };
    int product[2][2] = {0};

    //二维数组传参时，会退化为“指向一行数组的指针”。
    matrix_transpose(rows, cols, source, transposed);
    printf("Transpose:\n");
    //指向一个包含 rows 个 const int 元素的数组的指针
    matrix_print(cols, rows, (const int (*)[rows])transposed);

    matrix_multiply(rows, cols, product_cols, source, multiplier, product);
    printf("Product:\n");
    matrix_print(rows, product_cols, (const int (*)[product_cols])product);

    return 0;
}
```

Output:

```text
Transpose:
1 4
2 5
3 6
Product:
58 64
139 154
```

Transposing is just swapping two subscripts, with time complexity `O(rows * cols)`.
Multiplication first zeroes each `c[i][j]`, then accumulates `n` products, with time
complexity `O(m * n * p)`; if you skip the zeroing, whatever was already in `c` gets
wrongly folded into the result. Note also that this uses C99 VLA parameters, not a VLA
re-allocated inside the function: the actual array storage is still provided by the
caller, and the function only accesses it through pointers carrying the dimension
information.

:::

### Exercise 2: Comparing VLAs and malloc

**Difficulty: Intermediate** · Choosing between VLAs and malloc

Write a program that allocates an int array—its size decided by user input—once with a VLA and once with `malloc`, then compares how the two behave:

```c
#include <stdio.h>
#include <stdlib.h>

/// @brief 用 VLA 方式分配并填充数组
/// @param n 数组大小
/// @param arr 调用者提供的 VLA 数组
void fill_with_vla(int n, int arr[n]);

/// @brief 用 malloc 方式分配并填充数组
/// @param n 数组大小
/// @return 指向动态分配数组的指针，失败返回 NULL
int *fill_with_malloc(int n);

```

::: details Reference Solution

This example separates "who provides the storage" from "who releases it": the VLA's
space is provided on the stack by the caller, and `fill_with_vla` does not own it;
`fill_with_malloc` requests space on the heap and hands ownership to the caller, so the
caller must call `free` at the end. The input cap is deliberately conservative—it keeps
the demo program from crashing outright when a VLA exhausts the stack; production code
should re-evaluate that cap against the actual stack size.

```c
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * VLA 的空间由调用者提供。本函数只负责填充数据，
 * 不负责释放数组，因为 VLA 通常位于调用者的栈上。
 */
void fill_with_vla(int n, int arr[n])
{
    if (n <= 0 || arr == NULL)
    {
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        arr[i] = i + 1;
    }
}

/*
 * malloc 在堆上申请数组，并填充 1, 2, 3, ... n。
 * 返回的内存归调用者所有，调用者使用完后必须调用 free。
 */
int *fill_with_malloc(int n)
{
    if (n <= 0)
    {
        return NULL;
    }

    int *arr = malloc((size_t)n * sizeof (*arr));
    if (arr == NULL)
    {
        return NULL;
    }

    for (int i = 0; i < n; ++i)
    {
        arr[i] = i + 1;
    }

    return arr;
}

// 这里只打印前 10 个元素，避免大输入让示例输出失控。
static void print_array(const char *name, int n, const int arr[n])
{
    const int count = n < 10 ? n : 10;

    printf("%s:", name);
    for (int i = 0; i < count; ++i)
    {
        printf(" %d", arr[i]);
    }
    if (n > count)
    {
        printf(" ...");
    }
    putchar('\n');
}

int main(void)
{
    int n;
    char line[100];

    puts("请输入数组长度（1~100000）：");
    if (fgets(line, sizeof(line), stdin) == NULL ||
        sscanf(line, "%d", &n) != 1 || n <= 0 || n > 100000)
    {
        fprintf(stderr, "输入无效：请输入 1~100000 之间的整数。\n");
        return EXIT_FAILURE;
    }

    /* VLA：数组大小由运行时的 n 决定，离开作用域后自动释放。 */
    int vla_array[n];
    fill_with_vla(n, vla_array);

    /* malloc：在堆上申请同样大小的数组，使用完后必须手动释放。 */
    int *malloc_array = fill_with_malloc(n);
    if (malloc_array == NULL)
    {
        fprintf(stderr, "malloc 分配内存失败。\n");
        return EXIT_FAILURE;
    }

    printf("\n--- VLA 与 malloc 对比 ---\n");
    printf("数组长度：%d\n", n);
    printf("VLA 数组大小：%lu 字节\n",
           (unsigned long)sizeof vla_array);
    printf("malloc 数组大小：%lu 字节\n",
           (unsigned long)((size_t)n * sizeof *malloc_array));

    print_array("VLA 数组", n, vla_array);
    print_array("malloc 数组", n, malloc_array);

    printf("\nVLA：由作用域自动管理，不需要 free。\n");
    printf("malloc：由程序员手动管理，下面调用 free 释放。\n");

    free(malloc_array);
    malloc_array = NULL;
    return EXIT_SUCCESS;
}

```

Output (with input `5`):

```text
请输入数组长度（1~100000）：

--- VLA 与 malloc 对比 ---
数组长度：5
VLA 数组大小：20 字节
malloc 数组大小：20 字节
VLA 数组: 1 2 3 4 5
malloc 数组: 1 2 3 4 5

VLA：由作用域自动管理，不需要 free。
malloc：由程序员手动管理，下面调用 free 释放。
```

The two arrays can hold identical contents and occupy the same number of bytes, but
their lifetimes differ: once execution leaves `main`'s scope, the VLA's lifetime ends
automatically, while memory obtained from `malloc` is not released automatically—you
must call `free` explicitly. If you change the input to `100000000`, this program rejects
it first, avoiding the creation of a dangerous stack array; if you simply delete that
cap, the VLA could trigger a stack overflow before even entering the function body, and
the program would have no chance to handle the failure as gracefully as it does by
checking `malloc`'s return value. Embedded systems generally prefer fixed-size static
buffers, resorting to dynamic allocation—combined with an explicit memory budget—only
when runtime sizes are genuinely needed.

C11 downgraded VLAs to an optional feature; compilers may define `__STDC_NO_VLA__` to
signal that they don't support them. This example must be compiled with a C99/C11
compiler that supports VLAs, for example `gcc -std=c11 -Wall -Wextra example.c`; MSVC's
C compiler cannot be relied upon for VLAs.

:::

## References

- [Array Declaration and Initialization - cppreference](https://en.cppreference.com/w/c/language/array_initialization)
- [Variable-Length Arrays - cppreference](https://en.cppreference.com/w/c/language/array#Variable-length_arrays)
- [std::array - cppreference](https://en.cppreference.com/w/cpp/container/array)
- [std::vector - cppreference](https://en.cppreference.com/w/cpp/container/vector)
