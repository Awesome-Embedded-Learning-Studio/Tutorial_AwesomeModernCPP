# Introduction to Value Categories

By this chapter, we have worked extensively with variables, types, and `const`. But have you ever wondered: why can some expressions appear on the left side of an assignment, while others can only appear on the right? Why does `x = 42` compile, but `42 = x` does not? These seemingly scattered phenomena actually share a unifying thread—**value categories**.

Value categories might sound like an academic term, but they directly dictate how the compiler handles every expression you write, which operations are legal, what references can bind to, how function return values are passed, and more. Without understanding value categories, you will likely feel like you "know how to write it but not why" when you later learn about references, move semantics, and perfect forwarding. So let's clear this up now. We won't dive excessively deep (value category taxonomy got fairly complex after C++11), but we will solidify the core concepts of lvalues and rvalues, and lay a firm foundation for using references.

## What Is an Lvalue — A Named Storage Location

The term lvalue originates from the historical definition of "a value that can appear on the left side of an assignment in C." While not entirely accurate, it does provide a decent intuition. In more modern terms, an lvalue is an expression that **has a name and a definite memory address**—you can take its address (using the `&` operator), and its lifetime does not immediately end when the current expression finishes evaluating.

You can think of an lvalue as a labeled storage bin: the bin has its own location (memory address), the label lets you find it at any time (variable name), and you can put things into it or take things out of it.

The most typical lvalues are plain variables. In `int x = 10;`, `x` is an lvalue—it has the name `x`, a memory address `&x`, and it persists until the end of its scope. Similarly, a dereferenced pointer is also an lvalue; `*ptr` represents "the memory that ptr points to." That memory has an address and a name (accessed via `*ptr`), so it is an lvalue. Array elements work the same way; `arr[3]` refers to the memory at the fourth position in the array, so it is naturally an lvalue.

Let's look at a few concrete examples:

```cpp
int x = 10;        // x 是左值
int* ptr = &x;     // ptr 是左值
*ptr = 20;         // *ptr 是左值
int arr[5];        // arr 是左值
arr[3] = 42;       // arr[3] 是左值
```

These expressions all share a common trait: you can take their address. `&x`, `&ptr`, and `&arr[3]` are all valid operations. This is actually the most practical way to judge whether an expression is an lvalue—if you can take its address and it has a name, it is almost certainly an lvalue.

> ⚠️ **Watch out**: Don't equate "lvalue" with "can appear on the left side of an assignment." In `const int x = 10;`, `x` is an lvalue, but `x = 20` fails to compile—a const lvalue cannot be assigned to. An lvalue describes having "identity" (a memory address), not "being modifiable."

## What Is an Rvalue — A Fleeting Temporary

An rvalue is the exact opposite of an lvalue: it is an expression **without persistent identity**, usually a temporarily produced value. You cannot take its address, and it may disappear as soon as the expression has been evaluated.

You can think of an rvalue as a delivery parcel—once the parcel arrives in your hands (the expression's value has been computed), you can open it and look inside, but you can't stuff things back into the sender's parcel (you can't take its address or assign to it), because that parcel is merely a temporary medium of delivery.

The most typical rvalues are literals. `42` is an rvalue—it is the integer 42, but "42" has no memory address (at least not at your code level), and you cannot write `&42`. The result of the expression `x + 1` is also an rvalue; when the compiler evaluates `x + 1`, it places the result in a temporary location. This temporary location has no name, and you cannot reference it through a variable name.

```cpp
42           // 字面量，右值
x + 1        // 表达式结果，右值
3.14         // 浮点字面量，右值
"hello"      // 字符串字面量比较特殊，是左值（有静态存储期）
```

You cannot take their address: `&42` fails to compile, and `&(x + 1)` also fails to compile. The compiler will tell you directly—these are temporary values with no address to take.

> ⚠️ **Watch out**: Pay special attention to function return values. If a function returns by value (not by reference), such as `int foo()`, then calling `foo()` yields an rvalue—it is a temporary value copied out from inside the function, with no persistent identity. But if you write `int& bar()`, then `bar()` returns a reference, and its result is an lvalue—because it ultimately binds to a variable with identity.

## Why Distinguish Them — Rules for Reference Binding

Simply knowing "what is an lvalue and what is an rvalue" is not enough; the key is understanding how this distinction affects the code you actually write. The most direct impact is on **reference binding**.

C++ has several kinds of references, and we will start with the most basic one: the lvalue reference. An lvalue reference is denoted by `T&`, and it must bind to an lvalue—this makes sense, because the essence of a reference is an "alias," and you need a real, persistent variable before you can give it an alias.

```cpp
int x = 10;
int& ref = x;    // OK: ref 绑定到左值 x
ref = 20;        // 修改的是 x
```

But if you try to make an lvalue reference bind to an rvalue:

```cpp
int& ref = 42;   // 错误：不能将右值绑定到非 const 左值引用
```

The compiler will flatly reject it, with an error message roughly like this:

```text
error: cannot bind non-const lvalue reference of type 'int&' to an rvalue of type 'int'
```

The reason is intuitive: `42` is a temporary value, and its lifetime might end as soon as this line of code finishes. If you use a reference to point to it, by the time this line executes, the thing the reference points to might no longer exist—that is a "dangling reference," a classic safety hazard. The compiler stops you here to help you avoid problems.

There is one exception, though—a const lvalue reference can bind to an rvalue:

```cpp
const int& ref = 42;   // OK: const 左值引用可以绑定右值
```

This might seem counterintuitive, but the C++ standard makes a special provision here: when a const lvalue reference binds to an rvalue, the compiler automatically extends the lifetime of that temporary value so that it lives until the reference goes out of scope. This is actually a very practical feature—later on, you will frequently see `const T&` used as a function parameter. It can accept both lvalue and rvalue arguments precisely because of this rule.

## Reference Basics — Not a Pointer, but Better Than a Pointer

Since we brought up references, let's clarify their basic usage. The concept of a reference is simple—it is an **alias** for an already existing variable. From the moment you create it, it is bound to the referenced variable, and any operation on the reference is equivalent to an operation on the original variable.

```cpp
int x = 10;
int& ref = x;     // ref 是 x 的别名
ref = 20;         // 等同于 x = 20
std::cout << x;   // 输出 20
```

References have a few important properties that you need to get right from the start. First, a reference **must be initialized at creation**—you cannot declare a reference first and then make it point to a variable later. `int& ref;` directly fails to compile, and the compiler will tell you that the reference requires initialization. Second, once a reference is bound, it cannot be changed—there is no operation to "make a reference point to another variable." If you write `ref = y;`, this does not rebind `ref` to `y`; instead, it assigns the value of `y` to the variable that `ref` references. This behavior is completely different from a pointer, which can point to different addresses at any time.

The most common use of references is as function parameters. If we pass by value, the function gets a copy of the argument, and modifying the copy does not affect the original data. If we pass by reference, the function directly operates on the original data. For large objects (such as a very long string or a container with many elements), passing by value means an expensive copy operation, while passing by reference incurs no extra overhead.

```cpp
void increment(int& n) {
    n++;  // 直接修改原始变量
}

int main() {
    int x = 10;
    increment(x);
    std::cout << x;  // 输出 11
}
```

> ⚠️ **Watch out**: This is one of the most common mistakes beginners make—returning a reference to a local variable. The local variable is destroyed when the function returns, and the thing the reference points to no longer exists. Accessing it is **undefined behavior (UB)**—you might get garbage data, it might crash, or it might coincidentally appear to work correctly—but it is wrong regardless.

```cpp
int& bad_function() {
    int local = 42;
    return local;  // 严重错误：返回局部变量的引用
}

int main() {
    int& ref = bad_function();  // ref 指向已销毁的内存
    std::cout << ref;           // 未定义行为！
}
```

The compiler will usually issue a warning but won't stop you from compiling. Remember a simple rule: **never return a reference or pointer to a local variable**.

## Rvalue References — A Quick Introduction (C++11)

Before C++11, C++ had only one kind of reference—the lvalue reference we just discussed. C++11 introduced the **rvalue reference**, denoted by `T&&`, which can only bind to rvalues.

```cpp
int&& rref = 42;      // OK: 右值引用绑定到右值
int x = 10;
// int&& rref2 = x;   // 错误：右值引用不能绑定到左值
```

You might ask: what is the point of rvalue references? Why create a special kind of reference that can only bind to temporary values? The answer is **move semantics**—it allows us to "steal" the resources inside a temporary value instead of making an expensive copy. For example, with a container holding a million elements, when you no longer need the original, move semantics lets you simply take over its internal pointers at almost zero cost.

We won't expand on this here. Just remember the `T&&` syntax and know that it is a reference designed for rvalues. Move semantics is a major topic in Volume Two, where we will cover it in depth.

## Hands-on Experiment — values.cpp

After all this theory, let's write a complete program to verify these rules. This program will demonstrate whether various expressions are lvalues or rvalues, and the different scenarios of reference binding.

```cpp
#include <iostream>

int global_var = 100;

int return_by_value() {
    return 42;
}

int& return_by_ref() {
    return global_var;
}

int main() {
    // === 左值示例 ===
    int x = 10;
    int arr[5] = {1, 2, 3, 4, 5};
    int* ptr = &x;

    std::cout << "=== 左值 ===" << std::endl;
    std::cout << "&x = " << &x << std::endl;           // OK
    std::cout << "&arr[2] = " << &arr[2] << std::endl; // OK
    std::cout << "&(*ptr) = " << &(*ptr) << std::endl; // OK

    // === 右值示例 ===
    std::cout << "\n=== 右值 ===" << std::endl;
    // std::cout << &42 << std::endl;           // 错误
    // std::cout << &(x + 1) << std::endl;      // 错误
    // std::cout << &return_by_value() << std::endl; // 错误

    // === 左值引用绑定 ===
    std::cout << "\n=== 左值引用 ===" << std::endl;
    int& lref = x;              // OK: 左值引用绑定左值
    lref = 20;
    std::cout << "x = " << x << std::endl;  // 20

    // int& bad_ref = 42;         // 错误：不能绑定右值
    // int& bad_ref2 = x + 1;    // 错误：不能绑定右值

    // === const 左值引用绑定 ===
    std::cout << "\n=== const 左值引用 ===" << std::endl;
    const int& cref = 42;       // OK: const 引用可以绑定右值
    const int& cref2 = x + 1;   // OK
    std::cout << "cref = " << cref << std::endl;   // 42
    std::cout << "cref2 = " << cref2 << std::endl; // 11

    // === 右值引用绑定 ===
    std::cout << "\n=== 右值引用 ===" << std::endl;
    int&& rref = 42;            // OK: 右值引用绑定右值
    int&& rref2 = x + 1;        // OK
    std::cout << "rref = " << rref << std::endl;   // 42
    std::cout << "rref2 = " << rref2 << std::endl; // 11

    // int&& bad_rref = x;       // 错误：右值引用不能绑定左值

    // === 函数返回值与引用 ===
    std::cout << "\n=== 函数返回值 ===" << std::endl;
    // int& ref_to_rvalue = return_by_value();  // 错误：右值不能绑定到非 const 左值引用
    int&& rref_to_rvalue = return_by_value();   // OK: 右值引用绑定右值
    std::cout << "rref_to_rvalue = " << rref_to_rvalue << std::endl; // 42

    int& ref_to_lvalue = return_by_ref();       // OK: 返回左值引用
    ref_to_lvalue = 200;                        // 修改的是 global_var
    std::cout << "global_var = " << global_var << std::endl; // 200

    return 0;
}
```

Compile and run:

```text
g++ -std=c++17 -Wall -Wextra values.cpp -o values
./values
```

```text
=== 左值 ===
&x = 0x7ffd5e8b3a4c
&arr[2] = 0x7ffd5e8b3a58
&(*ptr) = 0x7ffd5e8b3a4c

=== 右值 ===

=== 左值引用 ===
x = 20

=== const 左值引用 ===
cref = 42
cref2 = 11

=== 右值引用 ===
rref = 42
rref2 = 11

=== 函数返回值 ===
rref_to_rvalue = 42
global_var = 200
```

Let's walk through a few key points of this program. The commented-out lines are the ones that would cause compilation errors—you can try uncommenting them to see what errors the compiler reports. This is the fastest way to understand value categories. `return_by_value()` returns `int`, so calling it yields an rvalue, and `int& ref_to_rvalue = return_by_value();` is invalid. `return_by_ref()` returns `int&`, so calling it yields an lvalue reference, and you can directly assign to it—`ref_to_lvalue = 200;` might look a bit odd, but it is actually assigning to `global_var`.

`const int& cref = 42;` is a very important usage. A const lvalue reference can bind to an rvalue, and the compiler automatically extends the lifetime of the temporary value `42`. This technique is extremely common in function parameters—when we don't want to copy a large object and don't need to modify it, using `const T&` as the parameter type is the best choice.

## Try It Yourself

At this point, we have gone through the concepts of lvalues, rvalues, lvalue references, const references, and rvalue references, along with their relationships. Now let's test what you have learned.

### Exercise 1: Classify the Expressions

Determine whether each of the following expressions is an lvalue or an rvalue, and explain why:

- `x` (assuming `int x = 10;`)
- `42`
- `x + y`
- `arr[i]` (assuming `int arr[10];`)
- `x++` (postfix increment)
- `++x` (prefix increment)

If you are unsure, you can write a small program and try taking their address—if you can take its address, it is very likely an lvalue. The difference between `x++` and `++x` is a classic trap and is especially worth thinking about.

### Exercise 2: Predict Reference Binding

Which of the following code snippets will compile? Which will report errors? Judge in your head first, then verify by actually compiling.

```cpp
// (a)
int x = 10;
int& r1 = x;
const int& r2 = x;
int&& r3 = x;

// (b)
const int& r4 = 42;
int& r5 = 42;
int&& r6 = 42;

// (c)
int& r7 = x + 1;
const int& r8 = x + 1;
int&& r9 = x + 1;
```

### Exercise 3: Fix the Dangling Reference

The following code has a serious bug—the function returns a reference to a local variable. Find it and fix it:

```cpp
#include <iostream>

int& get_value() {
    int value = 42;
    return value;
}

int main() {
    int& ref = get_value();
    std::cout << ref << std::endl;
    return 0;
}
```

Hint: Think about whether this function should return by value or by reference. Does the local variable `value` still exist after the function returns?

## Summary

In this chapter, we spent a fair amount of time understanding value categories—lvalues, rvalues, and their relationship with references. An lvalue is an expression with a name, an address, and a relatively long lifetime. An rvalue is a temporary expression without persistent identity. An lvalue reference `T&` can only bind to lvalues, a const lvalue reference `const T&` can bind to everything, and an rvalue reference `T&&` (C++11) can only bind to rvalues. References must be initialized, cannot be rebound, and the most common pitfall is returning a reference to a local variable.

This knowledge might seem theoretical, but it is the foundation for understanding subsequent content. When we get to move semantics (in Volume Two), you will find that today's concepts become the key factors determining program performance. But there is no need to rush—let's build a solid foundation first.

In the next chapter, we move on to control flow—learning to make decisions with `if`/`else` and repeat tasks with loops, so our programs can truly "think."
