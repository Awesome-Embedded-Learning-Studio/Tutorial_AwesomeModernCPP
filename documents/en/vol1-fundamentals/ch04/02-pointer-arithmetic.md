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
- Pointer Basics
reading_time_minutes: 20
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Pointer Arithmetic and Arrays
translation:
  source: documents/vol1-fundamentals/ch04/02-pointer-arithmetic.md
  source_hash: 26f4cb9285d282a0a299c1635baa3d6e9789f7373207541e35fd2153d3548c18
  translated_at: '2026-09-25T10:38:05+00:00'
  engine: anthropic
  token_count: 4600
---
# Pointer Arithmetic and Arrays: p + 1 Adds More Than Just One Byte

If you have already made peace with the idea that "a pointer is an address," then the next fact we have to face runs deeper: in C++, pointers and arrays are tied together extremely tightly at the lowest mechanical level. (I strongly recommend against confusing the concepts of pointers and arrays, because that will only hurt you in engineering logic.)

In this chapter we string together pointer arithmetic, array-to-pointer decay, and pointer operations on C-style strings. If arrays and pointers have always struck you as "clearly related, but you can't quite say how," this chapter will pin it down.

## An Array Name Is Not a Pointer, But It Almost Always Decays Into One

Let's begin with the most classic move of all: declare an array, then assign its name to a pointer:

```cpp
#include <iostream>

int main()
{
    int arr[5] = {10, 20, 30, 40, 50};
    int* p = arr;  // Legal! An array name can be assigned directly to a pointer

    std::cout << "arr 的地址:  " << arr << "\n";
    std::cout << "p 的值:      " << p << "\n";
    std::cout << "arr[0] 的地址: " << &arr[0] << "\n";
    std::cout << "*p:          " << *p << "\n";

    return 0;
}
```

```output:no-line-numbers
arr 的地址:  0x7ffd3a2b1c00
p 的值:      0x7ffd3a2b1c00
arr[0] 的地址: 0x7ffd3a2b1c00
*p:          10
```

All three addresses are exactly the same. This brings us to one of the most important concepts in C++: **array-to-pointer decay**. When we write the name `arr`, in the vast majority of contexts the compiler does not treat it as "the entire array," but as "a pointer to the array's first element"—that is, `&arr[0]`.

So, strictly speaking, the sentence "an array name is a pointer" is wrong. The type of `arr` is `int[5]`, a complete array type that holds 5 `int`s and occupies 20 bytes. But the moment we use it in a context that wants a pointer (assigning it to an `int*`, passing it to a function, doing arithmetic on it), the compiler automatically decays it into an `int*`. This decay is irreversible: once decayed, there is no way back, and the array's length information is lost along with it.

> Since I said "the vast majority of contexts," when does it not decay? Only three situations hand us the whole array: `sizeof(arr)` returns the size of the entire array; `&arr` yields a "pointer to the array" (of type `int(*)[5]`, not `int*`); and initializing a character array from a string literal. Everywhere else, the array name decays, full stop.

## Pointer Addition and Subtraction—Stepping by Elements, Not Bytes

Arithmetic is one of the pointer's most powerful abilities. But the rules here are not quite what everyday intuition suggests: adding 1 to a pointer moves it by **the size of one element of the pointed-to type**, not by 1 byte.

### What Pointer Addition Actually Does

Let's look straight at the code and compare the stepping of `int*` versus `char*`:

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

```output:no-line-numbers
=== int* 步进 ===
pi:     0x7ffd4e3a1c00 -> *pi = 100
pi + 1: 0x7ffd4e3a1c04 -> *(pi+1) = 200
pi + 2: 0x7ffd4e3a1c08 -> *(pi+2) = 300

=== char* 步进 ===
pc:     0x7ffd4e3a1bf0 -> *pc = A
pc + 1: 0x7ffd4e3a1bf1 -> *(pc+1) = B
pc + 2: 0x7ffd4e3a1bf2 -> *(pc+2) = C
```

Watch the address deltas. Each `+1` on an `int*` bumps the address by 4 (from `...c00` to `...c04`), while each `+1` on a `char*` bumps it by only 1 (from `...bf0` to `...bf1`). This is the core rule of pointer arithmetic: **`p + n` actually moves `n * sizeof(*p)` bytes**. The compiler works out the real byte offset automatically from the type the pointer points to—we never need to multiply by `sizeof` by hand.

This stepping sequence is available as an animation: you can play it, pause it, or single-step through it to see clearly how the address changes each time `pi` gains 1 and which slot the dereference lands on:

<Anim id="pointer-arithmetic" />

> For the `char*` output we forced hexadecimal address printing with `static_cast<void*>`, because `std::ostream` gives `char*` special treatment—it assumes it is a C string and keeps printing until it runs into a `'\0'`. We will meet this pit again shortly.

### Pointer Subtraction—Measuring Distance in Elements

Subtract two pointers that point into the same array, and what you get is how many elements apart they sit (not a byte count):

```cpp
int arr[5] = {10, 20, 30, 40, 50};
int* p1 = &arr[1];  // points at 20
int* p2 = &arr[4];  // points at 50

std::cout << "p2 - p1 = " << (p2 - p1) << "\n";  // 3
```

The result of `p2 - p1` is 3, because exactly 3 elements separate `arr[1]` from `arr[4]`. This property is extremely useful in many algorithms—for example, to compute an element's index within an array, all we need is `ptr - arr`.

> Pointer subtraction is only valid between two pointers that point into the **same array** (or the same contiguous block of memory). Subtract two completely unrelated pointers and the result is undefined behavior—and the compiler may not even warn us.

## Traversing an Array with Pointers

Since `arr + i` is exactly `&arr[i]`, we can perfectly well walk the array from head to tail with a pointer, no subscripts required:

```cpp
#include <iostream>

int main()
{
    int arr[5] = {10, 20, 30, 40, 50};

    // Pointer traversal
    std::cout << "指针遍历: ";
    for (int* p = arr; p != arr + 5; ++p) {
        std::cout << *p << " ";
    }
    std::cout << "\n";

    // Subscript traversal
    std::cout << "下标遍历: ";
    for (int i = 0; i < 5; ++i) {
        std::cout << arr[i] << " ";
    }
    std::cout << "\n";

    // range-for traversal
    std::cout << "range-for: ";
    for (int x : arr) {
        std::cout << x << " ";
    }
    std::cout << "\n";

    return 0;
}
```

```output:no-line-numbers
指针遍历: 10 20 30 40 50
下标遍历: 10 20 30 40 50
range-for: 10 20 30 40 50
```

Wow, all three versions look the same! So here comes the question: which one should we use?

In day-to-day development, **prefer range-for**. It is the most concise, the least error-prone, and after compiler optimization its performance is exactly the same as pointer traversal. Pointer traversal earns its keep in scenarios that need finer control—for example, when you only want to walk part of the array (starting from the first element that meets some condition), or when you need to manipulate several positions at once. But for one clean pass over the entire array, range-for is the best choice. Let's compress the choice among the three into a single sentence: whole-range traversal goes to range-for; segmented or interleaved manipulation is where pointers come out.

> A very common trap sits right here: the "one-past-the-end pointer" `arr + 5` is legal, and we may compare against it, but we must **never dereference it**. `*(arr + 5)` is undefined behavior, because the position it points to already lies outside the array's bounds. The C++ standard only allows computing this address; it does not allow reading or writing what it points to. This is the same idea as the `end()` iterator of standard library containers: it marks "the position after the last element" and is not itself a valid element.

## Pointers and C-Style Strings

A C-style string is at heart nothing more than a `char` array terminated by `'\0'` (the null character). Since it is an array, everything we said about the relationship between pointers and arrays applies here. A string literal like `"hello"` written in C++ code has type `const char[6]` (5 characters plus 1 `'\0'`) and decays to `const char*` in most contexts.

```cpp
#include <iostream>

int main()
{
    const char* s = "hello";

    std::cout << "字符串: " << s << "\n";
    std::cout << "首字符: " << *s << "\n";
    std::cout << "第3个字符: " << s[2] << "\n";

    // Compute the string length by hand—emulating strlen
    std::size_t len = 0;
    while (s[len] != '\0') {
        ++len;
    }
    std::cout << "长度: " << len << "\n";

    return 0;
}
```

```output:no-line-numbers
字符串: hello
首字符: h
第3个字符: l
长度: 5
```

Now let's rewrite this length computation in pure pointer style, without a single subscript:

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

This pattern is everywhere inside implementations of the C standard library. Functions like `strlen`, `strcpy`, and `strchr` all rest on this kind of pointer traversal at the bottom—start at the beginning, walk one character at a time, and stop when you hit a `'\0'`. The `s - start` step exploits the pointer subtraction we covered earlier and directly yields how many elements were crossed along the way.

> Here is another classic trap: `const char* s = "hello";` makes `s` point at a string literal. String literals are stored in the program's read-only data segment, so **we must never modify the content through this pointer**. `s[0] = 'H';` causes undefined behavior—on most systems it triggers a segmentation fault right away. If you need a modifiable string, use a character array: `char s[] = "hello";` copies the content into an array on the stack, and modifying it is then safe.

## The Essence of the Subscript Operator

Now that the groundwork is in place, we can state one thing plainly: **the `[]` operator is essentially syntactic sugar for pointer arithmetic**.

Look at what the compiler actually does with `arr[n]`: `*(arr + n)`—first add the offset `n` to the pointer `arr`, then dereference. Because an array name decays into a pointer inside expressions, the whole process is pure pointer manipulation. This also explains why an array loses its length after being passed to a function: the function receives nothing but a pointer, so `sizeof` can only produce the size of the pointer itself, not of the original array.

Since `arr[n]` is `*(arr + n)`, and addition is commutative, we can derive that `n[arr]`—that is, `*(n + arr)`—is completely equivalent. Yes, writing `5[arr]` is legal and behaves exactly the same as `arr[5]`.

```cpp
int arr[5] = {10, 20, 30, 40, 50};

std::cout << arr[3] << "\n";  // 40
std::cout << 3[arr] << "\n";  // Also 40—but this is pure trivia, so never write it in real code
```

We bring up this bit of trivia to deepen understanding: **subscripting is just pointer addition plus a dereference**—there is no extra mechanism behind it. Once that truly clicks, many things that used to look strange explain themselves—for example, why `sizeof` goes wrong after an array is passed as an argument, and why negative subscripts are legal in certain scenarios (`p[-1]` is simply `*(p - 1)`, provided you guarantee that `p - 1` points at valid memory).

## Multidimensional Arrays and Pointers—Just a Taste

Multidimensional arrays are the most headache-inducing part of the pointer-array relationship. Let's look at one simple example—just a taste, no deep dive:

```cpp
int matrix[3][4] = {
    {1,  2,  3,  4},
    {5,  6,  7,  8},
    {9, 10, 11, 12}
};

int (*row_ptr)[4] = matrix;  // A pointer to "an array of 4 ints"

std::cout << row_ptr[1][2] << "\n";  // 7
```

The type of `matrix` is `int[3][4]`; after decay it becomes a pointer to the first row, of type `int(*)[4]`—a "pointer to an array of 4 `int`s". Note that the parentheses in `(*row_ptr)` are mandatory: `[]` binds tighter than `*`, and `int* row_ptr[4]` declares an "array of 4 `int*`s"—an entirely different thing.

The pointer relationships of multidimensional arrays really are a bit twisty, and feeling dizzy right now is perfectly fine: in real projects, directly manipulating multidimensional arrays through raw pointers is rare, and once we get to `std::array` and `std::span` later, safer ways to handle this kind of problem will be available.

## In Practice: The Complete ptr_arith.cpp Demo

Let's fold everything covered earlier into one complete program, spanning pointer traversal, distance via pointer subtraction, and C-string manipulation with pointers:

```cpp
#include <cstddef>
#include <iostream>

int main()
{
    // --- 1. Traversing the array in several ways ---
    int data[6] = {5, 12, 7, 23, 18, 9};

    std::cout << "=== 指针遍历 ===\n";
    for (int* p = data; p != data + 6; ++p) {
        std::cout << *p << " ";
    }
    std::cout << "\n";

    // --- 2. Element distance via pointer subtraction ---
    int* first = &data[0];
    int* last  = &data[5];
    std::cout << "\n=== 指针距离 ===\n";
    std::cout << "first 和 last 之间隔了 "
              << (last - first) << " 个元素\n";

    // Find the index of a value using pointer subtraction
    int target = 23;
    for (int* p = data; p != data + 6; ++p) {
        if (*p == target) {
            std::cout << "值 " << target << " 的下标是: "
                      << (p - data) << "\n";
            break;
        }
    }

    // --- 3. Implementing strlen with pointers ---
    const char* msg = "pointer";
    const char* scan = msg;
    while (*scan != '\0') {
        ++scan;
    }
    std::cout << "\n=== 手写 strlen ===\n";
    std::cout << "\"" << msg << "\" 的长度: "
              << (scan - msg) << "\n";

    // --- 4. Reversing the array with pointers ---
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

```output:no-line-numbers
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

This program ties together every core point of the chapter: pointer traversal, pointer subtraction for distance, pointer-scanning a C string, and in-place array reversal with the two-pointer technique. That "two-pointer" trick for reversing an array—two pointers starting at the head and the tail, walking toward the middle and swapping as they go—is one we will run into again and again in interviews and algorithm problems.

## Exercises

### Exercise 1: Writing strlen by Hand

Please implement string-length computation with pure pointers, using no standard library functions. The function signature is `std::size_t my_strlen(const char* s)`.

How to verify: we compare whether `my_strlen("hello world")` agrees with the result of `std::strlen("hello world")`.

::: details Reference Solution

```cpp
#include <iostream>
#include <cstring>

constexpr std::size_t my_strlen(const char *s)
{
    const char *msg = s;
    while ((*msg) != '\0')
    {
        msg++;
    }
    return msg - s;
}

int main()
{
    constexpr const char *test = "Hello, World!";
    constexpr std::size_t length1 = my_strlen(test);
    std::size_t length2 = std::strlen(test);
    std::cout << "my_strlen(Hello, World)的长度是: " << length1 << std::endl;
    std::cout << "std::strlen(Hello, World)的长度是: " << length2 << std::endl;
    if (length1 == length2)
    {
        std::cout << "两个长度相等" << std::endl;
    }
    else
    {
        std::cout << "两个长度不相等" << std::endl;
    }
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

```output:no-line-numbers
my_strlen(Hello, World)的长度是: 13
std::strlen(Hello, World)的长度是: 13
两个长度相等
```

:::

### Exercise 2: Reversing an Array with Two Pointers

We already demonstrated two-pointer reversal in the practice code above. Now please wrap it into a function `void reverse_array(int* begin, int* end)`, where `end` is the one-past-the-end pointer. Note: the function body has no need to know the array length—two pointers alone are enough to complete the reversal.

::: details Reference Solution

```cpp
#include <iostream>

void reverse_array(int *begin, int *end)
{
    --end;
    while (begin < end)
    {
        int temp = *begin;
        *begin = *end;
        *end = temp;
        begin++;
        end--;
    }
}

int main()
{
    int data[6] = {5, 12, 7, 23, 18, 9};
    std::cout << "反转前的数组: " << std::endl;
    for (int x : data)
    {
        std::cout << x << " ";
    }
    reverse_array(data, data + 6);
    std::cout << "\n反转后的数组: " << std::endl;
    for (int x : data)
    {
        std::cout << x << " ";
    }
    std::cout << std::endl;
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

```output:no-line-numbers
反转前的数组:
5 12 7 23 18 9
反转后的数组:
9 18 23 7 12 5
```

:::

### Exercise 3: String Comparison via Pointers

Please implement `int my_strcmp(const char* a, const char* b)`: compare character by character; return 0 if the strings are completely identical; return a negative number if `a`'s first differing character is less than the corresponding character in `b`; otherwise return a positive number. This is a slightly harder exercise—you need to walk two strings at the same time and get the termination condition right.

::: details Reference Solution

```cpp
#include <iostream>

constexpr int my_strcmp(const char *a, const char *b)
{
    // Note: the standard strcmp does not check for null pointers (passing one in is undefined behavior)
    // The null-pointer check here is an extra safety measure; -2 signals a bad argument
    if (a == nullptr || b == nullptr)
    {
        return -2;
    }

    while (*a != '\0' && *b != '\0')
    {
        // Casting to unsigned char ensures the character comparison is correct
        // and avoids comparison errors caused by negative values of a signed char
        const unsigned char byte_a = static_cast<unsigned char>(*a);
        const unsigned char byte_b = static_cast<unsigned char>(*b);
        if (byte_a != byte_b)
        {
            return byte_a < byte_b ? -1 : 1;
        }
        ++a;
        ++b;
    }

    const unsigned char byte_a = static_cast<unsigned char>(*a);
    const unsigned char byte_b = static_cast<unsigned char>(*b);
    if (byte_a == byte_b)
    {
        return 0;
    }

    return byte_a < byte_b ? -1 : 1;
}

int main()
{
    constexpr char test1[] = "Hello, Worlg!";
    constexpr char test2[] = "Hello, World!";
    constexpr int result = my_strcmp(test1, test2);
    switch (result)
    {
    case 0:
        std::cout << "test1与test2相等" << std::endl;
        break;
    case 1:
        std::cout << "test1>test2" << std::endl;
        break;
    case -1:
        std::cout << "test1<test2" << std::endl;
        break;
    case -2:
        std::cout << "字符串指针不能为空" << std::endl;
        break;
    default:
        break;
    }
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

> Just swap the `g` in `Hello, Worlg!` for a different letter and you will see different results

```output:no-line-numbers
test1>test2
```

:::

---

> **Up next**: Pointers are powerful, but they are dangerous too. Next we meet "references"—a safer alternative provided by C++ that can replace raw pointers in many scenarios and keep the code both safe and clear.
