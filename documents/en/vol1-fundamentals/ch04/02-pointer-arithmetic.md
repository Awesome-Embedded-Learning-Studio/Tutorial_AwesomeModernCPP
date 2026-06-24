---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: Master pointer arithmetic, the relationship between pointers and arrays,
  and pointer operations on C-style strings.
difficulty: beginner
order: 2
platform: host
prerequisites:
- 指针基础
reading_time_minutes: 14
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Pointer Arithmetic and Arrays
translation:
  source: documents/vol1-fundamentals/ch04/02-pointer-arithmetic.md
  source_hash: 4fb2bc0de94a7d866fc9b587f3e077ea2c6210a7633f5b131766688d18a9bc47
  translated_at: '2026-06-24T00:30:25.587504+00:00'
  engine: anthropic
  token_count: 2578
---
# Pointer Arithmetic and Arrays

If you have already grasped the fact that "a pointer is an address," then we must now face a deeper truth: in C++, pointers and arrays are, **at their most fundamental level**, practically two sides of the same coin. (I strongly advise against confusing the concepts of pointers and arrays, as doing so will only lead to trouble in engineering logic.)

In this chapter, we will connect pointer arithmetic, array-to-pointer decay, and C-style string pointer operations. If you previously felt that arrays and pointers were "related but somehow indistinct," today we will untie this knot once and for all.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Understand the mechanism and trigger conditions for array-to-pointer decay.
> - [ ] Grasp the relationship between the actual byte count and element count in pointer addition and subtraction.
> - [ ] Use pointers to traverse arrays and C-style strings.
> - [ ] Understand that the `[]` operator is essentially syntactic sugar for pointer arithmetic.

## Environment Setup

We will conduct all subsequent experiments in the following environment:

- Platform: Linux x86\_64 (WSL2 is also acceptable).
- Compiler: GCC 13+ or Clang 17+.
- Compiler flags: `-Wall -Wextra -std=c++17`.

## An Array Name Is Not a Pointer—But It Mostly Pretends to Be One

Let's start with a classic operation. We declare an array and assign its name to a pointer:

```cpp
#include <iostream>

int main()
{
    int arr[5] = {10, 20, 30, 40, 50};
    int* p = arr;  // 合法！数组名可以直接赋给指针

    std::cout << "arr 的地址:  " << arr << "\n";
    std::cout << "p 的值:      " << p << "\n";
    std::cout << "arr[0] 的地址: " << &arr[0] << "\n";
    std::cout << "*p:          " << *p << "\n";

    return 0;
}
```

**Output:**

```text
arr 的地址:  0x7ffd3a2b1c00
p 的值:      0x7ffd3a2b1c00
arr[0] 的地址: 0x7ffd3a2b1c00
*p:          10
```

The three addresses are identical. This leads us to a crucial concept in C++: **array-to-pointer decay**. In most contexts, when you write the name `arr`, the compiler doesn't treat it as "the entire array," but rather as "a pointer to the first element of the array," which is `&arr[0]`.

Strictly speaking, the statement "an array name is a pointer" is incorrect. The type of `arr` is `int[5]`; it is a complete array type containing five `int` values and occupying 20 bytes. However, once you use it in a context requiring a pointer (such as assigning to an `int*`, passing it to a function, or performing arithmetic), the compiler automatically decays it to `int*`. This decay process is irreversible—once decayed, you cannot go back, and the array length information is lost.

> I mentioned "most contexts," so when does it *not* decay? There are only three exceptions: `sizeof(arr)` returns the size of the entire array; `&arr` yields a "pointer to the array" (type `int(*)[5]`, not `int*`); and when initializing a character array with a string literal. Apart from these, the array name always decays.

## Pointer Arithmetic—Stepping by Elements, Not Bytes

One of the most powerful capabilities of pointers is arithmetic. However, the rules here differ from our typical intuition—adding 1 to a pointer doesn't move it by 1 byte, but by **the size of the type it points to**.

### The Actual Effect of Pointer Addition

Let's look at the code directly to compare the stepping of `int*` and `char*`:

```cpp
#include <iostream>

int main()
{
    int numbers[4] = {100, 200, 300, 400};
    char chars[4]  = {'A', 'B', 'C', 'D'};

    int* pi = numbers;
    char* pc = chars;

    std::cout << "=== int* 步进 ===\n";
    std::cout << "pi:     " << pi << " -> *pi = " << *pi << "\n";
    std::cout << "pi + 1: " << (pi + 1) << " -> *(pi+1) = " << *(pi + 1) << "\n";
    std::cout << "pi + 2: " << (pi + 2) << " -> *(pi+2) = " << *(pi + 2) << "\n";

    std::cout << "\n=== char* 步进 ===\n";
    std::cout << "pc:     " << static_cast<void*>(pc)
              << " -> *pc = " << *pc << "\n";
    std::cout << "pc + 1: " << static_cast<void*>(pc + 1)
              << " -> *(pc+1) = " << *(pc + 1) << "\n";
    std::cout << "pc + 2: " << static_cast<void*>(pc + 2)
              << " -> *(pc+2) = " << *(pc + 2) << "\n";

    return 0;
}
```

Output:

```text
=== int* 步进 ===
pi:     0x7ffd4e3a1c00 -> *pi = 100
pi + 1: 0x7ffd4e3a1c04 -> *(pi+1) = 200
pi + 2: 0x7ffd4e3a1c08 -> *(pi+2) = 300

=== char* 步进 ===
pc:     0x7ffd4e3a1bf0 -> *pc = A
pc + 1: 0x7ffd4e3a1bf1 -> *(pc+1) = B
pc + 2: 0x7ffd4e3a1bf2 -> *(pc+2) = C
```

Notice the difference in the addresses. Adding one to an `int*` increases the address by four (from `...c00` to `...c04`), while adding one to a `char*` increases the address by only one (from `...bf0` to `...bf1`). This is the golden rule of pointer arithmetic: **`p + n` actually moves `n * sizeof(*p)` bytes**. The compiler automatically calculates the byte offset based on the type the pointer points to, so we do not need to manually multiply by `sizeof`.

> We used `static_cast<void*>` to force the address to print in hexadecimal for the `char*` output. This is because `std::ostream` treats `char*` specially—it assumes it is a C-style string and prints characters until it hits a `'\0'`. We will encounter this pitfall again later.

### Pointer Subtraction—Calculating Element Distance

We can subtract two pointers that point to the same array. The result is the number of elements between them (not the number of bytes):

```cpp
int arr[5] = {10, 20, 30, 40, 50};
int* p1 = &arr[1];  // 指向 20
int* p2 = &arr[4];  // 指向 50

std::cout << "p2 - p1 = " << (p2 - p1) << "\n";  // 3
```

The result of `p2 - p1` is 3, because there are three elements separating `arr[1]` from `arr[4]`. This feature is very useful in many algorithms—for example, to calculate the index of an element within an array, we simply need `ptr - arr`.

> Pointer subtraction is only valid for two pointers pointing to the **same array** (or the same contiguous memory block). If we subtract two unrelated pointers, the result is undefined behavior, and the compiler might not even issue a warning.

## Traversing Arrays with Pointers

Since `arr + i` is equivalent to `&arr[i]`, we can traverse the array from start to finish using pointers, without needing subscripts:

```cpp
#include <iostream>

int main()
{
    int arr[5] = {10, 20, 30, 40, 50};

    // 指针遍历
    std::cout << "指针遍历: ";
    for (int* p = arr; p != arr + 5; ++p) {
        std::cout << *p << " ";
    }
    std::cout << "\n";

    // 下标遍历
    std::cout << "下标遍历: ";
    for (int i = 0; i < 5; ++i) {
        std::cout << arr[i] << " ";
    }
    std::cout << "\n";

    // range-for 遍历
    std::cout << "range-for: ";
    for (int x : arr) {
        std::cout << x << " ";
    }
    std::cout << "\n";

    return 0;
}
```

**Output:**

```text
指针遍历: 10 20 30 40 50
下标遍历: 10 20 30 40 50
range-for: 10 20 30 40 50
```

All three approaches yield identical results. So, which one should we use?

Honestly, in daily development, **prioritize range-for**. It is the most concise, the least error-prone, and, after compiler optimization, its performance is identical to that of pointer traversal. The advantage of pointer traversal lies in scenarios requiring finer control—such as when you only need to iterate over a portion of an array (starting from an element meeting a specific condition), or when you need to manipulate multiple positions simultaneously. However, if you simply need to traverse the entire array, range-for is the best choice.

> There is a very common pitfall here: the "past-the-end pointer" `arr + 5` is valid, and you can use it for comparisons, but you **must absolutely never dereference it**. `*(arr + 5)` is undefined behavior because it points to a location outside the bounds of the array. The C++ standard only allows you to calculate this address; it does not permit reading from or writing to the content it points to. This follows the same logic as the `end()` iterator in standard library containers—it marks "one past the last element," and is not a valid element itself.

## Pointers and C-Style Strings

A C-style string is essentially a `char` array that ends with a `'\0'` (null character). Since it is an array, all the relationships between pointers and arrays discussed here apply. When we write a string literal like `"hello"` in C++, its type is `const char[6]` (5 characters plus 1 `'\0'`), and in most contexts, it decays to `const char*`.

```cpp
#include <iostream>

int main()
{
    const char* s = "hello";

    std::cout << "字符串: " << s << "\n";
    std::cout << "首字符: " << *s << "\n";
    std::cout << "第3个字符: " << s[2] << "\n";

    // 手动计算字符串长度——模拟 strlen
    std::size_t len = 0;
    while (s[len] != '\0') {
        ++len;
    }
    std::cout << "长度: " << len << "\n";

    return 0;
}
```

**Output:**

```text
字符串: hello
首字符: h
第3个字符: l
长度: 5
```

Now, let's rewrite this length calculation using pure pointers, which means we won't use any subscripts:

```cpp
const char* str_len_demo(const char* s)
{
    const char* start = s;
    while (*s != '\0') {
        ++s;
    }
    std::cout << "长度 = " << (s - start) << "\n";
    return s;
}
```

This pattern is ubiquitous in the C standard library implementation. Functions like `strlen`, `strcpy`, and `strchr` all rely on similar pointer traversals at their core—starting from the beginning and walking character by character until `'\0'` is encountered. `s - start` utilizes the pointer arithmetic we discussed earlier to directly calculate the number of elements spanned.

> Here is another classic pitfall: `const char* s = "hello";` causes `s` to point to a string literal. String literals are stored in the read-only data segment of the program, so **you must absolutely never modify the content through this pointer**. `s[0] = 'H';` leads to undefined behavior (UB)—on most systems, it will immediately trigger a segmentation fault. If you need a modifiable string, use a character array like `char s[] = "hello";`. This copies the content to an array on the stack, making modifications safe.

## The Essence of the Subscript Operator

Now that we have laid the groundwork, we can reveal a fundamental truth: **the `[]` operator is essentially syntactic sugar for pointer arithmetic**.

When the compiler sees `arr[n]`, what it actually does is `*(arr + n)`. It adds the offset `n` to the pointer `arr`, and then dereferences the result. Since an array name decays into a pointer in an expression, the entire process is purely a pointer operation. This also explains why arrays lose their length when passed to a function—the function receives only a pointer, so `sizeof` returns the size of the pointer itself, not the original array size.

Since `arr[n]` is equivalent to `*(arr + n)` and addition is commutative, `n[arr]` is simply `*(n + arr)`—completely equivalent. Yes, the syntax `5[arr]` is valid and works exactly the same as `arr[5]`.

```cpp
int arr[5] = {10, 20, 30, 40, 50};

std::cout << arr[3] << "\n";  // 40
std::cout << 3[arr] << "\n";  // 也是 40——但这纯粹是 trivia，别在实际代码里这么写
```

We mention this trivia not to encourage code golf, but to deepen understanding: **subscripting is never magic; it is simply pointer arithmetic plus dereferencing**. Once you truly grasp this, many previously confusing phenomena become easy to explain—such as why `sizeof` yields incorrect results when an array is passed as a parameter, or why negative indices are valid in certain scenarios (`p[-1]` is simply `*(p - 1)`, provided you ensure that `p - 1` points to valid memory).

## Multidimensional Arrays and Pointers—A Brief Overview

Multidimensional arrays are the most headache-inducing part of the relationship between pointers and arrays. We will provide a simple example here, but we will keep it brief and not dive too deep:

```cpp
int matrix[3][4] = {
    {1,  2,  3,  4},
    {5,  6,  7,  8},
    {9, 10, 11, 12}
};

int (*row_ptr)[4] = matrix;  // 指向"含4个int的数组"的指针

std::cout << row_ptr[1][2] << "\n";  // 7
```

The type of `matrix` is `int[3][4]`. After decay, it becomes a pointer to the first row, with the type `int(*)[4]`—a "pointer to an array of four `int`s". Note that the parentheses around `(*row_ptr)` are mandatory because `[]` has higher precedence than `*`. The declaration `int* row_ptr[4]` declares an "array of four `int*`s", which is completely different.

The pointer relationships in multi-dimensional arrays are indeed a bit convoluted. If you feel a bit dizzy right now, don't worry—scenarios in actual projects where we directly manipulate multi-dimensional arrays with raw pointers are rare. Later, when we learn `std::array` and `std::span`, we will see safer ways to handle such problems.

## In Practice: Comprehensive Demo `ptr_arith.cpp`

Let's integrate the content we covered earlier into a complete program, covering pointer traversal, calculating distance via pointer subtraction, and manipulating C-style strings with pointers:

```cpp
#include <cstddef>
#include <iostream>

int main()
{
    // --- 1. 多种方式遍历数组 ---
    int data[6] = {5, 12, 7, 23, 18, 9};

    std::cout << "=== 指针遍历 ===\n";
    for (int* p = data; p != data + 6; ++p) {
        std::cout << *p << " ";
    }
    std::cout << "\n";

    // --- 2. 指针减法计算元素距离 ---
    int* first = &data[0];
    int* last  = &data[5];
    std::cout << "\n=== 指针距离 ===\n";
    std::cout << "first 和 last 之间隔了 "
              << (last - first) << " 个元素\n";

    // 用指针减法找到某个值的下标
    int target = 23;
    for (int* p = data; p != data + 6; ++p) {
        if (*p == target) {
            std::cout << "值 " << target << " 的下标是: "
                      << (p - data) << "\n";
            break;
        }
    }

    // --- 3. 用指针实现 strlen ---
    const char* msg = "pointer";
    const char* scan = msg;
    while (*scan != '\0') {
        ++scan;
    }
    std::cout << "\n=== 手写 strlen ===\n";
    std::cout << "\"" << msg << "\" 的长度: "
              << (scan - msg) << "\n";

    // --- 4. 用指针反转数组 ---
    std::cout << "\n=== 反转数组 ===\n";
    std::cout << "反转前: ";
    for (int x : data) {
        std::cout << x << " ";
    }
    std::cout << "\n";

    int* left  = data;
    int* right = data + 5;
    while (left < right) {
        int temp = *left;
        *left  = *right;
        *right = temp;
        ++left;
        --right;
    }

    std::cout << "反转后: ";
    for (int x : data) {
        std::cout << x << " ";
    }
    std::cout << "\n";

    return 0;
}
```

Build and Run:

```bash
g++ -Wall -Wextra -std=c++17 ptr_arith.cpp -o ptr_arith && ./ptr_arith
```

**Output:**

```text
=== 指针遍历 ===
5 12 7 23 18 9

=== 指针距离 ===
first 和 last 之间隔了 5 个元素
值 23 的下标是: 3

=== 手写 strlen ===
"pointer" 的长度: 7

=== 反转数组 ===
反转前: 5 12 7 23 18 9
反转后: 9 18 23 7 12 5
```

This program brings together the core concepts of this chapter: pointer traversal, calculating distance via pointer subtraction, scanning C-style strings with pointers, and in-place array reversal using the two-pointer technique. The "two-pointer" trick for reversing arrays—where one pointer starts at the beginning and the other at the end, moving inward while swapping—is a frequent guest in interview questions and algorithm challenges.

## Summary

Let's review the core points of this chapter:

- In most expressions, an array name **decays** into a pointer to its first element, losing length information in the process.
- Pointer arithmetic steps by the **size of the pointed-to type**; `p + 1` actually moves `sizeof(*p)` bytes.
- Two pointers pointing to the same array can be **subtracted**, yielding the number of elements between them.
- The `[]` operator is essentially syntactic sugar for `*(p + n)`, which explains why `sizeof` fails on array parameters.
- A C-style string is a `char` array terminated by `'\0'`; traversing until `'\0'` signifies the end of the string.
- Prefer range-based `for` loops for daily array traversal; use pointer traversal when fine-grained control is needed.

### Common Pitfalls

| Error | Cause | Solution |
|------|------|----------|
| `sizeof(arr)` returns pointer size inside a function | Array decay; the function parameter is actually a pointer | Pass the length as a separate parameter, or use `std::array`/`std::span` |
| Dereferencing a past-the-end pointer `*(arr + len)` | Past-the-end pointers are for comparison only, not access | Use `!=` instead of `<=` in loop conditions, and never dereference |
| Modifying a string literal `s[0] = 'H'` | Literals reside in read-only memory; writing triggers a segmentation fault | Use `char s[]` to copy to the stack before modifying |
| Subtracting unrelated pointers | The two pointers must point to the same memory block | Always ensure pointers involved in arithmetic belong to the same array |

## Exercises

### Exercise 1: Implement `strlen` by Hand

Calculate string length using pure pointers without any standard library functions. The required function signature is `std::size_t my_strlen(const char* s)`.

**Verification:** Compare the result of `my_strlen("hello world")` with `std::strlen("hello world")` to ensure consistency.

### Exercise 2: Two-Pointer Array Reversal

We demonstrated the two-pointer reversal technique in the practical code above. Now, try encapsulating it into a function `void reverse_array(int* begin, int* end)`, where `end` is a past-the-end pointer. Note: The function does not need to know the array length; it can complete the reversal using only the two pointers.

### Exercise 3: String Comparison via Pointers

Implement `int my_strcmp(const char* a, const char* b)`: compare character by character. Return 0 if they are identical, a negative number if the first differing character in `a` is less than the corresponding character in `b`, and a positive number otherwise. This is a slightly more challenging exercise requiring simultaneous traversal of two strings and checking for termination conditions.

---

> **Next Stop:** Pointers are powerful, but they are also dangerous. Next, we will explore "references"—a safer alternative provided by C++. In many scenarios, they can replace raw pointers, making code both safer and clearer.
