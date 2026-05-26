---
title: Introduction to Value Categories
description: Understand the concepts of lvalues and rvalues, master the basic usage
  of references, and lay the foundation for move semantics.
chapter: 1
order: 4
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
- const 初探
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
translation:
  source: documents/vol1-fundamentals/ch01/04-value-categories.md
  source_hash: 242fc75da53284aed6d62dac3c6f661785313f67f172625626f81e961bd7669d
  translated_at: '2026-05-26T10:44:15.849333+00:00'
  engine: anthropic
  token_count: 2073
---
# Introduction to Value Categories

By this chapter, we have worked extensively with variables, types, and `const`. But have you ever wondered: why can some expressions appear on the left side of an assignment, while others can only appear on the right? Why does `int& ref = x;` compile, but `int& ref = 42;` does not? Behind these seemingly scattered phenomena lies a unifying thread—**value categories**.

Value categories might sound like an academic term, but they directly determine how the compiler handles every expression you write: which operations are legal, which are not, what references can bind to, and how function return values are passed. We could say that without understanding value categories, when you later learn about references, move semantics, and perfect forwarding, you will remain in a state of "knowing how to write it but not knowing why." So let's get this straight in this chapter. We won't go extremely deep (value category taxonomy after C++11 is actually quite complex), but we will at least nail down the core concepts of lvalues and rvalues, and build a solid foundation for using references.

## What Is an Lvalue — A Named Storage Location

The term lvalue comes from the historical definition of "a value that can appear on the left side of an assignment in C." While not entirely accurate, it does provide a good intuition. In more modern terms, an lvalue is an expression that **has a name and a definite memory address**—you can take its address (using the `&` operator), and its lifetime does not immediately end when the current expression finishes evaluating.

You can think of an lvalue as a storage box with a label: the box has its own location (memory address), the label lets you find it at any time (the variable name), and you can put things into it or take things out of it.

The most typical lvalues are plain variables. In `int x = 10;`, `x` is an lvalue—it has the name `x`, a memory address `&x`, and it persists until the end of its scope. Similarly, a dereferenced pointer is also an lvalue; `*ptr` means "the memory that ptr points to." That memory has an address and a name (accessed via `*ptr`), so it is an lvalue. Array elements are the same; `arr[3]` refers to the memory at the fourth position in the array, so it is naturally an lvalue.

Let's look at a few concrete examples:

```cpp
int x = 10;       // x 是左值
int* ptr = &x;    // ptr 是左值，&x 取出了 x 的地址
*ptr = 20;        // *ptr 是左值——它代表 x 那块内存
int arr[5] = {};
arr[2] = 42;      // arr[2] 是左值——它代表数组第三个位置的内存
```

These expressions share a common trait: you can take their address. `&x`, `&(*ptr)`, and `&(arr[2])` are all legal operations. This is actually the most practical way to judge whether an expression is an lvalue—if you can take its address and it has a name, it is almost certainly an lvalue.

> ⚠️ **Watch out**: Don't equate "lvalue" with "can appear on the left side of an assignment." In `const int cx = 10;`, `cx` is an lvalue, but `cx = 20;` will not compile—a const lvalue cannot be assigned to. An lvalue describes having "identity" (a memory address), not "being modifiable."

## What Is an Rvalue — A Fleeting Temporary

An rvalue is the opposite of an lvalue: it is an expression **without persistent identity**, usually a temporarily produced value. You cannot take its address, and it may disappear once the expression has been evaluated.

You can think of an rvalue as a package delivered by a courier—the package arrives in your hands (the expression's value has been computed), and you can open it to look, but you can't stuff things back into the sender's package (you can't take its address or assign to it) because that package is merely a temporary medium of delivery.

The most typical rvalues are literals. `42` is an rvalue—it is the integer 42, but "42" has no memory address (at least not at your code level), and you cannot write `&42`. The result of the expression `x + y` is also an rvalue. When the compiler computes `x + y`, it places the result in a temporary location. This temporary location has no name, and you cannot reference it through a variable name.

```cpp
42                  // 右值——字面量
3.14                // 右值——浮点字面量
x + y               // 右值——算术表达式的临时结果
static_cast<int>(3.14)  // 右值——类型转换产生的临时值
```

You cannot take their address: `&42` will not compile, and neither will `&(x + y)`. The compiler will tell you directly—these are temporary values with no address to take.

> ⚠️ **Watch out**: Function return values require special attention. If a function returns by value (not by reference), such as `int get_value() { return 42; }`, then the result of calling `get_value()` is an rvalue—it is a temporary value copied out from inside the function, with no persistent identity. But if you write `int& get_ref() { return x; }`, then `get_ref()` returns a reference, and its result is an lvalue—because it ultimately binds to a variable with identity.

## Why Distinguish Them — Rules for Reference Binding

Simply knowing "what is an lvalue and what is an rvalue" is not enough; the key is understanding how this distinction affects the code you actually write. The most direct impact is on **reference binding**.

C++ has several kinds of references. Let's start with the most basic one: the lvalue reference. An lvalue reference is denoted by `T&`, and it must bind to an lvalue—this makes sense, because the essence of a reference is an "alias," and you need a real, persistent variable before you can give it an alias.

```cpp
int x = 10;
int& ref = x;     // 没问题：ref 是 x 的别名
ref = 20;          // 现在 x 也变成了 20
```

But if you try to make an lvalue reference bind to an rvalue:

```cpp
int& ref = 42;    // 编译错误！
```

The compiler will flat-out reject it, with an error message that looks roughly like this:

```text
error: cannot bind non-const lvalue reference of type 'int&' to an rvalue of type 'int'
```

The reason is intuitive: `42` is a temporary value, and its lifetime might end as soon as this line of code finishes. If you use a reference to point to it, by the time this line executes, the thing the reference points to might no longer exist—this is a "dangling reference," a classic safety hazard. The compiler stops you here to help you avoid problems.

There is one exception, though—a const lvalue reference can bind to an rvalue:

```cpp
const int& ref = 42;   // 合法！
```

This seems a bit counterintuitive, but the C++ standard makes a special provision here: when a const lvalue reference binds to an rvalue, the compiler automatically extends the lifetime of that temporary value so that it lives until the end of the reference's scope. This is actually a very practical feature—later on, you will frequently see the pattern `const std::string&` in function parameters. It can accept both lvalue and rvalue arguments precisely because of this rule.

## Reference Basics — Not a Pointer, but Better Than a Pointer

Since we brought up references, let's clarify their basic usage. The concept of a reference is simple—it is an **alias** for an already existing variable. From the moment you create it, it is bound to the referenced variable, and any operation on the reference is equivalent to an operation on the original variable.

```cpp
int x = 10;
int& ref = x;    // ref 是 x 的别名
ref = 20;        // x 现在是 20
std::cout << x;  // 输出 20
```

References have a few important properties that you need to get right from the start. First, a reference **must be initialized at creation**—you cannot declare a reference first and then make it point to some variable later. `int& ref;` will simply not compile; the compiler will tell you that the reference requires initialization. Second, once a reference is bound, it cannot be changed—there is no operation to "make a reference point to another variable." If you write `ref = y;`, you are not rebinding ref to y; you are assigning the value of y to the variable that ref references. This is completely different from pointer behavior, as a pointer can point to different addresses at any time.

The most common use of references is as function parameters. If we pass by value, the function gets a copy of the argument, and modifying the copy does not affect the original data. If we pass by reference, the function operates directly on the original data. For large objects (such as a very long string or a container with many elements), passing by value means an expensive copy operation, while passing by reference has no extra overhead.

```cpp
/// @brief 值传递——函数内部修改不影响外部
void add_one_by_value(int n)
{
    n = n + 1;    // 只修改了局部的拷贝
}

/// @brief 引用传递——函数内部直接修改外部变量
void add_one_by_ref(int& n)
{
    n = n + 1;    // 修改了原始变量
}

int main()
{
    int a = 10;
    add_one_by_value(a);
    std::cout << a << "\n";   // 输出 10，没变

    add_one_by_ref(a);
    std::cout << a << "\n";   // 输出 11，变了

    return 0;
}
```

> ⚠️ **Watch out**: This is one of the most common mistakes beginners make—returning a reference to a local variable from a function. The local variable is destroyed when the function returns, the thing the reference points to no longer exists, and accessing it is **undefined behavior**. You might get garbage data, it might crash, or it might coincidentally appear to work—but it is wrong regardless.

```cpp
int& bad_function()
{
    int local = 42;
    return local;    // 严重错误！local 在函数返回后销毁
}                    // 返回的引用指向已销毁的变量
```

The compiler will usually issue a warning but won't stop you from compiling. Remember a simple rule: **never return a reference or pointer to a local variable**.

## Rvalue References — A Quick Introduction (C++11)

Before C++11, C++ had only one kind of reference—namely, the lvalue reference we just discussed. C++11 introduced the **rvalue reference**, denoted by `T&&`, which can only bind to rvalues.

```cpp
int x = 10;
int& lref = x;        // 左值引用，绑定到左值 x
int&& rref = 42;      // 右值引用，绑定到右值 42
int&& rref2 = x + 1;  // 右值引用，绑定到临时表达式结果

// int&& rref3 = x;   // 编译错误！右值引用不能绑定到左值
```

You might ask: what is the point of rvalue references? Why create a special kind of reference that can only bind to temporary values? The answer is **move semantics**—it allows us to "steal" the resources inside a temporary value instead of making an expensive copy. For example, with a container holding one million elements, when you no longer need the original, move semantics lets you directly take over the internal pointer at almost zero cost.

We won't go into detail here. Just remember the syntax `T&&` and know that it is a reference designed for rvalues. Move semantics is a major topic in Volume Two, where we will dive deep into it.

## Hands-on Experiment — values.cpp

After all this theory, let's write a complete program to verify these rules. This program will demonstrate whether various expressions are lvalues or rvalues, along with different reference binding scenarios.

```cpp
// values.cpp -- 值类别与引用绑定演示
// Standard: C++11

#include <iostream>

/// @brief 返回一个整数值（右值）
int get_value()
{
    return 42;
}

/// @brief 返回一个整数的引用（左值）
int global = 100;
int& get_ref()
{
    return global;
}

int main()
{
    // ---- 左值 ----
    int x = 10;            // x 是左值
    int* ptr = &x;         // &x 合法：x 是左值，可以取地址
    *ptr = 20;             // *ptr 是左值
    int arr[3] = {1, 2, 3};
    arr[0] = 99;           // arr[0] 是左值

    std::cout << "x = " << x << "\n";            // 20
    std::cout << "arr[0] = " << arr[0] << "\n";  // 99

    // ---- 右值 ----
    // &42;                  // 错误：不能对右值取地址
    // &(x + 1);             // 错误：x + 1 的结果是右值
    // &get_value();          // 错误：函数返回值是右值

    int sum = x + arr[1];   // x + arr[1] 的结果是右值
    std::cout << "sum = " << sum << "\n";        // 22

    // ---- 左值引用 ----
    int& lref = x;          // OK：左值引用绑定到左值
    lref = 30;
    std::cout << "x = " << x << "\n";            // 30

    // int& bad = 42;        // 错误：左值引用不能绑定到右值

    const int& cref = 42;   // OK：const 引用可以绑定到右值
    std::cout << "cref = " << cref << "\n";      // 42

    // ---- 右值引用（C++11）----
    int&& rref = 42;        // OK：右值引用绑定到右值
    int&& rref2 = x + 1;   // OK：x + 1 是右值
    // int&& rref3 = x;     // 错误：右值引用不能绑定到左值

    std::cout << "rref = " << rref << "\n";      // 42
    std::cout << "rref2 = " << rref2 << "\n";    // 31

    // ---- 函数返回值的值类别 ----
    // get_value() 返回右值
    int val = get_value();
    std::cout << "get_value() = " << val << "\n";   // 42

    // get_ref() 返回左值
    get_ref() = 200;       // OK：get_ref() 返回左值引用，可以赋值
    std::cout << "global = " << global << "\n";      // 200

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++11 -Wall -Wextra -o values values.cpp
./values
```

```text
x = 20
arr[0] = 99
sum = 22
x = 30
cref = 42
rref = 42
rref2 = 31
get_value() = 42
global = 200
```

Let's walk through a few key points of this program. The commented-out lines are the ones that would cause compilation errors—you can try uncommenting them to see what errors the compiler reports. This is the fastest way to understand value categories. `get_value()` returns `int`, so calling it yields an rvalue, meaning `&get_value()` is illegal. `get_ref()` returns `int&`, so calling it yields an lvalue reference, and you can directly assign to it—`get_ref() = 200;` looks a bit odd, but it is indeed assigning to `global`.

`const int& cref = 42;` is a very important usage pattern. A const lvalue reference can bind to an rvalue, and the compiler automatically extends the lifetime of the temporary value `42`. This technique is extremely common in function parameters—when we don't want to copy a large object and don't need to modify it, using `const T&` as the parameter type is the best choice.

## Try It Yourself

At this point, we have gone through the concepts of lvalues, rvalues, lvalue references, const references, and rvalue references, as well as the relationships between them. Now let's test what you have learned.

### Exercise 1: Classify These Expressions

Determine whether each of the following expressions is an lvalue or an rvalue, and explain why:

- `x` (assuming `int x = 5;`)
- `x + 3`
- `"hello"`
- `*ptr` (assuming `int* ptr = &x;`)
- `x++` (postfix increment)
- `++x` (prefix increment)

If you are unsure, you can write a small program and try taking their address—if you can take its address, it is very likely an lvalue. The difference between `x++` and `++x` is a classic trap, and it is worth thinking about carefully.

### Exercise 2: Predict Reference Binding

Which of the following code snippets will compile? Which will report errors? Judge in your head first, then verify by actually compiling.

```cpp
int a = 10;
int& r1 = a;
int& r2 = 10;
const int& r3 = 10;
int&& r4 = 10;
int&& r5 = a;
const int& r6 = a;
```

### Exercise 3: Fix the Dangling Reference

The following code has a serious bug—the function returns a reference to a local variable. Find it and fix it:

```cpp
int& get_max(int a, int b)
{
    int result = (a > b) ? a : b;
    return result;
}

int main()
{
    int& m = get_max(3, 7);
    std::cout << m << "\n";
    return 0;
}
```

Hint: Think about whether this function should return by value or by reference. Does the local variable `result` still exist after the function returns?

## Summary

In this chapter, we spent considerable time understanding value categories—lvalues, rvalues, and their relationship with references. Lvalues are expressions with names, addresses, and longer lifetimes, while rvalues are temporary expressions without persistent identity. An lvalue reference `T&` can only bind to lvalues, a const lvalue reference `const T&` can bind to everything, and an rvalue reference `T&&` (C++11) can only bind to rvalues. References must be initialized, cannot be rebound, and the most common pitfall is returning a reference to a local variable.

This knowledge might seem theoretical, but it is the foundation for understanding subsequent content. When we get to move semantics (in Volume Two), you will find that today's concepts become the key factors determining program performance. But there is no need to rush—let's build a solid foundation first.

In the next chapter, we move on to control flow—learning to make decisions with `if`/`else`, use loops for repetition, and make our programs truly "think."
