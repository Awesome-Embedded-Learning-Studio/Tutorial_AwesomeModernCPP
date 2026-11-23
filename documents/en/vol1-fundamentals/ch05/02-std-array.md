---
title: "std::array"
description: "Master std::array usage and how it compares with C arrays, and learn to use modern C++'s fixed-size container"
chapter: 5
order: 2
difficulty: beginner
reading_time_minutes: 16
platform: host
prerequisites:
  - "C-Style Arrays"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
contributors:
  - github: Charliechen114514
    roles: [author, maintain]
    note:
      zh: "文章撰写与长期维护"
      en: "Article writing and ongoing maintenance"
  - github: xiaoshuaijie
    roles: [revision]
    note:
      zh: "补充练习参考答案与修订正文 (PR #216)"
      en: "Contributed exercise solutions and article revisions (PR #216)"
translation:
  source: documents/vol1-fundamentals/ch05/02-std-array.md
  source_hash: 80cbb95b5693c63cd9971496df9e969a536c992a14f330aa27de0f927346402b
  translated_at: '2026-11-22T16:43:11+00:00'
  engine: glm-5.3-refresh
  token_count: 10784
---

# std::array: Everything a C Array Can Do, and It Knows Its Own Size

Can C-style arrays get the job done? Of course they can—we settled that question in the previous section, and the truth is we have been using them since day one of learning C. (If C is new to you, this repository also ships a fairly detailed C tutorial!)

But with C arrays, it is far too easy to get a nasty surprise: they decay into pointers when passed to functions, lose their length information, cannot be assigned directly, cannot be returned from a function, and have no bounds checking. These problems are not something you avoid by "just being careful while writing"—they are inherent design flaws of C arrays.

`std::array` was born to solve these problems. It allocates memory on the stack and is every bit as compact and efficient as a C array, yet it has true value semantics: you can copy it, assign it, pass it as an argument, and return it, and it always knows its own size. Let's take a look at why, ever since C++11, fixed-size arrays should prefer `std::array`.

## std::array Basics

Look at how `std::array` is defined: in the `<array>` header, it takes two template parameters—the element type and a fixed size. The size must be a compile-time constant; just like a C array, `std::array` does not grow dynamically. It is simply a fixed-size contiguous block of memory.

```cpp
#include <array>
#include <iostream>

int main()
{
    std::array<int, 5> arr = {1, 2, 3, 4, 5};

    std::cout << "大小:     " << arr.size() << "\n";
    std::cout << "为空?     " << (arr.empty() ? "是" : "否") << "\n";
    std::cout << "最大大小: " << arr.max_size() << "\n";

    return 0;
}
```

```text
大小:     5
为空?     否
最大大小: 5
```

Functions like `size()`, `max_size()`, and `empty()` look a bit redundant on a fixed-size `std::array`. They exist for the sake of a unified interface: they give `std::array` the same access style as containers like `std::vector`, so when we write generic code we never need to care whether the container underneath is fixed-size or dynamically sized.

> `std::array<int, 0>` is legal, and in that case `empty()` returns `true`. But zero-sized `std::array`s barely ever show up in real code. If you need a container that "might be empty", use `std::vector`.

## Accessing Elements

`std::array` offers several ways to access elements. The ones we use most are `[]` and the safe `at()`, plus convenient interfaces for grabbing the first and last elements and the underlying pointer:

```cpp
#include <array>
#include <iostream>

int main()
{
    std::array<int, 5> arr = {10, 20, 30, 40, 50};

    std::cout << "arr[0]     = " << arr[0] << "\n";      // 无边界检查
    std::cout << "arr.at(2)  = " << arr.at(2) << "\n";   // 越界抛异常
    std::cout << "front      = " << arr.front() << "\n";
    std::cout << "back       = " << arr.back() << "\n";

    int* p = arr.data();                                   // 获取裸指针
    std::cout << "data()[3]  = " << p[3] << "\n";

    return 0;
}
```

```text
arr[0]     = 10
arr.at(2)  = 30
front      = 10
back       = 50
data()[3]  = 40
```

The difference between `[]` and `at()` matters: `arr[10]` on this five-element array is undefined behavior—it might read garbage, might crash, or might appear fine on the surface while the data has been quietly corrupted. `arr.at(10)`, on the other hand, throws a `std::out_of_range` exception, which we can catch and handle.

> A quick aside from the author: during development, prefer `at()` for indexes that might go out of range. In release builds you can switch back to `[]` to avoid the exception overhead—though on modern compilers the extra cost of `at()` when nothing is out of range is nearly zero. Alternatively, use `[]` throughout and lean on AddressSanitizer to catch out-of-bounds bugs.

`data()` returns a raw pointer to the underlying element storage; when we interact with C library functions that take an `int*` parameter, we can pass it straight in.

## Value Semantics: std::array's Core Advantage

That was all the groundwork—next comes where `std::array` truly leaves C arrays behind: it has **value semantics**. We can manipulate it just like an `int` or a `std::string`: copy, assign, pass as an argument, return from a function—all of it works.

```cpp
#include <array>
#include <iostream>

// 直接返回 std::array——C 数组做不到这一点
std::array<int, 5> make_array()
{
    std::array<int, 5> result = {1, 2, 3, 4, 5};
    return result;
}

// 按值传参——不会丢失大小信息
void print_array(std::array<int, 5> arr)
{
    for (int x : arr) {
        std::cout << x << " ";
    }
    std::cout << "\n函数内大小: " << arr.size() << "\n";
}

int main()
{
    auto arr1 = make_array();
    auto arr2 = arr1;  // 直接拷贝——C 数组做不到

    arr2[0] = 99;
    std::cout << "arr1[0] = " << arr1[0] << "\n";  // 1，不受 arr2 影响
    std::cout << "arr2[0] = " << arr2[0] << "\n";  // 99

    print_array(arr1);
    print_array(arr2);

    return 0;
}
```

```text
arr1[0] = 1
arr2[0] = 99
1 2 3 4 5
函数内大小: 5
99 2 3 4 5
函数内大小: 5
```

Notice that every line above is something a C array cannot do. C arrays cannot be assigned directly (`a = b` won't even compile), cannot serve as a function return value, and decay into pointers—losing their length—when passed as function arguments. `std::array` manages all of this because it is a class that wraps an internal C array and provides a copy constructor and a copy-assignment operator. The compiler knows how to copy this object, and it knows its size, which eliminates the array decay problem at the root.

> Passing a `std::array` by value copies the entire array contents. If our array is large (say `std::array<int, 10000>`), we should use a `const` reference: `void process(const std::array<int, 10000>& arr)`. For small arrays, the cost of pass-by-value is essentially negligible.

## C Arrays vs std::array: A Direct Comparison

Let's put C arrays and `std::array` side by side across the common operations:

| Operation | C array | std::array |
|------|--------|------------|
| Declaration | `int arr[5];` | `std::array<int, 5> arr;` |
| Getting the size | `sizeof(arr)/sizeof(arr[0])` (breaks once passed to a function) | `arr.size()` (always valid) |
| Assignment | Not supported | `arr2 = arr1` |
| Copy | Manual `memcpy` | `auto copy = arr;` |
| Passing to a function | Decays to a pointer, size lost | By value keeps the size, or pass a reference |
| Return value | Impossible | Works |
| Bounds checking | None | `arr.at(i)` throws |
| Getting the raw pointer | Automatic decay | `arr.data()` (explicit) |
| Zero overhead | Yes | Yes |

The last row is the key: **`std::array` and C arrays are fully equivalent in memory layout and runtime performance**. All the extra capabilities (`size()`, `at()`, `data()`, value semantics) are compile-time zero-overhead abstractions—there is no extra memory allocation or function-call overhead at runtime.

> If you're curious, compile one traversal program using a C array and another using `std::array` with `-O2` and compare the assembly output—the instructions the two generate are nearly identical. Zero-overhead abstraction is not an empty slogan.

## Fill, Swap, and Traversal

`std::array` also has a few practical operations we'll reach for, and they pair directly with STL algorithms:

```cpp
#include <algorithm>
#include <array>
#include <iostream>

int main()
{
    std::array<int, 5> a = {1, 2, 3, 4, 5};
    std::array<int, 5> b = {10, 20, 30, 40, 50};

    // fill —— 全部设为同一值
    a.fill(0);
    std::cout << "fill 后: ";
    for (int x : a) { std::cout << x << " "; }
    std::cout << "\n";

    // swap —— 交换两个 array 的内容
    a = {1, 2, 3, 4, 5};
    a.swap(b);
    std::cout << "swap 后 a: ";
    for (int x : a) { std::cout << x << " "; }
    std::cout << "\n";

    // 配合 <algorithm>
    std::array<int, 5> c = {5, 3, 1, 4, 2};
    std::sort(c.begin(), c.end());
    std::cout << "排序后: ";
    for (int x : c) { std::cout << x << " "; }
    std::cout << "\n";

    return 0;
}
```

```text
fill 后: 0 0 0 0 0
swap 后 a: 10 20 30 40 50
排序后: 1 2 3 4 5
```

`fill()` is extremely handy when you need to reset a buffer—one line and done. Under the hood, `swap()` exchanges elements one by one, with O(n) time complexity. The entire STL algorithm library works on `std::array` directly—`std::sort`, `std::find`, `std::reverse`—we just pass `begin()` and `end()`.

## In Practice: Rewriting C Array Code with std::array

Let's reimplement, with `std::array`, the operations we previously did with C arrays, and get an intuitive feel for where the improvements land:

```cpp
#include <algorithm>
#include <array>
#include <iostream>

// 函数签名清晰——类型和大小一目了然，不需要额外传长度
void print_stats(const std::array<int, 5>& data)
{
    std::cout << "元素个数: " << data.size() << "\n";

    auto [min_it, max_it] = std::minmax_element(data.begin(), data.end());
    std::cout << "最小值: " << *min_it << "\n";
    std::cout << "最大值: " << *max_it << "\n";

    int sum = 0;
    for (int x : data) { sum += x; }
    std::cout << "平均: " << static_cast<double>(sum) / data.size() << "\n";
}

int main()
{
    std::array<int, 5> scores = {85, 92, 78, 96, 88};

    std::cout << "原始数据: ";
    for (int x : scores) { std::cout << x << " "; }
    std::cout << "\n\n";

    print_stats(scores);

    std::sort(scores.begin(), scores.end());
    std::cout << "\n排序后: ";
    for (int x : scores) { std::cout << x << " "; }

    auto it = std::find(scores.begin(), scores.end(), 88);
    if (it != scores.end()) {
        std::cout << "\n找到 88，下标: " << (it - scores.begin());
    }

    std::reverse(scores.begin(), scores.end());
    std::cout << "\n反转后: ";
    for (int x : scores) { std::cout << x << " "; }
    std::cout << "\n";

    return 0;
}
```

Compile and run:

```bash
g++ -Wall -Wextra -std=c++17 std_array.cpp -o std_array && ./std_array
```

```text
原始数据: 85 92 78 96 88

元素个数: 5
最小值: 78
最大值: 96
平均: 87.8

排序后: 78 85 88 92 96
找到 88，下标: 2
反转后: 96 92 88 85 78
```

The improvements are across the board: `print_stats` takes a `const std::array<int, 5>&`, so the type and size are self-evident; every STL algorithm drops right in—sorting, searching, reversing, min/max are all one-line calls; and the array-decay-loses-the-length problem can never bite us again.

> Whenever we see a C-style signature like `void func(int arr[], int n)` in code, we suggest rewriting it as `void func(const std::array<int, N>& arr)` (size fixed) or `void func(std::span<int> arr)` (size determined at runtime). Neither loses length information, and both are far safer than passing `n` by hand.

### By the Way: What a `std::array` Really Is, and 2D Arrays

When you first meet `std::array`, you might feel it just makes the C array type longer to write. For example:

```cpp
std::array<int, 5> values = {32, 76, 43, 10, 54};
```

That is actually shorthand for aggregate initialization. You can picture a `std::array` as "an object wrapping a fixed-size C array", so the fully spelled-out form can be written with two layers of braces:

```cpp
std::array<int, 5> values = {{
    32, 76, 43, 10, 54
}};
```

The outer braces initialize the `std::array` object; the inner braces initialize the elements it contains. The compiler allows that inner layer of braces to be omitted, which is why the single-layer form is all we need day to day. The "wrapping" here is a mental model to aid understanding; the standard semantics of `std::array` are equivalent to an aggregate holding a `T[N]`, and the concrete member names are implementation details.

A two-dimensional `std::array` simply swaps the element type for another `std::array`. For example:

```cpp
#include <array>
#include <cstdint>

std::array<std::array<std::uint8_t, 4>, 8> data = {{
    {{0x01, 0x00, 0x00, 0x00}},
    {{0x01, 0x01, 0x00, 0x00}},
    {{0x00, 0x01, 0x00, 0x00}},
    {{0x00, 0x01, 0x01, 0x00}},
    {{0x00, 0x00, 0x01, 0x00}},
    {{0x00, 0x00, 0x01, 0x01}},
    {{0x00, 0x00, 0x00, 0x01}},
    {{0x01, 0x00, 0x00, 0x01}}
}};
```

To read this type, work from the outside in: the outer layer has 8 elements, and each of those elements is itself a `std::array` holding 4 `std::uint8_t`s. You can therefore think of it as a fixed-size table of "8 rows, 4 bytes per row". Each row's double braces initialize one inner `std::array`; the rows sit side by side—they are not nested layer upon layer.

If the double braces on every row feel too cramped, you can omit just the inner `std::array`'s layer and keep the outer two:

```cpp
std::array<std::array<std::uint8_t, 4>, 8> data = {{
    {0x01, 0x00, 0x00, 0x00},
    {0x01, 0x01, 0x00, 0x00},
    {0x00, 0x01, 0x00, 0x00},
    {0x00, 0x01, 0x01, 0x00},
    {0x00, 0x00, 0x01, 0x00},
    {0x00, 0x00, 0x01, 0x01},
    {0x00, 0x00, 0x00, 0x01},
    {0x01, 0x00, 0x00, 0x01}
}};
```

This form initializes exactly the same contents as the fully spelled-out version in the previous snippet, and it has been the standard way to write it since C++11. When you meet layer upon layer of braces in older code, unpack them in the order "outer object → outer element → inner object → inner element", and the brackets usually stop making your head spin.

## Exercises

### Exercise 1: Redo the C Array Exercises

Rewrite, with `std::array`, every exercise you previously did with C arrays: declaration, initialization, traversal, passing to functions, finding the maximum—and get a feel for how the two styles differ in clarity and safety.

> Exercise 1 is for your own practice.

### Exercise 2: Sorting and Summarizing Scores

Create a `std::array<int, 8>` holding a set of scores, sort it with `std::sort`, then print the highest score, the lowest score, and the average. All the statistics must be computed with functions from `<algorithm>`.

::: details Reference Solution

```cpp
#include <algorithm>
#include <array>
#include <iostream>

int main()
{
    std::array<int, 8> arr = {32, 76, 43, 10, 54, 65, 87, 21};
    std::cout << "原始数据: ";
    int sum = 0;
    for (int x : arr)
    {
        std::cout << x << " ";
        sum += x;
    }
    double average = static_cast<double>(sum) / arr.size();
    std::cout << "\n";

    std::sort(arr.begin(), arr.end());
    std::cout << "排序后数据: ";

    for (int x : arr)
    {
        std::cout << x << " ";
    }
    std::cout << "\n";
    auto [min_it, max_it] = std::minmax_element(arr.begin(), arr.end());
    std::cout << "最小值: " << *min_it << "\n";
    std::cout << "最大值: " << *max_it << "\n";
    std::cout << "平均值: " << average << "\n";

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Output:

```text
原始数据: 32 76 43 10 54 65 87 21
排序后数据: 10 21 32 43 54 65 76 87
最小值: 10
最大值: 87
平均值: 48.5
```

:::

### Exercise 3: Checking Whether an Element Exists

Write `bool contains(const std::array<int, 5>& arr, int value)` that uses `std::find` to determine whether the array contains the given value. In `main`, test both a value that exists and one that doesn't.

::: details Reference Solution

```cpp
#include <algorithm>
#include <array>
#include <iostream>

bool contains(const std::array<int, 5>& arr, int value)
{
    return std::find(arr.begin(), arr.end(), value) != arr.end();
}

int main()
{
    std::array<int, 5> arr = {32, 76, 43, 10, 54};
    std::cout << "数组是否包含 43: " << (contains(arr, 43) ? "是" : "否") << "\n";
    std::cout << "数组是否包含 99: " << (contains(arr, 99) ? "是" : "否") << "\n";

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Output:

```text
数组是否包含 43: 是
数组是否包含 99: 否
```

:::

---

> **Next up**: `std::array` has fixed-size containers covered, but what about strings? The pitfalls of C-style strings (managing `'\0'` by hand, easy out-of-bounds access, no value semantics) mirror those of C arrays exactly. Next we'll meet `std::string` and see how it solves these problems.
