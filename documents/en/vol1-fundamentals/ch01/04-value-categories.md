---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand the concepts of lvalues and rvalues, master the basic usage
  of references, and lay the foundation for subsequent move semantics.
difficulty: beginner
order: 4
platform: host
prerequisites:
- const 初探
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
  source_hash: 520fc7977a98b5946eeab9f787c08f5408bed7e5d302ef08b3cfa64cbfe152d3
  translated_at: '2026-06-16T03:40:55.611165+00:00'
  engine: anthropic
  token_count: 2069
---
# Introduction to Value Categories

By this chapter, we have worked quite a bit with variables, types, and `const`. But have you ever wondered: why can some expressions appear on the left side of an assignment operator, while others can only appear on the right? Why does `int x = 42;` compile, but `42 = x;` does not? Behind these seemingly scattered phenomena lies a unifying thread—**value categories**.

"Value category" might sound like an academic term, but it directly determines how the compiler handles every expression you write: which operations are legal and which are not, what a reference can bind to, and how function return values are passed. You could say that without understanding value categories, when you later learn about references, move semantics, and perfect forwarding, you will remain in a state of "knowing how to write it but not why." So, let's get this straight in this chapter. We won't go extremely deep (the classification of value categories after C++11 is actually quite complex), but we will clarify the core concepts of lvalues and rvalues, and solidify the basic usage of references.

## What is an lvalue—A named storage location

The term lvalue comes from the historical definition of "value that can appear on the left side of an assignment" in C. While this definition isn't entirely accurate, it does offer a good intuition. In more modern terms, an lvalue is an expression that **has a name and a determined memory address**—you can take its address (using the `&` operator), and its lifetime does not terminate immediately when the current expression ends.

You can imagine an lvalue as a storage locker with a label: the locker has its own location (memory address), the label lets you find it at any time (variable name), and you can put things in or take things out of it.

The most typical lvalue is an ordinary variable. In `int x = 10;`, `x` is an lvalue—it has the name `x`, has a memory address `&x`, and exists until its scope ends. Similarly, a dereferenced pointer is also an lvalue; `*ptr` represents "that block of memory that ptr points to." That block of memory has an address and a name (accessed via `*ptr`), so it is an lvalue. Array elements are the same; `arr[3]` refers to the memory at the fourth position in the array, so it is naturally an lvalue.

Let's look at a few specific examples:

```cpp
int x = 10;          // x is an lvalue
int* ptr = &x;       // ptr is an lvalue
*ptr = 20;           // *ptr is an lvalue
int arr[5] = {0};    // arr is an lvalue
arr[3] = 5;          // arr[3] is an lvalue
```

These expressions share a common characteristic: you can take their address. `&x`, `&ptr`, `&arr[3]` are all legal operations. This is actually the most practical method to determine if an expression is an lvalue—if you can take its address and it has a name, it is basically an lvalue.

> ⚠️ **Watch Out**: Don't equate "lvalue" with "can appear on the left side of an assignment." In `const int x = 10;`, `x` is an lvalue, but `x = 5;` will not compile—const lvalues cannot be assigned to. Lvalue describes "having an identity" (having a memory address), not "being modifiable."

## What is an rvalue—A temporary existence

An rvalue is the opposite of an lvalue: it is an expression **without a persistent identity**, usually a temporarily generated value. You cannot take its address, and it might disappear after the expression is evaluated.

You can imagine an rvalue as a package delivered by a courier: the package is delivered to your hands (the expression's value is calculated), you can open it and look, but you can't stuff things back into the courier's package (cannot take address, cannot assign), because that package is just a temporary medium for transfer.

The most typical rvalue is a literal. `42` is an rvalue—it is the integer 42, but "42" has no memory address (at least not at your code level), you cannot write `&42`. The result of the expression `x + 1` is also an rvalue. When the compiler calculates `x + 1`, it puts the result in a temporary location. This temporary location has no name, and you cannot reference it via a variable name.

```cpp
42;           // 42 is an rvalue
x + 1;        // The result of x + 1 is an rvalue
std::string("hello"); // A temporary string object is an rvalue
```

You cannot take their address: `&42` will not compile, and `&(x + 1)` will not compile either. The compiler will tell you directly—these are temporary values with no address to take.

> ⚠️ **Watch Out**: Pay special attention to function return values. If a function returns a value (not a reference), such as `int func()`, then calling `func()` results in an rvalue—it is a temporary value copied out from inside the function, without a persistent identity. But if you write `int& func()`, then `func()` returns a reference, and its result is an lvalue—because it ultimately binds to a variable with an identity.

## Why distinguish them—Rules of reference binding

Just knowing "what is an lvalue and what is an rvalue" is not enough; the key is to understand how this distinction affects the code you actually write. The most direct impact is **reference binding**.

C++ has several types of references. Let's start with the most basic one: lvalue references. An lvalue reference is represented by `T&`, and it must bind to an lvalue—this makes sense, because the essence of a reference is an "alias." You must first have a real, persistent variable before you can give it an alias.

```cpp
int x = 10;
int& ref = x;  // OK: ref is an lvalue reference binding to lvalue x
```

But if you try to make an lvalue reference bind to an rvalue:

```cpp
int& ref = 42; // Error: cannot bind non-const lvalue reference to an rvalue
```

The compiler will refuse directly, and the error message will look something like this:

```text
error: invalid initialization of non-const reference of type 'int&' from an rvalue of type 'int'
```

The reason is intuitive: `42` is a temporary value, and its lifecycle might end when this line of code finishes. If you use a reference to point to it, by the time this line executes, the thing the reference points to might no longer exist—this is a "dangling reference," a classic safety hazard. The compiler stops you here to help you avoid problems.

There is, however, an exception—a const lvalue reference can bind to an rvalue:

```cpp
const int& ref = 42; // OK: const reference extends the lifetime of the temporary
```

This seems a bit counter-intuitive, but the C++ standard makes a special provision here: when a const lvalue reference binds to an rvalue, the compiler automatically extends the lifetime of that temporary value to live until the reference goes out of scope. This is actually a very practical feature—later you will often see `const T&` in function parameters. It can accept both lvalue and rvalue arguments because of this rule.

## Reference basics—Not a pointer, but better than a pointer

Since we mentioned references, let's clarify their basic usage. A reference is conceptually simple—it is an **alias** for an existing variable. From the moment you create it, it is bound together with the referenced variable; any operation on the reference is equivalent to an operation on the original variable.

```cpp
int x = 10;
int& ref = x; // ref is an alias for x
ref = 20;     // x becomes 20
```

References have several important characteristics that must be understood from the start. First, a reference **must be initialized when created**—you cannot declare a reference first and then make it point to a variable later. `int& ref;` will simply not compile; the compiler will tell you the reference needs initialization. Second, once a reference is bound, it cannot be changed—there is no operation to "make a reference point to another variable." If you write `ref = y;`, you are not rebinding `ref` to `y`, you are assigning the value of `y` to the variable that `ref` refers to. This is completely different from a pointer's behavior; a pointer can point to different addresses at any time.

The most common use of references is as function parameters. If we pass by value, the function gets a copy of the parameter, and modifying the copy does not affect the original data. If we pass by reference, the function operates directly on the original data. For large objects (like a very long string or a container with many elements), passing by value implies an expensive copy operation, while passing by reference has no extra overhead.

```cpp
void modifyByValue(int x) {
    x = 20; // Only modifies the local copy
}

void modifyByReference(int& x) {
    x = 20; // Modifies the original variable
}
```

> ⚠️ **Watch Out**: This is one of the easiest mistakes for beginners to make—returning a reference to a local variable from a function. The local variable is destroyed after the function returns, so the reference points to something that no longer exists. Accessing it is **undefined behavior**—you might get garbage data, it might crash, or it might accidentally look normal—but regardless, it is wrong.

```cpp
int& dangerous() {
    int local = 42;
    return local; // ERROR: returning reference to local variable 'local'
}
```

The compiler will usually give a warning, but it won't stop you from compiling. Remember a simple principle: **never return a reference or pointer to a local variable**.

## Rvalue references—Just get familiar with them (C++11)

Before C++11, C++ had only one kind of reference—what we just discussed, the lvalue reference. C++11 introduced **rvalue references**, represented by `T&&`, which can only bind to rvalues.

```cpp
int&& rref = 42; // OK: rref is an rvalue reference binding to rvalue 42
```

You might ask: What is the use of an rvalue reference? Why create a special kind of reference that can only bind to temporary values? The answer is **move semantics**—it allows us to "steal" resources from temporary values rather than making an expensive copy. For example, for a container containing a million elements, when you no longer need the original, using move semantics allows you to directly take over the internal pointer at almost zero cost.

We won't expand on this here; just remember the `T&&` syntax and know that it is a reference prepared for rvalues. Move semantics is a major topic in Volume II, where we will dive deep.

## Hands-on experiment—values.cpp

We've discussed a lot of theory, so let's write a complete program to verify these rules. This program will demonstrate whether various expressions are lvalues or rvalues, and the various situations of reference binding.

```cpp
#include <iostream>
#include <string>

// Returns by value -> result is an rvalue
int getValue() {
    return 42;
}

// Returns by reference -> result is an lvalue
int& getReference() {
    static int x = 10; // static to avoid dangling reference
    return x;
}

int main() {
    // 1. Basic lvalue and rvalue
    int x = 10;
    // int& ref1 = 5;        // ERROR: lvalue reference cannot bind to rvalue
    const int& ref2 = 5;   // OK: const reference extends lifetime

    // 2. Function return values
    // int& ref3 = getValue(); // ERROR: getValue() returns an rvalue
    const int& ref4 = getValue(); // OK: const reference binds to rvalue

    int& ref5 = getReference();    // OK: getReference() returns an lvalue
    ref5 = 100;                    // Modifies the static variable x inside getReference

    // 3. Rvalue reference
    int&& rref1 = getValue(); // OK: rvalue reference binds to rvalue
    // int&& rref2 = x;          // ERROR: rvalue reference cannot bind to lvalue

    std::cout << "x = " << x << std::endl;
    std::cout << "ref5 = " << ref5 << std::endl;
    std::cout << "rref1 = " << rref1 << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++11 values.cpp -o values
./values
```

Output:

```text
x = 100
ref5 = 100
rref1 = 42
```

Let's walk through the key points of this program. The commented-out lines are ones that would cause compilation errors—you can try uncommenting them to see what errors the compiler reports; this is the fastest way to understand value categories. `getValue()` returns `int`, so calling it yields an rvalue, meaning `int& ref3 = getValue();` is illegal. `getReference()` returns `int&`, so calling it yields an lvalue reference, and you can assign to it directly—`ref5 = 100;` looks a bit weird, but it is indeed assigning to the static variable `x` inside `getReference`.

`const int& ref2 = 5;` is a very important usage. A const lvalue reference can bind to an rvalue, and the compiler automatically extends the lifetime of the temporary value `5`. This technique is extremely common in function parameters—when we don't want to copy a large object and don't need to modify it, using `const T&` as the parameter type is the best choice.

## Try it yourself

At this point, we have covered the concepts of lvalues, rvalues, lvalue references, const references, and rvalue references, and the relationships between them. Next, let's check your learning progress.

### Exercise 1: Category determination

Determine whether each of the following expressions is an lvalue or an rvalue, and state the reason:

- `x` (assuming `int x;`)
- `x + 1`
- `std::string("hello")`
- `*ptr` (assuming `int* ptr;`)
- `x++` (post-increment)
- `++x` (pre-increment)

If you aren't sure, you can write a small program and try to take their address—if you can take the address, it's likely an lvalue. The difference between `x++` and `++x` is a classic trap and is worth thinking about specifically.

### Exercise 2: Predict reference binding

Which of the following code snippets will compile? Which will report an error? Judge in your head first, then actually compile to verify.

```cpp
int x = 10;
const int& r1 = x;     // ?
int& r2 = x * 2;       // ?
const int& r3 = x * 2; // ?
int&& r4 = x;          // ?
int&& r5 = x * 2;      // ?
```

### Exercise 3: Fix the dangling reference

The following code has a serious bug—the function returns a reference to a local variable. Find it and fix it:

```cpp
std::string& getGreeting() {
    std::string temp = "Hello";
    return temp; // BUG: returns reference to local variable
}
```

Hint: Think about whether this function should return a value or a reference? Does the local variable `temp` still exist after the function returns?

## Summary

In this chapter, we spent a fair amount of time understanding value categories—lvalues, rvalues, and their relationship with references. Lvalues are expressions with names, addresses, and longer lifecycles; rvalues are temporary expressions without persistent identities. Lvalue references `T&` can only bind to lvalues, const lvalue references `const T&` can bind to anything, and rvalue references `T&&` (C++11) can only bind to rvalues. References must be initialized, cannot be rebound, and the most common trap is returning a reference to a local variable.

This knowledge might seem theoretical, but it is the cornerstone for understanding subsequent content. When we get to move semantics (Volume II), you will find that today's concepts become key factors in determining program performance. But for now, no need to rush; let's get the basics solid first.

In the next chapter, we move into control flow—learning to use `if`/`else` for judgment and loops for repetition, to make the program truly "think."
