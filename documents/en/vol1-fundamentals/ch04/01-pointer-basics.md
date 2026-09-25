---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Understand pointers from scratch: address-of, dereferencing, pointer
  types, and null pointers, and master the core mechanism of C++ memory access.'
difficulty: beginner
order: 1
platform: host
prerequisites:
- inline and constexpr Functions
reading_time_minutes: 14
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Pointer Basics
translation:
  source: documents/vol1-fundamentals/ch04/01-pointer-basics.md
  source_hash: 2ad6b3ac70d1db4251e588b0764e44c992ff79c5efaef4dba57b6d42cab881d8
  translated_at: '2026-09-25T10:40:13+00:00'
  engine: anthropic
  token_count: 3000
---
# What Is a Pointer: A Variable Holding the Address of Another Variable

Pointers are probably the best-known feature in C++—and the one most likely to scare beginners off. If you have come from Python or Java, you are probably used to the mindset of "the variable is the object itself": the variable holds the data, and you just use it. C++ is different. It hands us the power to manipulate memory addresses directly, and pointers are the gateway to that power.

Plenty of people tense up the moment they hear the word "pointer". But the truth is that **a pointer is just a variable that stores a memory address—nothing more.** Every variable occupies a spot in memory, that spot has a number (its address), and pointers are what record and manipulate those numbers. Once we have nailed the basics—address-of, dereferencing, pointer types, and null pointers—pointer arithmetic, arrays, and dynamic memory management will have a foundation to stand on.

## First, Get "Addresses" Straight: Where Every Variable Lives in Memory

We can picture memory as a row of storage cells numbered byte by byte; each variable occupies a few consecutive cells among them, and a variable's address is the starting number of that stretch of space. The `&` (address-of) operator gets us a variable's address:

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

The output looks roughly like:

```text
x 的值:   42
x 的地址: 0x7ffd4a3b2c5c
```

Here is how to read that output. What is x? 42! And where is x? At byte 0x7ffd4a3b2c5c!

That hexadecimal number starting with `0x` is x's address in memory. The address may differ from run to run, but one thing is certain: **every variable has a unique address, and `&` is the operator that fetches it**. Declare a few more variables and print their addresses, and we will find that adjacent `int`s sit 4 apart—exactly the number of bytes one `int` occupies.

## Pointer Variables—What We Usually Call "Pointers"

Since an address is just a number, we can naturally store it in a variable. That is the **pointer**: a variable that stores a memory address.

```cpp
int x = 42;
int* p = &x;   // p stores x's address
```

The `*` in a declaration says "this is a pointer", and `int*` reads as "pointer to int". `p` itself is a variable occupying memory of its own; the value it stores is the address `&x`, and following that address, what we fetch is x's value, 42.

Let's verify the relationship between the pointer and the original variable:

```cpp
int x = 42;
int* p = &x;

std::cout << "x 的值:   " << x << std::endl;   // 42
std::cout << "&x 的值:  " << &x << std::endl;   // 0x7ffd...
std::cout << "p 的值:   " << p << std::endl;     // same as &x
std::cout << "&p 的值:  " << &p << std::endl;    // a different address
```

We see that p's value is exactly `&x`—it really does store x's address. And `p` has an address of its own (`&p`), because a pointer is itself a variable and needs its own memory.

With `int* p1, p2;` the outcome is that `p1` is an `int*` while `p2` is an `int`—the `*` only modifies the variable immediately after it. To declare two pointers, we must write `int *p1, *p2;`. The best practice is to declare only one pointer per line.

## We Know the Address, Now Grab the Goods—Dereferencing

In a declaration, `*` means "this is a pointer"; in an expression, it means "follow this address and fetch the data"—same symbol, different meaning depending on context. Through `*p` we can read, and even modify, the variable the pointer points to:

```cpp
int x = 42;
int* p = &x;

std::cout << *p << std::endl;  // 42, read
*p = 100;                       // modify x through the pointer
std::cout << x << std::endl;   // 100
```

We never wrote `x = 100` ourselves; instead, we modified `x` indirectly through the pointer. This is the pointer's core capability: **indirect access**. `&` (address-of) and `*` (dereference) are a pair of mutually inverse operations: `*&x` is `x`, and `&*p` is `p`.

## Pointer Types—Why `int*` and `double*` Are Not the Same Thing

An address really is just a number, but the type information tells the compiler "what type of data is stored at this address": how many bytes a read touches, and how the binary content should be interpreted—which is exactly what we are about to look at.

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

Two conclusions come out of this: dereferencing pointers of different types yields values of different types, because the compiler interprets the binary data according to the pointer's type; yet no matter what they point to, the pointers themselves are all 8 bytes on a 64-bit system—an address is an address, just a recorded number.

`int* p = &d;` (assigning a `double`'s address to an `int*`) fails to compile outright, and that is the compiler protecting us. If we sneak past it with a C-style cast and write `int* p = (int*)&d;`, then `*p` reads out as a blob of completely meaningless numbers.

## Null Pointers: Pointing at Nothing

Sometimes we need a pointer but don't yet know where it should point, or a function's lookup has failed and we need to return a "not found" signal. That calls for the **null pointer**: a pointer that explicitly states "I point at nothing". In C++98 and C we used `NULL`—friends who have flipped through `stdlib.h` know that in C it is `(void*)0`, while in C++98 it is the integer `0`, and both forms left the door open to ambiguity. `nullptr`, introduced in C++11, is the one correct way to express a null pointer in modern C++:

```cpp
int* p = nullptr;  // points to no valid address

if (p != nullptr) { // Some folks prefer if(p); that's a habit—the author only writes it this way when he really needs to stress that this is not a null pointer.
    std::cout << *p << std::endl;
} else {
    std::cout << "p 是空指针，不能解引用" << std::endl;
}
```

Dereferencing a null pointer is **undefined behavior** (Undefined Behavior). The program may crash outright (a segmentation fault), may print garbage, or may look "normal" while the data has already been corrupted. The syntax is perfectly legal and the compiler will not stop us, so make it a habit: **check for null before dereferencing**.

In old code you may see `NULL` or `0`, but `nullptr` has one key advantage: its type is `std::nullptr_t`, so it never gets confused with an integer and never causes a wrong match during function overloading. We use `nullptr` across the board and leave `NULL` to history.

## Pointers and const—A Refresher

In earlier chapters we covered the three combinations of `const` and pointers; here is a quick review:

`const int* p`: a pointer to a constant—we cannot modify the data through `p`, but we can change where it points.

```cpp
int x = 10, y = 20;
const int* p = &x;
// *p = 100;  // compile error
p = &y;       // fine
```

`int* const p`: a constant pointer—we cannot change where it points, but we can modify the data.

```cpp
int x = 10;
int* const p = &x;
*p = 100;      // fine
// p = &y;     // compile error
```

`const int* const p`: double const—neither can change. When reading it, go from right to left: `const int* const p` reads as "p is a const pointer, pointing to a const int".

## Common Pitfalls

The power of pointers comes with danger. The traps below are ones nearly every beginner steps in; getting acquainted with them ahead of time saves a lot of debugging. For the specifics, our [Crash Lab](/crash-lab/) column will take everyone there for some proper hands-on fun (side-eye grin).

| Pitfall | How it happens | How bad it is | How to prevent it |
| --- | --- | --- | --- |
| **Uninitialized pointers** | Declare a pointer without assigning it; what's inside is a garbage address | Dereferencing it is undefined behavior, and it can be even worse than a null pointer: a null pointer at least crashes immediately, while a garbage address may happen to land in a valid region and data gets silently corrupted | Initialize a pointer the moment you declare it; even if we don't know the target yet, assign `nullptr` first |
| **Returning a local variable's address** | Local variables are allocated on the stack; once the function returns, that stack space is reclaimed | The caller receives a **dangling pointer**: the address is still there, but the data behind it is no longer reliable | Don't return a local variable's address; with `-Wall` the compiler emits `warning: address of local variable 'local' returned`—treat it seriously |
| **Double free and use after free** | Memory allocated with `new` is not `delete`d exactly once | Freeing twice (double free) or continuing to use memory after freeing it (use after free) are both severe undefined behavior | Remember the "exactly once" pairing rule; the details belong to dynamic memory management, which we will cover in depth later |

These three pitfalls share one root: pointers give us the power to manipulate memory directly, but the compiler cannot verify in every scenario that we are using that power correctly. So pointer-related problems tend to surface only at runtime, and the symptoms can be unstable (the program runs fine, then crashes the moment a compiler flag changes). Building good pointer habits up front is far more efficient than hunting problems down after they happen.

## Hands-On Practice — pointers.cpp

Let's string everything together:

```cpp
// pointers.cpp — a comprehensive demo of basic pointer operations
#include <iostream>

/// @brief Swap the values of two variables through pointers
void swap_by_pointer(int* a, int* b)
{
    if (a == nullptr || b == nullptr) {
        return;
    }
    int temp = *a;
    *a = *b;
    *b = temp;
}

/// @brief Safely print the value a pointer points to
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
    // address-of and dereference
    int x = 42;
    int* p = &x;
    std::cout << "=== 取地址与解引用 ===" << std::endl;
    std::cout << "x = " << x << ", &x = " << &x << std::endl;
    std::cout << "p = " << p << ", *p = " << *p << std::endl;

    // modify through the pointer
    *p = 100;
    std::cout << "\n=== *p = 100 后 ===" << std::endl;
    std::cout << "x = " << x << std::endl;

    // pointer swap
    int a = 10, b = 20;
    std::cout << "\n=== swap ===" << std::endl;
    std::cout << "交换前: a=" << a << ", b=" << b << std::endl;
    swap_by_pointer(&a, &b);
    std::cout << "交换后: a=" << a << ", b=" << b << std::endl;

    // null pointer check
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

Addresses may differ from run to run, but p's value always matches `&x`, the two values come out swapped after swap, and the null pointer is handled correctly. We suggest you copy this to your machine, compile, and run it yourself, and watch how the addresses change.

## Run It Online

You can also run the comprehensive pointer-basics example online and observe address-of, dereferencing, pointer swap, and null pointer checking:

<OnlineCompilerDemo
  title="Pointer Basics Comprehensive Drill: Address-of, Dereference, Swap, Null Pointers"
  source-path="code/examples/vol1/10_pointer_basics.cpp"
  description="Run it online and watch the basic pointer operations. Try changing the value a pointer points to and watch the original variable change."
  allow-run
/>

## Try It Yourself

### Exercise 1: Hand-Written swap, Watching the Addresses

Declare two `int` variables `a` and `b`, print their values and addresses, swap the values through pointers, then print again. The values changed—did the addresses change? Why?

::: details Reference Solution

```cpp
#include <iostream>

void swap(int* p1, int* p2)
{
    int temp = *p1;
    *p1 = *p2;
    *p2 = temp;
}

int main()
{
    int a = 10;
    int b = 20;
    int* p1 = &a;
    int* p2 = &b;

    std::cout << "========== 交换前 ==========" << std::endl;
    std::cout << "变量a的值: " << a << std::endl;
    std::cout << "变量a的地址: " << p1 << std::endl;
    std::cout << "变量b的值: " << b << std::endl;
    std::cout << "变量b的地址: " << p2 << std::endl;

    swap(p1, p2);

    std::cout << "\n========== 交换后 ==========" << std::endl;
    std::cout << "变量a的值: " << a << std::endl;
    std::cout << "变量a的地址: " << p1 << std::endl;
    std::cout << "变量b的值: " << b << std::endl;
    std::cout << "变量b的地址: " << p2 << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Result:

```text
========== 交换前 ==========
变量a的值: 10
变量a的地址: 0x7ffe15dc91b0
变量b的值: 20
变量b的地址: 0x7ffe15dc91b4

========== 交换后 ==========
变量a的值: 20
变量a的地址: 0x7ffe15dc91b0
变量b的值: 10
变量b的地址: 0x7ffe15dc91b4
```

**Key observation: the values changed, the addresses did not**

Comparing the output carefully, we find that the value of `a` went from 10 to 20 while its address stayed `0x7ffe15dc91b0` throughout; the value of `b` went from 20 to 10 while its address stayed `0x7ffe15dc91b4`.

**Why don't the addresses change?**

Look at `int a = 10;`: when space is allocated for it on the stack, the operating system and the compiler work together to give `a` a 4-byte block of memory (assuming `int` is 4 bytes), and the starting position of that block is a's address. That address will not change for the entire lifetime of the variable.

Now look at `swap`: what it exchanges through the pointers are the **contents** of the variables (their values); the variables' positions in memory never move—the memory at `0x7ffe15dc91b0` belongs to `a` for a's entire lifetime, and only the value stored inside changed from 10 to 20.

This is the pointer's power on display: through an address, we can reach across function boundaries to modify the contents of the original variable, without copying the whole variable or changing its memory layout.

:::

### Exercise 2: Tracing Pointer Values

Before running anything, trace the result on paper first, then compile to verify:

```cpp
#include <iostream>

int main()
{
    int x = 10, y = 20;
    int* p = &x;
    int* q = &y;
    *p = *q;   // assign the value q points to, into the spot p points to
    p = q;     // make p and q point to the same place
    *p = 30;
    std::cout << "x = " << x << std::endl;
    std::cout << "y = " << y << std::endl;
    std::cout << "*p = " << *p << std::endl;
    std::cout << "*q = " << *q << std::endl;
    return 0;
}
```

Many people stumble on their first try over the difference between `*p = *q` and `p = q`—the former assigns data, the latter changes where the pointer points. This spot deserves an extra minute of our time.

::: details Reference Solution

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Result:

```text
x = 20
y = 30
*p = 30
*q = 30
```

:::

### Exercise 3: Fix the Null Pointer Bugs

The code below has two pointer-related bugs. Please find and fix them:

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

::: details Reference Solution

```cpp
#include <iostream>

int *create_value()
{
    static int val = 42;
    return &val;
}

int main()
{
    int *p = nullptr; // bug 1
    int value = 10;
    p = &value;
    std::cout << "*p的值是: " << *p << std::endl;

    int *q = create_value(); // bug 2
    std::cout << "*q的值是: " << *q << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Result:

```text
*p的值是: 10
*q的值是: 42
```

:::
