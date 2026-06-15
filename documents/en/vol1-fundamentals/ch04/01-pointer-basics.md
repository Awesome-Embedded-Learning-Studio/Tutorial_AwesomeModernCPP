---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Understanding pointers from scratch: taking addresses, dereferencing,
  pointer types, and null pointers, mastering the core mechanisms of C++ memory access.'
difficulty: beginner
order: 1
platform: host
prerequisites:
- inline 与 constexpr 函数
reading_time_minutes: 12
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Pointer Basics
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/ch04/01-pointer-basics.md
  source_hash: 77f7dcd7558781b1323aff07abc4b16765db0922fe21a387686c9bfcba9254d6
  token_count: 2000
  translated_at: '2026-05-26T10:47:33.444483+00:00'
---
# Pointer Basics

Pointers are probably the most notorious feature in C++, and the one most likely to scare off newcomers. If you have a background in Python or Java, you are probably used to thinking that "a variable is the object itself"—the variable holds the data, and you just use it. But C++ is different. It gives us the ability to directly manipulate memory addresses, and pointers are the gateway to that ability.

Honestly, many people start feeling nervous the moment they hear the word "pointer." But in reality, a pointer is simply a variable that stores a memory address—nothing more. Understanding its essence means understanding how C++ views memory—every variable resides at some location in memory, that location has a number (an address), and pointers are how we record and manipulate those numbers. In this chapter, we will thoroughly cover the fundamentals—taking addresses, dereferencing, pointer types, and null pointers—laying a solid foundation for the pointer arithmetic, arrays, and dynamic memory management that come later.

## Understanding "Address" First — The House Numbers of Memory

Imagine program memory as a row of storage lockers, each with a number, holding data inside. When you declare a variable, the compiler allocates a few consecutive lockers for you, and the variable name is the label. You can use the `&` (address-of) operator to get a variable's address number:

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

The output looks something like this:

```text
x 的值:   42
x 的地址: 0x7ffd4a3b2c5c
```

The hexadecimal number starting with `0x` is the address of `x` in memory. The address may differ each time you run the program, but one thing is certain: **every variable has a unique address, and `&` is the operator to get it**. If we declare a few more variables and print their addresses, we will find that the addresses of adjacent `int`s differ by 4—exactly the size of one `int`, because the stack grows toward lower addresses.

## Pointer Variables — Variables That Store Addresses

Since an address is just a number, we can naturally store it in a variable. This is a **pointer**—a variable that stores a memory address:

```cpp
int x = 42;
int* p = &x;   // p 存储 x 的地址
```

The `*` in the declaration means "this is a pointer," and `int*` is read as "pointer to int." You can think of a pointer as a sticky note with a house number written on it—the note itself is the variable `p`, the house number is `&x`, and the value 42 lives inside the house as `x`.

Let us verify the relationship between the pointer and the original variable:

```cpp
int x = 42;
int* p = &x;

std::cout << "x 的值:   " << x << std::endl;   // 42
std::cout << "&x 的值:  " << &x << std::endl;   // 0x7ffd...
std::cout << "p 的值:   " << p << std::endl;     // 和 &x 一样
std::cout << "&p 的值:  " << &p << std::endl;    // 不同的地址
```

The value of `p` is exactly the same as `&x`—it truly stores the address of `x`. And `p` has its own address too (`&p`), because the pointer itself is also a variable and occupies memory.

> **Pitfall Warning**: The result of `int* p1, p2;` is that `p1` is a `int*` while `p2` is a `int`—`*` only modifies the variable immediately following it. To declare two pointers, you must write `int *p1, *p2;`. The best practice is to declare only one pointer per line.

## Dereferencing — Following the Address to Find the Data

`*` means "this is a pointer" in a declaration, but in an expression it means "follow this address to get the data"—the meaning changes depending on the context. Through `*p`, you can read or even modify the variable the pointer points to:

```cpp
int x = 42;
int* p = &x;

std::cout << *p << std::endl;  // 42，读取
*p = 100;                       // 通过指针修改 x
std::cout << x << std::endl;   // 100
```

We did not write `x = 100` directly; instead, we indirectly modified `x` through a pointer. This is the core capability of pointers—**indirect access**. `&` (address-of) and `*` (dereference) are inverse operations: `*&x` is just `x`, and `&*p` is just `p`.

## Pointer Types — Why `int*` and `double*` Are Not the Same Thing

An address is indeed just a number, but the type information tells the compiler "what type of data lives at this address"—how many bytes to read and how to interpret the binary content.

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

Two conclusions: different pointer types yield different value types when dereferenced, because the compiler interprets the binary data according to the pointer type. But regardless of what type they point to, the pointers themselves are all 8 bytes on a 64-bit system—an address is just an address, a number used for recording.

> **Pitfall Warning**: `int* p = &d;` (assigning the address of a `double` to a `int*`) will cause a direct compilation error—the compiler is protecting you. If you use a C-style cast to bypass this—`int* p = (int*)&d;`—then `*p` will read out as a completely meaningless number.

## Null Pointers — Pointing to Nothing

Sometimes we need a pointer but do not know where it should point yet, or a function needs to return a "not found" signal when a lookup fails. This is where **null pointers** come in—pointers that explicitly indicate "pointing to nothing." In C++98 and C, we used NULL. Anyone who has looked at stdlib.h knows that this is just a cast of (void*)0. The `nullptr` introduced in C++11 is the only correct way to represent a null pointer in modern C++:

```cpp
int* p = nullptr;  // 不指向任何有效地址

if (p != nullptr) { // 也有朋友喜欢if(p)，这个是习惯，笔者只有在非常需要强调兄弟们这不是空指针的时候这样写。
    std::cout << *p << std::endl;
} else {
    std::cout << "p 是空指针，不能解引用" << std::endl;
}
```

> **Pitfall Warning**: Dereferencing a null pointer is **undefined behavior** (UB). The program might crash immediately (Segmentation Fault), output garbage, or appear to work "fine" while data has been silently corrupted. The syntax is perfectly legal, and the compiler will not stop you—so build the habit: **always check for null before dereferencing**.

In older code, you might see `NULL` or `0`, but `nullptr` has a key advantage: its type is `std::nullptr_t`, so it will not be confused with integers and will not cause incorrect matches in function overloading. Always use `nullptr`, and leave `NULL` to history.

## Pointers and const — A Quick Review

In earlier chapters, we learned about the three combinations of `const` and pointers. Here is a quick recap:

`const int* p`—pointer to const, you cannot modify data through `p`, but you can change where it points:

```cpp
int x = 10, y = 20;
const int* p = &x;
// *p = 100;  // 编译错误
p = &y;       // 没问题
```

`int* const p`—const pointer, you cannot change where it points, but you can modify the data:

```cpp
int x = 10;
int* const p = &x;
*p = 100;      // 没问题
// p = &y;     // 编译错误
```

`const int* const p`—double const, neither can be changed. Reading tip: read from right to left, `const int* const p` reads as "p is a const pointer, pointing to const int."

## Common Pitfalls

The power of pointers comes with danger. The following traps are almost guaranteed to catch beginners; recognizing them early will save you a lot of debugging time.

### Uninitialized Pointers

If you declare a pointer without assigning a value, it contains a garbage address—dereferencing it is undefined behavior, and it can be even worse than a null pointer (a null pointer will at least crash immediately, while a garbage address might point to a valid area and cause data to be silently tampered with). **Initialize pointers immediately upon declaration**—even if you do not know where to point yet, assign `nullptr` first.

### Returning the Address of a Local Variable

Local variables inside a function are allocated on the stack, and the stack space is reclaimed when the function returns. Returning a pointer to a local variable gives the caller a **dangling pointer**—the address is still there, but the data is no longer reliable:

```cpp
int* get_value()
{
    int local = 42;
    return &local;  // 悬空指针！
}
```

The compiler with `-Wall` will issue a `warning: address of local variable 'local' returned`; take it seriously.

### Double Free and Use After Free

These fall under the category of dynamic memory management, which we will cover in detail later. The core principle: memory allocated via `new` should be freed via `delete` exactly once. Freeing twice (double free) or continuing to use after freeing (use after free) are both serious undefined behavior.

> **Pitfall Warning**: The three pitfalls above share a common root cause—pointers give you the ability to directly manipulate memory, but the compiler cannot check whether your usage is correct in all scenarios. As a result, pointer-related issues often only surface at runtime, and the symptoms can be highly unstable (sometimes it runs perfectly fine, but crashes with a different compiler flag). Building good pointer habits is far more efficient than troubleshooting problems after they occur.

## Comprehensive Example — pointers.cpp

Now let us put everything together:

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

The addresses may differ each time you run it, but the value of `p` will always match `&x`, the values are swapped after the swap, and the null pointer is handled correctly. We recommend copying this to your local machine, compiling, and running it to observe the address changes yourself.

## Run Online

Run the comprehensive pointer basics example online to observe taking addresses, dereferencing, pointer swaps, and null pointer checks:

<OnlineCompilerDemo
  title="指针基础综合演练：取地址、解引用、swap、空指针"
  source-path="code/examples/vol1/10_pointer_basics.cpp"
  description="在线运行并观察指针的基本操作。试着修改指针指向的值，观察原变量的变化。"
  allow-run
/>

## Try It Yourself

### Exercise 1: Write a swap by Hand and Observe Addresses

Declare two `int` variables, `a` and `b`, print their values and addresses, swap the values through pointers, and print again. Did the values change? Did the addresses change? Why?

### Exercise 2: Trace Pointer Values

Without running it first, trace the result on paper, then compile to verify:

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

Many people trip up on the difference between `*p = *q` and `p = q` the first time they do this—the former assigns data, while the latter changes where the pointer points.

### Exercise 3: Fix the Null Pointer Bug

The following code has three pointer-related bugs. Find and fix them:

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

Starting from memory addresses, this chapter walked through the core concepts of pointers. `&` gets the address, a pointer is a variable that stores an address, and `*` dereferences a pointer to read or write data; a pointer's type determines how memory is interpreted when dereferenced, but the pointer itself is always 8 bytes on a 64-bit system; `nullptr` is the correct way to represent a null pointer in modern C++, and dereferencing a null pointer is undefined behavior; the three combinations of `const` and pointers control whether the data and the pointer itself are mutable; uninitialized pointers, dangling pointers, and double frees are the three most common traps.

In the next chapter, we will enter the world of pointer arithmetic and arrays—what adding 1 to a pointer actually means, and what the real relationship is between an array name and a pointer. This knowledge will elevate pointers from "variables that store addresses" to "tools for traversing memory."
