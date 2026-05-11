---
title: Pointer arithmetic and arrays
description: Master pointer arithmetic, the relationship between pointers and arrays,
  and pointer operations on C-style strings.
chapter: 4
order: 2
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
- 指针基础
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
cpp_standard:
- 11
- 14
- 17
- 20
---
# Pointer Arithmetic and Arrays

If you already understand that "a pointer is an address," then we need to face a deeper truth—in C++, pointers and arrays are, **at their most fundamental level**, practically two sides of the same coin. (We strongly advise against conflating the concepts of pointers and arrays, as doing so will only lead to flawed engineering logic.)

In this chapter, we tie together pointer arithmetic, array-to-pointer decay, and pointer operations on C-style strings. If you previously felt that arrays and pointers were "related somehow but you couldn't quite articulate how," today we untangle this knot once and for all.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Understand the mechanism and trigger conditions of array-to-pointer decay
> - [ ] Master the relationship between the actual byte count and element count in pointer addition and subtraction
> - [ ] Traverse arrays and C-style strings using pointers
> - [ ] Understand that the `[]` operator is essentially syntactic sugar for pointer arithmetic

## Environment Setup

We run all the following experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c++17`

## An Array Name Is Not a Pointer—But It Usually Pretends to Be One

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

Output:

```text
arr 的地址:  0x7ffd3a2b1c00
p 的值:      0x7ffd3a2b1c00
arr[0] 的地址: 0x7ffd3a2b1c00
*p:          10
```

All three addresses are identical. This brings us to a crucial concept in C++—**array-to-pointer decay**. When you write the name `arr`, the compiler in most contexts does not treat it as "the entire array," but rather as "a pointer to the first element of the array," which is `&arr[0]`.

So the statement "an array name is a pointer" is strictly incorrect. The type of `arr` is `int[5]`, which is a complete array type containing five `int`s, occupying 20 bytes. But once you use it in a context that requires a pointer (such as assigning it to `int*`, passing it to a function, or doing arithmetic), the compiler automatically decays it into `int*`. This decay process is irreversible—once decayed, it cannot be undone, and you lose the array length information.

> We said "most contexts," so when does it *not* decay? Only in three situations: `sizeof(arr)` returns the size of the entire array, `&arr` yields a "pointer to the array" (the type is `int(*)[5]`, not `int*`), and when initializing a character array with a string literal. Aside from these, the array name always decays.

## Pointer Addition and Subtraction—Stepping by Elements, Not Bytes

One of the most powerful capabilities of pointers is arithmetic. But the rules here differ from our usual understanding—adding 1 to a pointer does not move it by 1 byte, but by **the size of the pointed-to type**.

### The Actual Effect of Pointer Addition

Let's look directly at the code, comparing the steps of `int*` and `char*`:

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

Notice the address differences. Each increment of `int*` increases the address by 4 (from `...c00` to `...c04`), while each increment of `char*` only increases the address by 1 (from `...bf0` to `...bf1`). This is the core rule of pointer arithmetic: **`p + n` actually moves by `n * sizeof(*p)` bytes**. The compiler automatically calculates the actual byte offset based on the type the pointer points to, so you don't need to manually multiply by `sizeof`.

> For the output of `char*`, we used `static_cast<void*>` to force printing the address in hexadecimal. The reason is that `std::ostream` has special handling for `char*`—it treats it as a C string and keeps printing until it encounters a `'\0'`. We will run into this pitfall again later.

### Pointer Subtraction—Calculating Element Distance

Two pointers pointing to the same array can be subtracted, and the result is the number of elements between them (not the number of bytes):

```cpp
int arr[5] = {10, 20, 30, 40, 50};
int* p1 = &arr[1];  // 指向 20
int* p2 = &arr[4];  // 指向 50

std::cout << "p2 - p1 = " << (p2 - p1) << "\n";  // 3
```

The result of `p2 - p1` is 3, because there are three elements between `arr[1]` and `arr[4]`. This feature is very useful in many algorithms—for example, to calculate the index of an element in an array, you only need `ptr - arr`.

> Pointer subtraction can only be performed on two pointers that **point to the same array (or the same contiguous block of memory)**. If you subtract two completely unrelated pointers, the result is undefined behavior, and the compiler might not even give a warning.

## Traversing Arrays with Pointers

Since `arr + i` is exactly equal to `&arr[i]`, we can completely traverse the array by walking from beginning to end with a pointer, without needing subscripts:

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

Output:

```text
指针遍历: 10 20 30 40 50
下标遍历: 10 20 30 40 50
range-for: 10 20 30 40 50
```

The results of all three approaches are exactly the same. So the question is—which one should we use?

Honestly, in daily development, **prefer range-for**. It is the most concise, the least error-prone, and after compiler optimization, its performance is completely identical to pointer traversal. The advantage of pointer traversal lies in scenarios requiring finer control—for example, when you only need to traverse a portion of the array (starting from an element that meets certain conditions), or when you need to manipulate multiple positions simultaneously. But if you just need to go through the entire array, range-for is the best choice.

> There is a very common pitfall here: the "past-the-end pointer" `arr + 5` is legal, and you can use it for comparison, but you **must absolutely never dereference it**. `*(arr + 5)` is undefined behavior because the location it points to is already outside the bounds of the array. The C++ standard only allows you to compute this address, not to read from or write to the content it points to. This follows the same logic as the `end()` iterator in the standard library containers—it marks the "next position after the last element" and is not a valid element itself.

## Pointers and C-Style Strings

A C-style string is essentially a `char` array terminated by a `'\0'` (null character). Since it is an array, all the relationships between pointers and arrays apply here. When we write a string literal like `"hello"` in C++ code, its type is `const char[6]` (five characters plus one `'\0'`), which decays to `const char*` in most contexts.

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

Output:

```text
字符串: hello
首字符: h
第3个字符: l
长度: 5
```

Now let's rewrite this length calculation using pure pointer operations, meaning without using any subscripts:

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

This pattern is ubiquitous in the implementations of the C standard library. Functions like `strlen`, `strcpy`, and `strchr` all use similar pointer traversal under the hood—starting from the beginning and walking character by character until hitting a `'\0'`. `s - start` leverages the pointer subtraction we discussed earlier to directly obtain how many elements were spanned in between.

> Here is another classic pitfall: `const char* s = "hello";` makes `s` point to a string literal. String literals are stored in the read-only data segment of the program, and **you must absolutely never modify the content through this pointer**. `s[0] = 'H';` leads to undefined behavior—on most systems, it will directly trigger a segmentation fault. If you need a modifiable string, please use a character array like `char s[] = "hello";`, which copies the content to an array on the stack, making modifications safe.

## The Essence of the Subscript Operator

Now we have enough groundwork to uncover another truth: **the `[]` operator is essentially syntactic sugar for pointer arithmetic**.

When the compiler sees `arr[n]`, what it actually does is `*(arr + n)`. It first adds the offset `n` to the pointer `arr`, and then dereferences it. Because the array name decays to a pointer in an expression, the entire process is purely a pointer operation. This also explains why the array length is lost after being passed to a function—the function receives only a pointer, and `sizeof` can only retrieve the size of the pointer itself, not the original array size.

Since `arr[n]` is just `*(arr + n)`, and addition is commutative, then `n[arr]` is also `*(n + arr)`—completely equivalent. Yes, the syntax `5[arr]` is legal and has the exact same effect as `arr[5]`.

```cpp
int arr[5] = {10, 20, 30, 40, 50};

std::cout << arr[3] << "\n";  // 40
std::cout << 3[arr] << "\n";  // 也是 40——但这纯粹是 trivia，别在实际代码里这么写
```

We mention this trivia not to encourage showing off in your code, but to deepen your understanding: **subscripts are never magic; they are just pointer addition followed by dereferencing**. When you truly understand this, many previously puzzling phenomena suddenly make sense—such as why `sizeof` is incorrect after passing an array as an argument, or why negative subscripts are legal in certain scenarios (`p[-1]` is just `*(p - 1)`, as long as you ensure that `p - 1` points to valid memory).

## Multidimensional Arrays and Pointers—Just a Taste

Multidimensional arrays are the most headache-inducing part of the relationship between pointers and arrays. Let's provide a simple example, just touching the surface without going into depth:

```cpp
int matrix[3][4] = {
    {1,  2,  3,  4},
    {5,  6,  7,  8},
    {9, 10, 11, 12}
};

int (*row_ptr)[4] = matrix;  // 指向"含4个int的数组"的指针

std::cout << row_ptr[1][2] << "\n";  // 7
```

The type of `matrix` is `int[3][4]`, which decays into a pointer to the first row, with the type `int(*)[4]`—"a pointer to an array of four `int`s." Note that the parentheses around `(*row_ptr)` are mandatory, because `[]` has higher precedence than `*`. `int* row_ptr[4]` declares "an array of four `int*`s," which is a completely different thing.

The pointer relationships in multidimensional arrays are indeed a bit convoluted. If you feel a bit dizzy right now, that's okay—in actual projects, scenarios where you directly manipulate multidimensional arrays with raw pointers are not that common. Later, when we learn about `std::array` and `std::span`, there will be safer ways to handle such problems.

## Hands-on: Comprehensive Demo of ptr_arith.cpp

Let's integrate the content discussed above into a complete program, covering pointer traversal, calculating distance via pointer subtraction, and manipulating C strings with pointers:

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

Compile and run:

```bash
g++ -Wall -Wextra -std=c++17 ptr_arith.cpp -o ptr_arith && ./ptr_arith
```

Output:

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

This program strings together all the core knowledge points of this chapter: pointer traversal, calculating distance with pointer subtraction, pointer scanning of C strings, and in-place array reversal using the two-pointer technique. The "two-pointer" trick for reversing an array—two pointers starting from each end moving toward the middle, swapping as they go—is a frequent guest in interviews and algorithm problems.

## Summary

Let's review the core points of this chapter:

- An array name **decays** into a pointer to its first element in most expressions, losing its length information once decayed
- Pointer addition and subtraction step by **the size of the pointed-to type**; `p + 1` actually moves `sizeof(*p)` bytes
- Two pointers pointing to the same array can be **subtracted**, and the result is the number of elements between them
- The `[]` operator is essentially syntactic sugar for `*(p + n)`, which also explains why `sizeof` fails after an array is passed as a parameter
- A C-style string is a `char` array terminated by `'\0'`, and traversing with a pointer until `'\0'` marks the end of the string
- For daily array traversal, prefer range-for; use pointer traversal for scenarios requiring fine-grained control

### Common Mistakes

| Mistake | Cause | Solution |
|---------|-------|----------|
| `sizeof(arr)` returns the pointer size inside a function | Array decay; the function parameter is actually a pointer | Pass the length as a separate parameter, or use `std::array`/`std::span` |
| Dereferencing the past-the-end pointer `*(arr + len)` | The past-the-end pointer is only for comparison and cannot be accessed | Use `!=` instead of `<=` for the loop condition, and do not dereference it |
| Modifying a string literal `s[0] = 'H'` | Literals reside in the read-only segment; writing triggers a segmentation fault | Use `char s[]` to copy to the stack before modifying |
| Subtracting unrelated pointers | The two pointers must point to the same block of memory | Always ensure the pointers involved in the operation belong to the same array |

## Exercises

### Exercise 1: Implement strlen by Hand

Without using any standard library functions, implement string length calculation using pure pointers. The required function signature is `std::size_t my_strlen(const char* s)`.

Verification method: compare whether the results of `my_strlen("hello world")` and `std::strlen("hello world")` are consistent.

### Exercise 2: Reverse an Array with Two Pointers

We already demonstrated reversing an array with two pointers in the hands-on code above. Now try encapsulating it into a function `void reverse_array(int* begin, int* end)`, where `end` is the past-the-end pointer. Note: the function does not need to know the array length internally; it can complete the reversal relying solely on the two pointers.

### Exercise 3: Implement String Comparison with Pointers

Implement `int my_strcmp(const char* a, const char* b)`: compare character by character, returning 0 if they are completely identical, a negative number if the first differing character in `a` is less than the corresponding character in `b`, and a positive number otherwise. This is a slightly more challenging exercise that requires traversing two strings simultaneously and judging the termination condition.

---

> **Next up**: Pointers are powerful, but they are also dangerous. Next, we will get to know "references"—a safer alternative provided by C++ that can replace raw pointers in many scenarios, making code both safe and clear.
