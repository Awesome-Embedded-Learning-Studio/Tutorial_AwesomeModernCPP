---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Understanding pointers from scratch: address-of, dereferencing, pointer
  types, and null pointers, mastering the core mechanisms of C++ memory access.'
difficulty: beginner
order: 1
platform: host
prerequisites:
- inline 与 constexpr 函数
reading_time_minutes: 11
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Pointer Basics
translation:
  source: documents/vol1-fundamentals/ch04/01-pointer-basics.md
  source_hash: 94f13fa343b86d0a5f8257d6e7a4804c90e3a68fbb786ca8859a9593d4d15035
  translated_at: '2026-06-16T05:57:55.527780+00:00'
  engine: anthropic
  token_count: 1996
---
# Pointer Basics

Pointers are likely the most famous, yet intimidating, feature in C++ that often discourages beginners. If you are coming from Python or Java, you might be used to the mindset that "a variable is the object itself"—the variable holds the data, and you just use it. However, C++ is different; it grants us the ability to manipulate memory addresses directly, and pointers are the gateway to this power.

Honestly, many people start feeling nervous the moment they hear the word "pointer." But in reality, a pointer is simply a variable that stores a memory address, nothing more. Understanding its essence is key to understanding how C++ views memory—every variable resides at a specific location in memory, that location has a number (an address), and pointers are used to record and manipulate these numbers. In this chapter, we will thoroughly cover the basics: taking addresses, dereferencing, pointer types, and null pointers, laying a solid foundation for pointer arithmetic, arrays, and dynamic memory management later on.

## First, Understand "Addresses"—The Numbers of Memory

Imagine a program's memory as a row of storage lockers, where each locker has a number and holds data inside. When we declare a variable, the compiler allocates several consecutive lockers for us, and the variable name acts as a label. We can use the `&` (address-of) operator to obtain the address number of a variable:

```cpp
// address_demo.cpp
#include <iostream>

int main()
{
    int x = 42;
    std::cout << "x 的值:   " << x << std::endl;
    std::cout << "x 的地址: " << &x << std::endl;
    return 0;
}
```

```bash
g++ -std=c++17 -Wall -Wextra -o address_demo address_demo.cpp && ./address_demo
```

The output is roughly:

```text
x 的值:   42
x 的地址: 0x7ffd4a3b2c5c
```

Hexadecimal numbers starting with `0x` represent the address of `x` in memory. The address may vary between runs, but one thing is certain: **every variable has a unique address, and `&` is the operator used to obtain it**. If we declare several variables and print their addresses, we will notice that the addresses of adjacent `int` variables differ by four—which is exactly the size of an `int`—because the stack grows towards lower memory addresses.

## Pointer Variables — Variables that Store Addresses

Since an address is just a number, we can naturally store it in a variable. This is a **pointer**—a variable that stores a memory address:

```cpp
int x = 42;
int* p = &x;   // p 存储 x 的地址
```

The `*` in the declaration indicates "this is a pointer", and `int*` is read as "pointer to int". We can think of a pointer like a slip of paper with an address written on it—the slip of paper itself is the variable `p`, the address is `&x`, and the value 42 lives inside the house at `x`.

Let's verify the relationship between the pointer and the original variable:

```cpp
int x = 42;
int* p = &x;

std::cout << "x 的值:   " << x << std::endl;   // 42
std::cout << "&x 的值:  " << &x << std::endl;   // 0x7ffd...
std::cout << "p 的值:   " << p << std::endl;     // 和 &x 一样
std::cout << "&p 的值:  " << &p << std::endl;    // 不同的地址
```

The value of `p` is exactly the same as `&x`—it indeed stores the address of `x`. However, `p` has its own address (`&p`), because a pointer itself is a variable and occupies memory.

> **Warning**: The result of `int* p1, p2;` is that `p1` is an `int*` while `p2` is an `int`—the `*` only modifies the variable immediately following it. To declare two pointers, we must write `int *p1, *p2;`. The best practice is to declare only one pointer per line.

## Dereferencing—Following the Address to Find Data

In a declaration, `*` indicates "this is a pointer," whereas in an expression, it means "fetch the data at this address"—the context changes the meaning. Through `*p`, we can read or even modify the variable pointed to:

```cpp
*p = 10;  // Modify the value of x via the pointer
```

```cpp
int x = 42;
int* p = &x;

std::cout << *p << std::endl;  // 42，读取
*p = 100;                       // 通过指针修改 x
std::cout << x << std::endl;   // 100
```

Instead of writing `x = 100` directly, we modified `x` indirectly via a pointer. This is the core capability of pointers—**indirect access**. `&` (address-of) and `*` (dereference) are inverse operations: `*&x` is `x`, and `&*p` is `p`.

## Pointer Types — Why `int*` and `double*` Are Not the Same

While an address is indeed just a number, the type information tells the compiler "what kind of data lives at this address"—specifically, how many bytes to read and how to interpret the binary content.

```cpp
// pointer_types.cpp
#include <iostream>

int main()
{
    int    i = 42;
    double d = 3.14;
    char   c = 'A';

    std::cout << "*(&i) = " << *(&i) << std::endl;  // 42
    std::cout << "*(&d) = " << *(&d) << std::endl;  // 3.14
    std::cout << "*(&c) = " << *(&c) << std::endl;  // A

    std::cout << "sizeof(int*):    " << sizeof(int*) << std::endl;    // 8
    std::cout << "sizeof(double*): " << sizeof(double*) << std::endl; // 8
    std::cout << "sizeof(char*):   " << sizeof(char*) << std::endl;   // 8
    return 0;
}
```

Two conclusions: dereferencing pointers of different types yields different value types, because the compiler interprets the binary data based on the pointer type. However, regardless of the target type, the pointer itself occupies 8 bytes on a 64-bit system—an address is just an address, merely recording a number.

> **Warning**: `int* p = &d;` (assigning the address of a `double` to an `int*`) will cause a compilation error; the compiler is protecting you. If you bypass this with a C-style cast—`int* p = (int*)&d;`—then `*p` will read out a meaningless number.

## Null Pointers—Pointing to Nothing

Sometimes we need a pointer but don't know where to point it yet, or a function needs to return a "not found" signal when a lookup fails. This requires a **null pointer**—a pointer that explicitly indicates "points to nothing." In C++98 and C, we used `NULL`. Anyone who has looked inside `stdlib.h` knows that this is just a cast of `(void*)0`. The `nullptr` introduced in C++11 is the only correct way to represent a null pointer in modern C++:

```cpp
int* p = nullptr;  // 不指向任何有效地址

if (p != nullptr) { // 也有朋友喜欢if(p)，这个是习惯，笔者只有在非常需要强调兄弟们这不是空指针的时候这样写。
    std::cout << *p << std::endl;
} else {
    std::cout << "p 是空指针，不能解引用" << std::endl;
}
```

> **Warning**: Dereferencing a null pointer results in **undefined behavior** (UB). The program might crash immediately (Segmentation Fault), output garbage data, or appear "normal" while data is silently corrupted. The syntax is perfectly legal, so the compiler won't catch it for you—make it a habit: **always check for null before dereferencing**.

In older code, you might see `NULL` or `0`, but `nullptr` has a key advantage: its type is `std::nullptr_t`, so it won't be confused with integers or cause incorrect matches during function overloading. Always use `nullptr`, and leave `NULL` to history.

## Pointers and const—A Quick Refresher

In previous chapters, we covered the three combinations of `const` and pointers. Let's do a quick review:

`const int* p` — A pointer to a constant. We cannot modify the data through `p`, but we can change where `p` points to:

```cpp
int x = 10, y = 20;
const int* p = &x;
// *p = 100;  // 编译错误
p = &y;       // 没问题
```

`int* const p` — a constant pointer; we cannot change where it points, but we can modify the data:

```cpp
int x = 10;
int* const p = &x;
*p = 100;      // 没问题
// p = &y;     // 编译错误
```

`const int* const p` — double `const`, neither can be changed. Reading tip: read from right to left. `const int* const p` reads as "p is a `const` pointer pointing to a `const int`".

## Common Pitfalls

The power of pointers comes with danger. Beginners almost inevitably fall into the following traps. Recognizing them early will save you significant debugging time.

### Uninitialized Pointers

Declaring a pointer without assigning a value leaves it with a garbage address. Dereferencing it is undefined behavior, and it can be even worse than a null pointer (a null pointer at least causes an immediate crash, whereas a garbage address might point to a valid memory area, leading to silent data corruption). **Initialize pointers immediately upon declaration**. If we don't know where it should point yet, assign `nullptr` for now.

### Returning Addresses of Local Variables

Local variables inside a function are allocated on the stack. After the function returns, the stack space is reclaimed. Returning a pointer to a local variable gives the caller a **dangling pointer** — the address still exists, but the data is no longer valid:

```cpp
int* get_value()
{
    int local = 42;
    return &local;  // 悬空指针！
}
```

Compiling with `-Wall` will issue `warning: address of local variable 'local' returned`, which we must take seriously.

### Double Free and Use-After-Free

These fall under the scope of dynamic memory management, which we will cover in detail later. The core principle is that memory allocated via `new` should be `delete`d exactly once. Freeing twice (double free) or continuing to use memory after it has been freed (use-after-free) are both serious forms of undefined behavior (UB).

> **Warning**: The three pitfalls above share a common root cause—pointers give you the ability to manipulate memory directly, but the compiler cannot verify correct usage in all scenarios. Consequently, pointer-related issues often only manifest at runtime, and the symptoms can be highly unstable (sometimes it runs fine, but crashes with different compiler options). Developing good pointer usage habits is far more efficient than debugging issues after they arise.

## Comprehensive Practice — pointers.cpp

Now, let's put everything together:

```cpp
// pointers.cpp —— 指针基础操作综合演示
#include <iostream>

/// @brief 通过指针交换两个变量的值
void swap_by_pointer(int* a, int* b)
{
    if (a == nullptr || b == nullptr) {
        return;
    }
    int temp = *a;
    *a = *b;
    *b = temp;
}

/// @brief 安全打印指针指向的值
void safe_print(const char* label, const int* p)
{
    std::cout << label;
    if (p != nullptr) {
        std::cout << *p << " (地址: " << p << ")" << std::endl;
    }
    else {
        std::cout << "(空指针)" << std::endl;
    }
}

int main()
{
    // 取地址与解引用
    int x = 42;
    int* p = &x;
    std::cout << "=== 取地址与解引用 ===" << std::endl;
    std::cout << "x = " << x << ", &x = " << &x << std::endl;
    std::cout << "p = " << p << ", *p = " << *p << std::endl;

    // 通过指针修改
    *p = 100;
    std::cout << "\n=== *p = 100 后 ===" << std::endl;
    std::cout << "x = " << x << std::endl;

    // 指针 swap
    int a = 10, b = 20;
    std::cout << "\n=== swap ===" << std::endl;
    std::cout << "交换前: a=" << a << ", b=" << b << std::endl;
    swap_by_pointer(&a, &b);
    std::cout << "交换后: a=" << a << ", b=" << b << std::endl;

    // 空指针检查
    std::cout << "\n=== 空指针 ===" << std::endl;
    int value = 99;
    safe_print("有效指针: ", &value);
    safe_print("空指针:   ", static_cast<int*>(nullptr));

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o pointers pointers.cpp && ./pointers
```

Expected output:

```text
=== 取地址与解引用 ===
x = 42, &x = 0x7ffd4a3b2c5c
p = 0x7ffd4a3b2c5c, *p = 42

=== *p = 100 后 ===
x = 100

=== swap ===
交换前: a=10, b=20
交换后: a=20, b=10

=== 空指针 ===
有效指针: 99 (地址: 0x7ffd4a3b2c4c)
空指针:   (空指针)
```

The address may vary with each run, but the value of `p` always matches `&x`. After the swap, the values are exchanged, and the null pointer is handled correctly. We recommend copying the code locally to compile and run it, so you can observe the address changes firsthand.

## Run Online

Run this comprehensive pointer basics example online to observe address-of operations, dereferencing, pointer swapping, and null pointer checks:

<OnlineCompilerDemo
  title="Comprehensive Pointer Basics: Address-of, Dereference, Swap, and Null"
  source-path="code/examples/vol1/10_pointer_basics.cpp"
  description="Run online and observe basic pointer operations. Try modifying the value pointed to by the pointer and observe the changes in the original variable."
  allow-run
/>

## Try It Yourself

### Exercise 1: Implement swap Manually and Observe Addresses

Declare two `int` variables, `a` and `b`. Print their values and addresses. Swap their values using pointers, then print again. Did the values change? Did the addresses change? Why?

### Exercise 2: Trace Pointer Values

Before running the code, trace the execution on paper to predict the results, then compile and verify:

```cpp
#include <iostream>

int main()
{
    int x = 10, y = 20;
    int* p = &x;
    int* q = &y;
    *p = *q;   // 把 q 指向的值赋给 p 指向的位置
    p = q;     // 让 p 和 q 指向同一个地方
    *p = 30;
    std::cout << "x = " << x << std::endl;
    std::cout << "y = " << y << std::endl;
    std::cout << "*p = " << *p << std::endl;
    std::cout << "*q = " << *q << std::endl;
    return 0;
}
```

Many developers stumble over the difference between `*p = *q` and `p = q` when doing this for the first time—the former assigns data, while the latter changes the pointer's target.

### Exercise 3: Fix Null Pointer Bugs

The code below contains three pointer-related bugs. Find and fix them:

```cpp
#include <iostream>

int* create_value()
{
    int val = 42;
    return &val;
}

int main()
{
    int* p;  // bug 1
    std::cout << *p << std::endl;

    int* q = create_value();  // bug 2
    std::cout << *q << std::endl;

    return 0;
}
```

## Summary

This chapter started with memory addresses and reviewed the core concepts of pointers. `&` obtains an address, a pointer is a variable that stores an address, and `*` dereferences a pointer to read or write data. The pointer's type determines how memory is interpreted during dereferencing, but the pointer itself is always 8 bytes on a 64-bit system. `nullptr` is the correct way to represent a null pointer in modern C++, and dereferencing a null pointer results in undefined behavior (UB). The three combinations of `const` and pointers control whether the data and the pointer itself are mutable. Uninitialized pointers, dangling pointers, and double frees are the three most common pitfalls.

In the next chapter, we will dive into the world of pointer arithmetic and arrays—what does adding 1 to a pointer actually mean, and what is the true relationship between an array name and a pointer? This knowledge will upgrade pointers from "variables storing addresses" to "tools for traversing memory."
