---
title: Pointers, Arrays, const, and Null Pointers
description: Gain a deep understanding of the array-to-pointer decay mechanism, the
  four combinations of `const` and pointers, and how to guard against NULL pointers
  and wild pointers, laying the foundation for learning C++ references and smart pointers.
chapter: 1
order: 10
tags:
- host
- cpp-modern
- beginner
- 入门
difficulty: beginner
platform: host
reading_time_minutes: 15
cpp_standard:
- 11
prerequisites:
- 指针入门：地址的世界
translation:
  source: documents/vol1-fundamentals/c_tutorials/07B-pointers-arrays-const.md
  source_hash: c144abb04048044a685d91c47cffedcbdb63dbdab3a4759354ea0d8607c927e4
  translated_at: '2026-04-20T03:23:00.704858+00:00'
  engine: anthropic
  token_count: 1750
---
# Pointers, Arrays, const, and Null Pointers

In the previous chapter, we mastered the basics of pointers—declaration, initialization, taking addresses, dereferencing, and pointer arithmetic. Now we tackle a few tricky but crucial applications: what exactly is the relationship between arrays and pointers, how many meanings can `const` and pointers combine to create, and why NULL pointers and wild pointers are so dangerous.

Let's take it one step at a time. There might seem to be a lot of content here, but the core logic is actually quite clear.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Understand the mechanism of array name decay to pointers, and its two exceptions
> - [ ] Correctly read and write the four combination declarations of `const` and pointers
> - [ ] Distinguish between NULL pointers and wild pointers, and master defensive techniques

## Environment Setup

All of our following experiments will be conducted in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c17`

## Step 1 — What Exactly Is an Array Name

### "Decay" — A Core Rule

In C, there is a very important rule: **in most contexts, an array name automatically decays into a pointer to its first element**. This rule sounds academic, but it's actually quite easy to understand—`numbers` itself represents an entire contiguous block of memory, but when you assign it to a pointer or pass it to a function, the compiler only passes the starting address of that block, and the array's length information is "lost".

```c
int numbers[5] = {1, 2, 3, 4, 5};
int* ptr = numbers;      // 合法：numbers 退化为 &numbers[0]
```

The type of `numbers` itself is `int[5]` (an array containing 5 ints), but when assigned to a pointer, it automatically converts to `int*` (a pointer to the first element). This means `numbers[i]` and `*(numbers + i)` are completely equivalent—the subscript operator `[]` is essentially syntactic sugar for pointer arithmetic.

Because of this, we can use pointers to traverse an array:

```c
int numbers[5] = {10, 20, 30, 40, 50};

for (int* p = numbers; p < numbers + 5; p++) {
    printf("%d ", *p);
}
```

Let's verify this by compiling and running:

```bash
gcc -Wall -Wextra -std=c17 array_ptr.c -o array_ptr && ./array_ptr
```

Output:

```text
10 20 30 40 50
```

### However — Arrays Are Not Pointers

Here lies the key point: an array name only "frequently decays into a pointer," but **an array itself is not a pointer**. There are two scenarios where an array name does not decay:

First, the `sizeof` operator. `sizeof(numbers)` returns the byte size of the entire array (5 × 4 = 20 bytes), not the size of a pointer (4 or 8 bytes). This is the technique we used in the previous chapter to calculate the number of array elements: `sizeof(numbers) / sizeof(numbers[0])`.

Second, the `&` operator. The type of `&numbers` is "a pointer to the entire array" (`int(*)[5]`), not "a pointer to a pointer" (`int**`). It has the same numeric value as `numbers` (both are the address of the array's first byte), but the types are different, and the step sizes for pointer arithmetic are also different.

Let's verify these differences:

```c
#include <stdio.h>

int main(void)
{
    int numbers[5] = {10, 20, 30, 40, 50};

    printf("sizeof(numbers)    = %zu（整个数组）\n", sizeof(numbers));
    printf("sizeof(&numbers)   = %zu（指针大小）\n", sizeof(&numbers));
    printf("numbers 的值       = %p\n", (void*)numbers);
    printf("&numbers 的值      = %p\n", (void*)&numbers);
    printf("numbers + 1        = %p（跳过一个 int）\n", (void*)(numbers + 1));
    printf("&numbers + 1       = %p（跳过整个数组）\n", (void*)(&numbers + 1));

    return 0;
}
```

Output:

```text
sizeof(numbers)    = 20（整个数组）
sizeof(&numbers)   = 8（指针大小）
numbers 的值       = 0x7ffd1234abcd
&numbers 的值      = 0x7ffd1234abcd
numbers + 1        = 0x7ffd1234abd1（跳过一个 int，+4）
&numbers + 1       = 0x7ffd1234abe1（跳过整个数组，+20）
```

Great, `numbers` and `&numbers` have the same numeric value, but `numbers + 1` only skips 4 bytes (one `int`), while `&numbers + 1` skips 20 bytes (the entire array). This is "different types, different step sizes."

> ⚠️ **Pitfall Warning**
> Once an array is passed to a function, it will definitely decay into a pointer—inside the function, `sizeof(arr)` returns the pointer size, not the array size. So if you need to know the array length inside a function, you must pass a length parameter separately.

## Step 2 — The Four Combinations of const and Pointers

The combinations of `const` and pointers are a classic interview topic, and they are also frequently used in actual coding. There are four combination methods in total, and we will break them down one by one, starting with the most intuitive.

### 1. A Non-const Pointer to const Data

```c
const int* p1 = &value;
// *p1 = 100;   // 错误：不能通过 p1 修改指向的数据
p1 = &other;    // 合法：指针本身可以指向别的地方
```

`const int*` means "the int that p1 points to is read-only"—you cannot modify that value through `p1`, but `p1` itself can point to other variables. Note that `value` itself doesn't necessarily have to be `const`; you are simply promising not to modify it through the `p1` pathway. This usage is extremely common in function parameters—`void process(const int* data)` tells the caller, "rest assured, I guarantee I won't touch your data."

### 2. A const Pointer to Non-const Data

```c
int* const p2 = &value;
*p2 = 100;      // 合法：可以修改指向的数据
// p2 = &other;  // 错误：指针本身不可变
```

The pointer itself is `const`—once initialized, it will always point to the same address, but you can use it to modify the data in that memory block. This usage is very common in embedded development, such as hardware register mapping at fixed addresses:

```c
volatile unsigned int* const kGpioBase = (volatile unsigned int*)0x40020000;
```

The value of the pointer (the address) remains fixed, but you can read and write the register through it.

### 3. A const Pointer to const Data

```c
const int* const p3 = &value;
// *p3 = 100;    // 错误
// p3 = &other;  // 错误
```

Both sides are locked down—the pointer cannot change direction, and the data cannot be modified through the pointer. This is typically used for accessing read-only hardware registers or constant lookup tables.

### 4. A Plain `int*`

This is the most ordinary `int* p`, where both sides can be modified, with no special constraints.

### How to Read These Declarations

A practical reading trick: look at whether `const` appears to the left or right of `*`.

- `const` to the **left** of `*`: modifies the **pointed-to data** (data is immutable)
- `const` to the **right** of `*`: modifies the **pointer itself** (direction is immutable)
- Both sides present: both are immutable

> ⚠️ **Pitfall Warning**
> Reading the declaration from right to left is also a good method: `const int* p` → "p is a pointer to int const" (a pointer to const int); `int* const p` → "p is a const pointer to int" (a const pointer to int).

## Step 3 — NULL Pointers and Wild Pointers

### NULL — "I'm Not Pointing to Anything"

`NULL` is a macro with the value `(void*)0`, meaning "not pointing to any valid memory address." Dereferencing a NULL pointer is undefined behavior (UB)—on most systems, it triggers a segmentation fault (SIGSEGV), and the program crashes immediately.

A segmentation fault sounds terrible, but it's actually a "good crash"—the problem is exposed immediately, and a quick look with a debugger tells you it's a NULL pointer dereference. In contrast, the wild pointer we're about to discuss is the truly scary thing.

### Wild Pointers — Time Bombs in Your Code

A wild pointer is a pointer that points to invalid memory. It usually comes from three sources:

The first is an **uninitialized pointer**—declared but not assigned a value, containing a random value on the stack, and this address could point anywhere. The second is a **dangling pointer**—the pointer once pointed to valid memory, but that memory has been freed (continuing to use the pointer after `free`). The third is **out-of-bounds access**—pointer arithmetic goes beyond the valid range.

```c
// 未初始化——最经典的野指针
int* wild;
*wild = 42;   // 未定义行为：往随机地址写入 42

// 悬空指针
int* dangling = (int*)malloc(sizeof(int));
free(dangling);
*dangling = 42;  // 未定义行为：内存已经释放了

// 好习惯：释放后置 NULL
dangling = NULL;
```

The terrifying thing about wild pointers is that they don't necessarily crash immediately—they might happen to point to a writable block of memory, and your program "appears" to run normally, but some unrelated variable has been quietly modified. The symptoms and the cause of this kind of bug can be worlds apart, making it incredibly frustrating to track down.

> ⚠️ **Pitfall Warning**
> Wild pointers create "Schrödinger's bugs"—in your program, everything might seem fine, until one day you switch compilers or turn on optimizations, and it suddenly crashes. Moreover, the crash location is often far from the actual bug, making it extremely painful to debug.

### Three Defensive Rules

The best defensive measures are actually quite simple—just remember these three rules:

1. **Initialize pointers immediately upon declaration**—even if you just initialize them to `NULL`
2. **Set to `NULL` immediately after `free`**—to prevent accidental misuse later
3. **Check if it is `NULL` before using a pointer**—add a layer of protection

```c
int* safe_ptr = NULL;

// ... 某处分配了内存 ...

if (safe_ptr != NULL) {
    *safe_ptr = 42;   // 安全：确认非空才使用
}
```

These three rules can help you avoid the vast majority of pointer-related disasters. I sincerely suggest: burn these three rules into your muscle memory, and you will save yourself a lot of hair loss when coding later.

## C++ Connection

C's raw pointers are powerful, but the responsibility falls entirely on the programmer. C++ builds on this foundation by doing a few very critical things.

First are **references**. A `int& r = value` is essentially a const pointer that the compiler automatically dereferences—it must be initialized at declaration, cannot be rebound once bound, and doesn't require `*` when used, making it syntactically identical to operating on the original variable directly. A reference cannot be NULL (well, strictly speaking, you can construct a dangling reference, but that's intentionally asking for trouble), and it cannot point to uninitialized memory. In C++, passing by reference is preferred over passing by pointer for function parameters.

Then there are **smart pointers**. `std::unique_ptr` and `std::shared_ptr` use the RAII mechanism to automatically manage memory lifecycles—memory is automatically released when the pointer goes out of scope, fundamentally eliminating memory leaks and dangling pointer issues caused by manual `malloc`/`free`.

```cpp
// C++ 智能指针——先睹为快
#include <memory>

std::unique_ptr<int> p = std::make_unique<int>(42);
// *p == 42，使用方式和原始指针一样
// 离开作用域时自动 delete，不需要手动释放
```

We will dive deep into these topics in subsequent C++ tutorials. For now, you just need to understand one core idea: **C++'s philosophy is to use the type system and object lifecycles for automatic management, rather than relying on the programmer's self-discipline**.

## Summary

Let's review the core points of this chapter. In most contexts, an array name decays into a pointer to its first element, but `sizeof` and `&` are two exceptions—in these scenarios, the array name retains its "array" identity. There are four combinations of `const` and pointers; just remember "const to the left of `*` modifies the data, and to the right modifies the pointer itself." Although a NULL pointer causes a segmentation fault, that is a "good crash"; wild pointers are the true time bombs, and remembering the three defensive rules (initialize upon declaration, set to NULL after free, check before use) will help you avoid the vast majority of disasters.

At this point, we have built a solid foundation in pointers. Next, we will learn about functions—how to organize code to make it more reusable and easier to maintain.

## Exercises

### Exercise 1: Pointer-Based Linear Search

Implement a linear search function that returns a pointer to the first occurrence of the target value in the array. If not found, return `NULL`.

```c
/// @brief 在 int 数组中线性搜索目标值
/// @param data 数组首元素地址
/// @param count 元素个数
/// @param target 要搜索的值
/// @return 指向目标元素的指针，未找到则返回 NULL
const int* linear_search(const int* data, size_t count, int target);
```

### Exercise 2: Pointer-Based Array Reversal

Implement an in-place array reversal function using only pointer arithmetic (two pointers moving from both ends toward the middle), without using array subscripts:

```c
/// @brief 原地反转 int 数组
/// @param data 数组首元素地址
/// @param count 元素个数
void reverse_array(int* data, size_t count);
```

### Exercise 3: const Practice

For each of the following declarations, determine which operations are legal and which will cause compilation errors:

```c
int value = 42, other = 100;

const int* p1 = &value;
int* const p2 = &value;
const int* const p3 = &value;

// 对每个指针 p1/p2/p3，判断以下操作是否合法：
// *px = 50;      // 通过指针修改数据
// px = &other;   // 修改指针指向
```

## References

- [cppreference: Pointer declarations](https://en.cppreference.com/w/c/language/pointer)
- [cppreference: NULL](https://en.cppreference.com/w/c/types/NULL)
- [cppreference: Array-to-pointer decay](https://en.cppreference.com/w/c/language/conversion)
