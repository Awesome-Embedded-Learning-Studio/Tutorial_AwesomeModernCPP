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
  source_hash: 9a9640d81ea871a737f948b9ca3ac263ab4911b65a7f7058b261eef2e2042199
  translated_at: '2026-06-16T03:42:53.293112+00:00'
  engine: anthropic
  token_count: 2578
---
# Pointer Arithmetic and Arrays

If you have already grasped the fact that "a pointer is an address," then we must now face a deeper truth—in C++, pointers and arrays are, **at their very core**, almost two sides of the same coin. (I strongly advise against confusing the concepts of pointers and arrays, as doing so will only lead to trouble in engineering logic.)

In this chapter, we will connect pointer arithmetic, array-to-pointer decay, and C-style string pointer operations. If you previously felt there was a vague connection between arrays and pointers that you couldn't quite articulate, today we will untie that knot completely.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Understand the mechanism and trigger conditions for array-to-pointer decay.
> - [ ] Master the relationship between the actual byte count and element count in pointer addition and subtraction.
> - [ ] Use pointers to traverse arrays and C-style strings.
> - [ ] Understand that the subscript operator is essentially syntactic sugar for pointer arithmetic.

## Environment Setup

We will conduct all subsequent experiments in the following environment:

- Platform: Linux x86\_64 (WSL2 is also acceptable).
- Compiler: GCC 13+ or Clang 17+.
- Compiler flags: `-std=c++23 -Wall -Wextra -pedantic`

## An Array Name is Not a Pointer—But It Does a Good Impression

Let's start with a classic operation. We declare an array and assign its name to a pointer:

```cpp
int arr[5] = {10, 20, 30, 40, 50};
int* p = arr;  // Can we do this?

std::cout << "arr address: " << arr << '\n';
std::cout << "p address:   " << p << '\n';
std::cout << "&arr[0]:     " << &arr[0] << '\n';
```

Output:

```text
arr address: 0x7ffc1e2e4b90
p address:   0x7ffc1e2e4b90
&arr[0]:     0x7ffc1e2e4b90
```

All three addresses are identical. This brings us to a crucial concept in C++—**array-to-pointer decay**. When you write the name `arr` in most contexts, the compiler doesn't treat it as "the entire array," but rather as "a pointer to the first element of the array," which is `&arr[0]`.

Strictly speaking, the statement "an array name is a pointer" is incorrect. The type of `arr` is `int[5]`; it is a complete array type containing five `int` values, occupying 20 bytes. However, once you use it in a context requiring a pointer (such as assigning to `int* p`, passing it to a function, or performing arithmetic), the compiler automatically decays it to `&arr[0]`. This decay process is irreversible—once decayed, you cannot go back, and you lose the array length information.

> I mentioned "most contexts." So when does it **not** decay? There are only three cases: `sizeof(arr)` returns the size of the entire array; `&arr` yields a "pointer to the array" (type is `int(*)[5]`, not `int*`); and when initializing a character array with a string literal. Aside from these, the array name always decays.

## Pointer Arithmetic—Stepping by Elements, Not Bytes

One of the most powerful capabilities of pointers is arithmetic. However, the rules here differ from our usual understanding—adding 1 to a pointer does not move it by 1 byte, but by **the size of the type it points to**.

### The Actual Effect of Pointer Addition

Let's look directly at the code to compare the step size of `int*` and `char*`:

```cpp
int nums[] = {10, 20, 30};
char chars[] = {'A', 'B', 'C'};

int* pi = nums;
char* pc = chars;

std::cout << "int pointer:\n";
std::cout << "  pi      : " << static_cast<void*>(pi) << '\n';
std::cout << "  pi + 1  : " << static_cast<void*>(pi + 1) << '\n';

std::cout << "char pointer:\n";
std::cout << "  pc      : " << static_cast<void*>(pc) << '\n';
std::cout << "  pc + 1  : " << static_cast<void*>(pc + 1) << '\n';
```

Output:

```text
int pointer:
  pi      : 0x7ffc1e2e4b80
  pi + 1  : 0x7ffc1e2e4b84
char pointer:
  pc      : 0x7ffc1e2e4b70
  pc + 1  : 0x7ffc1e2e4b71
```

Notice the difference in addresses. For `pi`, adding 1 increases the address by 4 (from `...b80` to `...b84`), while for `pc`, adding 1 increases the address by only 1 (from `...b70` to `...b71`). This is the core rule of pointer arithmetic: **`p + 1` actually moves `sizeof(T)` bytes**. The compiler automatically calculates the actual byte offset based on the pointer's target type, so you don't need to manually multiply by `sizeof(int)`.

> We used `static_cast<void*>` to force printing the address in hexadecimal for `std::cout`. The reason is that `std::cout` has special handling for `char*`—it treats it as a C-style string and prints characters until it hits a `\0` (null terminator). We will encounter this pitfall again shortly.

### Pointer Subtraction—Calculating Element Distance

Two pointers pointing to the same array can be subtracted. The result is the number of elements separating them (not the number of bytes):

```cpp
int arr[] = {10, 20, 30, 40, 50};
int* p1 = &arr[1]; // Points to 20
int* p2 = &arr[4]; // Points to 50

std::cout << "Distance: " << (p2 - p1) << '\n'; // Output: 3
```

The result of `p2 - p1` is 3, because there are 3 elements between `arr[1]` and `arr[4]`. This feature is very useful in many algorithms—for example, to calculate the index of an element within an array, you simply need `ptr - array_base`.

> Pointer subtraction is only valid for **two pointers pointing to the same array (or the same contiguous memory block)**. If you subtract two unrelated pointers, the result is undefined behavior, and the compiler might not even warn you.

## Traversing Arrays with Pointers

Since `*(arr + i)` is equivalent to `arr[i]`, we can traverse the array from start to finish using pointers without needing subscripts:

```cpp
int arr[] = {10, 20, 30, 40, 50};

// Method 1: Subscript
for (size_t i = 0; i < 5; ++i) {
    std::cout << arr[i] << ' ';
}
std::cout << '\n';

// Method 2: Range-based for
for (int x : arr) {
    std::cout << x << ' ';
}
std::cout << '\n';

// Method 3: Pointer traversal
int* p = arr;
while (p < arr + 5) { // Compare with "past-the-end" pointer
    std::cout << *p << ' ';
    ++p;
}
std::cout << '\n';
```

Output:

```text
10 20 30 40 50
10 20 30 40 50
10 20 30 40 50
```

The results of all three methods are identical. So, which one should you use?

Honestly, in daily development, **prioritize range-based for**. It is the most concise and least error-prone, and with compiler optimizations, performance is identical to pointer traversal. The advantage of pointer traversal lies in scenarios requiring finer control—for instance, if you only need to traverse part of an array (starting from an element meeting a specific condition) or if you need to manipulate multiple positions simultaneously. But if you just need to go through the entire array, range-based for is the best choice.

> Here is a very common pitfall: the "past-the-end pointer" `arr + 5` is valid; you can use it for comparison, but **you must absolutely never dereference it**. `*(arr + 5)` is undefined behavior because it points to a location outside the array's bounds. The C++ standard only allows you to calculate this address, not read from or write to the content it points to. This follows the same logic as the `end()` iterator in standard library containers—it marks "one past the last element" and is not a valid element itself.

## Pointers and C-Style Strings

A C-style string is essentially a `char` array ending with a `\0` (null character). Since it is an array, all relationships regarding pointers and arrays apply here. When we write a string literal like `"hello"` in C++, its type is `const char[6]` (5 characters plus 1 `\0`), which decays to `const char*` in most contexts.

```cpp
const char* str = "hello"; // str points to the read-only literal

// Standard library method
std::cout << "Length (strlen): " << std::strlen(str) << '\n';
```

Output:

```text
Length (strlen): 5
```

Now, let's rewrite this length calculation using pure pointers, without using any subscripts:

```cpp
size_t my_strlen(const char* str) {
    const char* p = str;
    while (*p != '\0') {
        ++p;
    }
    return p - str;
}
```

This pattern is ubiquitous in C standard library implementations. Functions like `strlen`, `strcpy`, and `strcmp` all rely on similar pointer traversal underneath—starting from the beginning and moving character by character until hitting `\0`. `my_strlen` utilizes the pointer subtraction we discussed earlier to directly obtain the number of elements spanned.

> Here is another classic pitfall: `const char* str = "hello";` causes `str` to point to a string literal. String literals are stored in the read-only data segment of the program, so **you must absolutely never modify the content through this pointer**. `str[0] = 'H'` triggers undefined behavior—on most systems, it will cause a segmentation fault immediately. If you need a modifiable string, use a character array `char str[] = "hello";` instead. This copies the content to an array on the stack, making modification safe.

## The Essence of the Subscript Operator

Now we have enough groundwork to reveal a truth: **the `[]` operator is essentially syntactic sugar for pointer arithmetic**.

When the compiler sees `arr[i]`, what it actually does is `*(arr + i)`. It adds the offset `i` to the pointer `arr`, then dereferences it. Since the array name decays to a pointer in an expression, the whole process is purely a pointer operation. This also explains why the array length is lost after being passed to a function—the function receives just a pointer, and `sizeof(arr)` only yields the size of the pointer itself, not the original array size.

Since `arr[i]` is `*(arr + i)`, and addition is commutative, then `arr[i]` is also `*(i + arr)`—completely equivalent. Yes, writing `i[arr]` is legal and has the exact same effect as `arr[i]`.

```cpp
int arr[] = {10, 20, 30};
std::cout << arr[2] << '\n'; // 30
std::cout << 2[arr] << '\n'; // 30 (Yes, this compiles!)
```

We mention this trivia not to encourage showing off in code, but to deepen understanding: **subscripts are never magic; they are just pointer addition plus dereferencing**. Once you truly understand this, many previously puzzling phenomena make sense—like why `sizeof` fails on array parameters, or why negative subscripts are legal in certain scenarios (`arr[-1]` is `*(arr - 1)`, provided you ensure `arr - 1` points to valid memory).

## Multidimensional Arrays and Pointers—Just a Taste

Multidimensional arrays are the part of the pointer-array relationship most likely to cause headaches. Let's give a simple example here, just to touch on it without going too deep:

```cpp
int matrix[3][4] = {
    {1, 2, 3, 4},
    {5, 6, 7, 8},
    {9, 10, 11, 12}
};

int (*p_row)[4] = matrix; // Pointer to an array of 4 ints
```

The type of `matrix` is `int[3][4]`. After decay, it becomes a pointer to the first row, with the type `int(*)[4]`—"pointer to an array of 4 `int`s". Note that the parentheses in `int (*p_row)[4]` are mandatory because `[]` has higher precedence than `*`. `int* p_row[4]` would declare an "array of 4 `int*` pointers," which is a completely different thing.

The pointer relationships in multidimensional arrays are indeed convoluted. If you feel a bit dizzy right now, don't worry—scenarios in actual projects requiring raw pointer manipulation of multidimensional arrays are rare. Later, when we learn `std::array` and `std::mdspan`, we will have safer ways to handle such problems.

## Practice: Comprehensive Demo `ptr_arith.cpp`

Let's integrate the content discussed above into a complete program, covering pointer traversal, pointer subtraction for distance, and operating on C-style strings with pointers:

```cpp
#include <iostream>
#include <cstring>

// Calculate string length using pointers
size_t my_strlen(const char* str) {
    const char* p = str;
    while (*p) {
        ++p;
    }
    return p - str;
}

// Reverse an array in-place using two pointers
void reverse(int* begin, int* end) {
    // 'end' is a past-the-end pointer
    int* start = begin;
    int* finish = end - 1; // Point to the last valid element

    while (start < finish) {
        // Swap
        int temp = *start;
        *start = *finish;
        *finish = temp;

        // Move pointers towards center
        ++start;
        --finish;
    }
}

int main() {
    // 1. Pointer traversal and subtraction
    int arr[] = {10, 20, 30, 40, 50};
    int* p_begin = arr;
    int* p_end = arr + 5; // Past-the-end pointer

    std::cout << "Array elements: ";
    for (int* p = p_begin; p != p_end; ++p) {
        std::cout << *p << " ";
    }
    std::cout << "\n";

    std::cout << "Distance between first and last: " << (p_end - 1 - p_begin) << "\n";

    // 2. C-style string pointer operations
    const char* text = "Embedded";
    std::cout << "String: " << text << "\n";
    std::cout << "Length (std::strlen): " << std::strlen(text) << "\n";
    std::cout << "Length (my_strlen):   " << my_strlen(text) << "\n";

    // 3. In-place array reversal
    reverse(arr, arr + 5);
    std::cout << "Reversed array: ";
    for (int x : arr) {
        std::cout << x << " ";
    }
    std::cout << "\n";

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++23 -Wall -Wextra -pedantic ptr_arith.cpp -o ptr_arith
./ptr_arith
```

Output:

```text
Array elements: 10 20 30 40 50
Distance between first and last: 4
String: Embedded
Length (std::strlen): 8
Length (my_strlen):   8
Reversed array: 50 40 30 20 10
```

This program connects all the core knowledge points of this chapter: pointer traversal, pointer subtraction for distance, scanning C-style strings with pointers, and in-place array reversal using the two-pointer technique. The "two-pointer" technique for reversing arrays—where one pointer starts at the beginning and another at the end, moving inward while swapping—is a common guest in interviews and algorithm problems.

## Summary

Let's review the core points of this chapter:

- Array names **decay** into pointers to their first element in most expressions, losing length information once decayed.
- Pointer arithmetic steps by **the size of the pointed-to type**; `p + 1` actually moves `sizeof(T)` bytes.
- Two pointers pointing to the same array can be **subtracted**, yielding the number of elements between them.
- The subscript operator `[]` is essentially syntactic sugar for `*(ptr + i)`, which explains why `sizeof` fails on array parameters.
- C-style strings are `char` arrays ending with `\0`; pointer traversal until `\0` marks the end of the string.
- For daily array traversal, prioritize range-based for; use pointer traversal for scenarios requiring fine-grained control.

### Common Errors

| Error | Cause | Solution |
|------|------|----------|
| `sizeof` returns pointer size inside a function | Array decay; the function parameter is actually a pointer | Pass length as a separate parameter, or use `std::array`/`std::span` |
| Dereferencing past-the-end pointer `*(end)` | Past-the-end pointers are for comparison only, not access | Use `p != end` for loop conditions and avoid dereferencing `end` |
| Modifying string literals `str[0] = 'x'` | Literals are in the read-only segment; writing triggers a segfault | Copy to a stack array `char str[] = "..."` before modifying |
| Subtracting unrelated pointers | Two pointers must point to the same memory block | Always ensure pointers involved in arithmetic belong to the same array |

## Exercises

### Exercise 1: Implement `strlen` by Hand

Implement string length calculation using pure pointers without any standard library functions. Required function signature: `size_t my_strlen(const char* str)`.

Verification: Compare the results of `my_strlen("Embedded")` and `std::strlen("Embedded")`.

### Exercise 2: Two-Pointer Array Reversal

We demonstrated the two-pointer reversal in the practical code above. Now try to encapsulate it into a function `void reverse(int* begin, int* end)`, where `end` is a past-the-end pointer. Note: The function does not need to know the array length; it can complete the reversal relying only on the two pointers.

### Exercise 3: String Comparison Using Pointers

Implement `int my_strcmp(const char* s1, const char* s2)`: compare character by character. Return 0 if they are identical. If the first differing character in `s1` is less than the corresponding character in `s2`, return a negative number; otherwise, return a positive number. This is a slightly harder exercise requiring traversing two strings simultaneously and checking termination conditions.

---

> **Next Stop**: Pointers are powerful, but they are also dangerous. Next, we will meet "references"—a safer alternative provided by C++ that can replace raw pointers in many scenarios, making code both safe and clear.
