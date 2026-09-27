---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand the concepts of lvalues and rvalues, master the basic usage
  of references, and lay the foundation for move semantics later on.
difficulty: beginner
order: 4
platform: host
prerequisites:
- A First Look at const
reading_time_minutes: 14
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Introduction to Value Categories
translation:
  source: documents/vol1-fundamentals/ch01/04-value-categories.md
  source_hash: c493e5f8f05ecefb7201e8e9f8a3ee956aac67a932c235c22a484b63623ad335
  translated_at: '2026-09-25T09:59:35+00:00'
  engine: anthropic
  token_count: 8900
---
# The Episode Where Difficulty Ramps Up: Value Categories in C++

The way I see it, telling value categories apart is a small mountain peak. Everyone, get ready for the climb!

We have already dealt plenty with variables, types, and `const`. But have you ever paused to wonder: why can some expressions sit on the left side of an assignment operator while others can only go on the right? Why does `int& ref = x;` compile, but `int& ref = 42;` doesn't? Skip past these questions, and it becomes very easy to fall into pits later in the journey!

Value categories directly determine **how the compiler treats every expression you write**: which operations are legal and which are not, what a reference can bind to, how function return values get passed along... You could say that without understanding value categories, when you later study references, move semantics, and perfect forwarding, you will stay stuck in a "knows how to write it but not why" state. So let's get this sorted out in this chapter. We won't go especially deep (the classification of value categories after C++11 is actually quite complex), but at minimum we'll get the core ideas of lvalues and rvalues straight and put the basic usage of references on solid footing.

## What Is an lvalue? Turns Out It's a Named Storage Location

The term lvalue comes from the historical definition of "a value in C that can sit on the left side of an assignment." That saying isn't entirely accurate, but it does provide a decent bit of intuition. In more modern terms, an lvalue is an expression that **has a name and a definite memory address**—you can take its address (with the `&` operator), and its lifetime doesn't end the instant the current expression finishes.

You can picture an lvalue as a labeled storage box: the box has its own location (a memory address, whether on the stack or on the heap), and the label means **you can find it at any time** (the variable name); you can put things in or take things out. In other words, while the program is running, you can point at a piece of memory and say, "Hey, you looking for x? Don't ask me—go look over there!"—that kind of thing.

The most typical lvalue is an ordinary variable. In `int x = 10;`, `x` is an lvalue—it has the name `x`, it has the memory address `&x`, and it keeps existing until its scope ends. Similarly, a dereferenced pointer is an lvalue: `*ptr` denotes "the chunk of memory ptr points to," that chunk has an address and a name (accessed through `*ptr`), so it is an lvalue. Array elements are the same: `arr[3]` refers to the memory at the fourth position of the array, so of course it is an lvalue.

Let's look at a few concrete examples:

```cpp
int x = 10;       // x is an lvalue: a small patch of stack space used to hold 10
int* ptr = &x;    // ptr is an lvalue: &x pulled out x's address and stored it into this little memory slot called ptr—you can find this ptr in memory.
*ptr = 20;        // What does ptr point to? Oh, it holds x's address—let me go see where x is. Found it: back when we wrote int x, the compiler dropped it somewhere on the stack; scribble out the 10 and write in a 20
int arr[5] = {};
arr[2] = 42;      // arr[2] is an lvalue—it stands for the memory at the third position of the array
```

These expressions all share one characteristic: you can take their address. `&x`, `&(*ptr)`, `&(arr[2])` are all legal operations. This is in fact the most practical way to judge whether an expression is an lvalue—if you can take its address and it has a name, it is basically an lvalue.

Don't equate "lvalue" with "can sit on the left side of an assignment": in `const int cx = 10;`, `cx` is an lvalue, yet `cx = 20;` won't compile—const lvalues cannot be assigned to. Lvalue-ness describes "having an identity" (having a memory address), not "being modifiable."

## What Is an rvalue — A Fleeting, Temporary Existence

An rvalue is precisely the opposite of an lvalue: it is an expression **without a persistent identity**, usually a value produced on the fly. You cannot take its address, and it may vanish once the expression has been evaluated. Viewed this way, you can think of it as something like an immediate value—used once and tossed; you can't even track down where the thing ended up.

The most typical rvalue is a literal. `42` is an rvalue—it is the integer 42, but "42" has no memory address (at least not at the level of your code); you cannot write `&42`. The result of the expression `x + y` is also an rvalue: when the compiler computes `x + y`, it drops the result into a temporary spot, that spot has no name, and there is no way to refer to it through a variable name.

```cpp
42                  // rvalue—a literal
3.14                // rvalue—a floating-point literal
x + y               // rvalue—the temporary result of an arithmetic expression
static_cast<int>(3.14)  // rvalue—a temporary value produced by a type conversion
```

The function-return case deserves special attention: if a function returns a value (not a reference)—say `int get_value() { return 42; }`—then the result of calling `get_value()` is an rvalue; at the syntactic level, **it is a temporary value copied out from inside the function, with no persistent identity**. But if you write `int& get_ref() { return x; }`, `get_ref()` returns a reference and the result is an lvalue—because it ultimately binds to a variable that has an identity.

## Why the Distinction Matters — The Rules of Reference Binding

Merely knowing "what an lvalue is and what an rvalue is" isn't enough; what matters is understanding how this distinction affects the code you actually write. The most direct impact is **reference binding**.

C++ has several kinds of references; let's start from the most basic one, the lvalue reference. Written as `T&`, it must bind to an lvalue—which makes sense, because a reference is essentially an "alias": you must first have a real, lasting variable before you can give it another name.

```cpp
int x = 10;
int& ref = x;     // No problem: ref is an alias for x
ref = 20;          // x is now 20 as well
```

But if you try to make an lvalue reference bind to an rvalue:

```cpp
int& ref = 42;    // Compilation error!
```

The compiler will refuse outright, with an error message that looks roughly like this:

```text
error: cannot bind non-const lvalue reference of type 'int&' to an rvalue of type 'int'
```

The reason is intuitive: `42` is a temporary value whose lifetime may expire exactly when this line of code ends. If you point a reference at it, then once this line finishes executing, the thing the reference points to may already be gone—**this is precisely a "dangling reference," a classic safety hazard. The compiler stopping you here is doing you a favor.**

There is one exception, though—a const lvalue reference can bind to an rvalue:

```cpp
const int& ref = 42;   // Legal!
```

This looks a bit counterintuitive, but the C++ standard makes a special provision here: **when a const lvalue reference binds to an rvalue, the compiler automatically extends that temporary's lifetime so that it lives until the reference's scope ends.** This is actually a very practical feature—later on you will often see `const std::string&` in function parameters, and **the reason it accepts both lvalue arguments and rvalue arguments is precisely this rule.**

> Personally, I think the design isn't great: convenient, yes, but it adds one more exception to keep track of while coding. No matter—later, when we learn move semantics, passing an rvalue will match a better interface.

## Reference Basics — Not a Pointer, Yet Better Than One

Since references have come up, let's lay out their basic usage properly. Conceptually, a reference is simple—it is an **alias** for an already-existing variable. From the moment you create it, it is bound to the referenced variable, and any operation you perform on the reference is equivalent to operating on the original variable.

> Feels like a pointer! Right! One way to understand it—my way, at least—is as a layer of encapsulation over pointers that spares you from writing a whole pile of `*`s!

```cpp
int x = 10;
int& ref = x;    // ref is an alias for x
ref = 20;        // x is now 20
std::cout << x;  // prints 20
```

References have several important properties that you need to get right from the very start. First, a reference **must be initialized at creation**—you cannot declare a reference first and point it at some variable later. `int& ref;` flat-out fails to compile; the compiler will tell you a reference needs an initializer. Second, once a reference is bound it cannot be changed—there is no such operation as "point the reference at another variable." If you write `ref = y;`, that is not rebinding ref to y; it is assigning y's value to the variable that ref refers to. This is completely different from a pointer's behavior—a pointer can point at a different address whenever it likes.

The most common use of references is as function parameters. If we pass by value, the function gets a copy of the argument, and modifying the copy leaves the original data untouched; if we pass by reference, the function operates on the original data directly. For large objects (say, a very long string or a container holding many elements), passing by value means an expensive copy operation, while passing by reference carries no extra overhead at all.

```cpp
/// @brief Pass by value—modifications inside the function don't affect the outside
void add_one_by_value(int n)
{
    n = n + 1;    // Only modifies the local copy
}

/// @brief Pass by reference—the function modifies the outer variable directly
void add_one_by_ref(int& n)
{
    n = n + 1;    // Modifies the original variable
}

int main()
{
    int a = 10;
    add_one_by_value(a);
    std::cout << a << "\n";   // prints 10—unchanged

    add_one_by_ref(a);
    std::cout << a << "\n";   // prints 11—changed

    return 0;
}
```

Returning a reference to a local variable is one of the mistakes beginners make most easily: the local variable is destroyed once the function returns, the thing the reference points to no longer exists, and accessing it is **undefined behavior**—you might get garbage data, you might crash, or it might happen to look normal, but whichever way it goes, it's wrong.

```cpp
int& bad_function()
{
    int local = 42;
    return local;    // Serious error! local is destroyed after the function returns
}                    // The returned reference points to a destroyed variable
```

The compiler will usually warn you, but it won't stop you from compiling. Remember one simple principle: **never return a reference or a pointer to a local variable**. For now, that gives us a usable minimal set of rules about references; as for which scenarios references and pointers each cover and how to choose between them, Chapter 4 will put the two side by side for a systematic comparison.

## Rvalue References — Just Getting Acquainted (C++11)

Before C++11, C++ had exactly one kind of reference—the lvalue reference we just discussed. C++11 introduced the **rvalue reference**, written `T&&`, which can only bind to rvalues.

```cpp
int x = 10;
int& lref = x;        // lvalue reference, bound to the lvalue x
int&& rref = 42;      // rvalue reference, bound to the rvalue 42
int&& rref2 = x + 1;  // rvalue reference, bound to a temporary expression result

// int&& rref3 = x;   // Compilation error! An rvalue reference cannot bind to an lvalue
```

You might ask: what are rvalue references for? Why go out of the way to create a kind of reference that only binds to temporaries? The answer is **move semantics**—it lets us "steal" the resources inside a temporary value instead of making an expensive copy. Take a container holding a million elements: once you no longer need the original one, move semantics lets you take over its internal pointers directly, at nearly zero cost.

We won't unpack all of that here; for now, just remember the `T&&` spelling and know that it is the reference set aside for rvalues—that's enough. Move semantics is a major topic in Volume Two, and we will cover it in depth there.

## Hands-On Experiment — values.cpp

That's a lot of theory, so let's write a complete program to verify these rules. This program will show which expressions are lvalues and which are rvalues, and run through the various reference-binding cases.

```cpp
// values.cpp -- a demo of value categories and reference binding
// Standard: C++11

#include <iostream>

/// @brief Returns an integer value (an rvalue)
int get_value()
{
    return 42;
}

/// @brief Returns a reference to an integer (an lvalue)
int global = 100;
int& get_ref()
{
    return global;
}

int main()
{
    // ---- lvalues ----
    int x = 10;            // x is an lvalue
    int* ptr = &x;         // &x is legal: x is an lvalue, its address can be taken
    *ptr = 20;             // *ptr is an lvalue
    int arr[3] = {1, 2, 3};
    arr[0] = 99;           // arr[0] is an lvalue

    std::cout << "x = " << x << "\n";            // 20
    std::cout << "arr[0] = " << arr[0] << "\n";  // 99

    // ---- rvalues ----
    // &42;                  // error: cannot take the address of an rvalue
    // &(x + 1);             // error: the result of x + 1 is an rvalue
    // &get_value();          // error: a function's return value is an rvalue

    int sum = x + arr[1];   // the result of x + arr[1] is an rvalue
    std::cout << "sum = " << sum << "\n";        // 22

    // ---- lvalue references ----
    int& lref = x;          // OK: an lvalue reference bound to an lvalue
    lref = 30;
    std::cout << "x = " << x << "\n";            // 30

    // int& bad = 42;        // error: an lvalue reference cannot bind to an rvalue

    const int& cref = 42;   // OK: a const reference can bind to an rvalue
    std::cout << "cref = " << cref << "\n";      // 42

    // ---- rvalue references (C++11) ----
    int&& rref = 42;        // OK: an rvalue reference bound to an rvalue
    int&& rref2 = x + 1;   // OK: x + 1 is an rvalue
    // int&& rref3 = x;     // error: an rvalue reference cannot bind to an lvalue

    std::cout << "rref = " << rref << "\n";      // 42
    std::cout << "rref2 = " << rref2 << "\n";    // 31

    // ---- value categories of function return values ----
    // get_value() returns an rvalue
    int val = get_value();
    std::cout << "get_value() = " << val << "\n";   // 42

    // get_ref() returns an lvalue
    get_ref() = 200;       // OK: get_ref() returns an lvalue reference, which can be assigned to
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

Let's walk through the key points of this program. Every commented-out line is one that would cause a compilation error—try uncommenting them and seeing what error the compiler reports; it's the fastest way to understand value categories. `get_value()` returns `int`, so calling it yields an rvalue, which makes `&get_value()` illegal. `get_ref()` returns `int&`, so calling it yields an lvalue reference that you can assign to directly—`get_ref() = 200;` looks a bit weird, but it really is assigning to `global`.

`const int& cref = 42;` is a very important idiom. A const lvalue reference can bind to an rvalue, and the compiler automatically extends the lifetime of the temporary `42`. This trick is extremely common in function parameters—when we don't want to copy a large object and don't need to modify it either, `const T&` is the best choice of parameter type.

## Try It Yourself

At this point we've been through lvalues, rvalues, lvalue references, const references, and rvalue references, plus the relationships among them. Now let's check what you've learned.

### Exercise 1: Classify Them

For each expression below, decide whether it is an lvalue or an rvalue, and explain why:

- `x` (assume `int x = 5;`)
- `x + 3`
- `"hello"`
- `*ptr` (assume `int* ptr = &x;`)
- `x++` (postfix increment)
- `++x` (prefix increment)

If you're unsure, write a small program and try taking their addresses—whatever you can take the address of is most likely an lvalue. Among these, the difference between `x++` and `++x` is a classic trap and deserves some extra thought.

### Exercise 2: Predict the Reference Binding

Which of the following lines compile, and which produce errors? Judge in your head first, then actually compile to verify.

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

The code below contains a serious bug—the function returns a reference to a local variable. Find it and fix it:

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

Hint: think about it—should this function return a value or return a reference? Does the local variable `result` still exist after the function returns?

::: details Reference answer

`result` is a local variable inside the function body; its lifetime ends when the function returns. So the original code, returning `int&`, yields a dangling reference—and the caller's subsequent read of `m` through that reference is undefined behavior. Here `a` and `b` are also local copies passed in by value, so they likewise cannot be returned by reference.

This function should return `int` by value. At the return, the function initializes the result into the return object that the caller can receive; even if `result` is destroyed afterwards, the already-returned integer is unaffected. The caller should also receive it with a plain `int` rather than declaring another reference:

```cpp
#include <iostream>

int get_max(int a, int b)
{
    return (a > b) ? a : b;
}

int main()
{
    int m = get_max(3, 7);
    std::cout << m << "\n";
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
7
```

:::
