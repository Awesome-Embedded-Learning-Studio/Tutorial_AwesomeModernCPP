---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the various uses of `const` with variables and pointers, and get
  a first taste of `constexpr` compile-time constants.
difficulty: beginner
order: 3
platform: host
prerequisites:
- Type Conversion
reading_time_minutes: 16
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: A First Look at const
translation:
  source: documents/vol1-fundamentals/ch01/03-const-basics.md
  source_hash: 40684fabacdf7a729bbc8236ce50fa035797a3af780e842eba676ae0873c84f9
  translated_at: '2026-09-25T09:59:29+00:00'
  engine: anthropic
  token_count: 3400
---
# Long Time No See, `const` — Look How Immutable You've Become

When we write code, **some things simply should not be changed**—a configuration parameter, once set, should not be accidentally overwritten; an array's capacity, once declared, should not change again; and physical constants like pi go without saying. **If we rely purely on "self-discipline" to keep these values untouched, that is no different from walking down a dark road with our eyes closed. Sooner or later someone's hand slips, a critical value gets modified, and half a day goes into chasing down a baffling bug. In other words, a guarantee built into the mechanism beats whatever you keep in your head!**

C++ hands us a safety lock: `const`. The core idea is dead simple—if something should not change, say so explicitly and let the compiler keep watch for us. Any code that tries to modify a `const` value **gets stopped dead at the compilation stage**. Compared with discovering in production that some data was accidentally tampered with, strangling the problem at compile time is clearly the more dependable option. (This is why Rust simply flips the whole thing around: unless you say a variable is mutable, it is immutable! So variables are effectively declared `const` by default!)

## Putting a Lock on Variables — Basic `const` Usage

Suppose we have the maximum capacity of a buffer (a simple way to think about it: a spot where we put things away for use in a moment), a value that should never change for as long as the program runs:

```cpp
const int kMaxBufferSize = 1024;
```

Once `const` is attached, the variable becomes "read-only"—we must give it an initial value at declaration, and from then on any operation that tries to modify it will be rejected by the compiler. Let's give it a try:

```cpp
const int kMaxBufferSize = 1024;
kMaxBufferSize = 2048;  // Compile error!
```

The compiler will produce a very explicit error message:

```text
error: assignment of read-only variable 'kMaxBufferSize'
```

This is the core value of `const`—it turns "I shouldn't modify this value" from a convention based on self-discipline into a rule enforced by the compiler. **You might ask: isn't this just using the compiler as a bodyguard? Exactly, that is precisely the idea—and this bodyguard never dozes off.**

### What Exactly Is the Difference Between `const` and `#define`

If you have some C under your belt, you might say, "I can do this with `#define` too." True, `#define MAX_SIZE 1024` looks roughly the same in effect, but there are several key differences between the two.

First, a `const` variable **has an explicit type (much cleaner semantics!)**. The `int` in `const int kMaxBufferSize = 1024;` tells the compiler this is an integer; if you later accidentally assign it to a `double`, the compiler can perform type checking and even issue a warning. `#define`, on the other hand, is plain text substitution—the preprocessor does not care about types at all. **It just dutifully replaces every `MAX_SIZE` with `1024`; whether that `1024` is an integer or a floating-point number is none of its business. Whose business is it? Yours!**

Second, `const` variables follow normal scoping rules. A `const` variable declared inside a function is visible only within that function, and a `const` variable declared at global scope has internal linkage by default (in other words, other `.cpp` files cannot see it). A `#define`, once expanded, is in effect from its point of definition all the way to the end of the file, with no scope restriction whatsoever—**which easily breeds name collisions in large projects.**

That is why, in C++, I prefer `const`—or `constexpr`, which we will meet later—for defining constants, **and keep `#define` for the scenarios that genuinely need conditional compilation. That is where `#define` truly earns its place in C++, and especially in modern C++!**

> Sharp-eyed readers may notice that my `const` constants look rather distinctive—why do they start with `k`? The answer is the `kPascalCase` style, as in `kMaxBufferSize`, `kDefaultBaudRate`, `kPi`. This `k` prefix is a fairly common constant-naming convention in the C++ community; you can tell at a glance that this is a value not meant to be modified. In truth, I lifted it from Google Chrome's constant-naming guidelines.

## When `const` Meets Pointers — Not That Commonly Used, but Worth Mentioning

As for `const`, our advice is: if there is a need, add it.

Using `const` to modify a plain variable by itself is simple, but once `const` meets pointers, things start getting interesting. Plenty of people get thoroughly turned around by this part—including the author himself, who got stuck here for a long time when first learning. Don't panic; let's take it apart step by step.

The core question is: does `const` apply to the pointer itself, or to the data the pointer points to? The answer **depends on where the `const` appears**. There are three ways to combine `const` with a pointer declaration in C++, and we will look at them one by one.

### Pointer to a Constant: `const int* p`

```cpp
int value = 42;
const int* p = &value;
```

Here `const` applies to the `int` (let me add the parentheses this way: (const int)* p—does that click now?). In other words, modifying the data `p` points to through `p` is not allowed. But the pointer `p` itself can change—it may point to a different address. You can understand it as "this pointer is well-behaved: it promises not to modify the target data through itself."

```cpp
int x = 10;
int y = 20;
const int* p = &x;

*p = 100;   // Compile error! Cannot modify data through a const int*
p = &y;     // Fine, the pointer itself can point elsewhere
```

Note one detail: although you cannot modify `x` through `p`, `x` itself is not `const`. Modifying it directly with `x = 100;` is perfectly legal—`const int*` only says "I won't modify through this pointer"; it does not mean the target data is actually immutable.

### Constant Pointer: `int* const p`

```cpp
int value = 42;
int* const p = &value;
```

Here, let me parenthesize it this way: int* (const p)—`p` itself is the pointer, and it is the `const` one. **So just look to the right and see what it binds to first.** This time `const` applies to the pointer variable `p` itself. That is, once the pointer is initialized, it is glued to that one address and cannot point anywhere else. Modifying the target data through `p`, however, is completely allowed.

```cpp
int x = 10;
int y = 20;
int* const p = &x;

*p = 100;   // Fine, the data can be modified
p = &y;     // Compile error! The pointer itself is const and cannot be repointed
```

You can think of it as a "one-track-minded pointer"—once it has settled on an address it will not budge, but the contents at that address are fair game for it to modify.

### Both const: `const int* const p`

```cpp
int value = 42;
const int* const p = &value;
```

**This form stacks the two constraints above: the pointer itself cannot be repointed, and the data cannot be modified through the pointer. You actually see this quite often in function parameters—when you pass a pointer to a function and want neither the pointer's target changed inside the function nor the data modified, this is how you write it.**

## `const` and References

With pointers done, let's look at references. Pairing `const` with references is much simpler than with pointers, because references themselves are not allowed to rebind—from the moment it is born, a reference is welded to some variable. So there is only one case for combining `const` with a reference:

```cpp
int x = 42;
const int& ref = x;
```

`ref` is an alias for `x`, but you cannot modify `x`'s value through `ref`. Similar to `const int*`, this only says "I won't modify through `ref`"—`x` itself can still be freely modified.

This kind of "reference to a constant" has one hugely important use in real-world development—function parameters. Imagine you have a function that needs to take a `std::string` parameter:

```cpp
void print(std::string s)
{
    std::cout << s << std::endl;
}
```

Every call to `print("hello")` triggers a copy of the string. If the string is long, or the function is called frequently, that copying overhead becomes impossible to ignore. Switching to a `const` reference solves it:

> We have not yet covered the move mechanism in modern C++. In the C++98 era, we almost never wrote pass-by-value parameters; it was not until C++11, when `std::move` and rvalues arrived, that we finally had better semantics for this.

```cpp
void print(const std::string& s)
{
    std::cout << s << std::endl;
}
```

`const std::string& s` means: take a reference (no copy), but promise not to modify it. This avoids the copying overhead while assuring the caller of safety. The `const T&` parameter pattern appears at an extremely high frequency in C++; later chapters will run into it again and again, so for now just carry the impression with you.

## `constexpr` — Letting the Compiler Do the Math for You

So far, the `const` we have been talking about only means "this value will not change during execution." But some constants have values that are already settled at the compile stage—`5 * 5` is definitely `25`, so there is no need to wait for the program to run to compute it. C++11 introduced `constexpr` to tell the compiler explicitly: "this is a value you can work out at compile time." If you are familiar with assembly, the meaning becomes plain—it gets computed into an immediate for you, with nothing left to process at runtime.

```cpp
constexpr int kSquare = 5 * 5;           // Computed at compile time, value is 25
constexpr int kBufferSize = 1024 * 64;   // Also computed at compile time

// Under some very low optimization levels, the compiler really will direct the CPU at runtime to do two
// register loads and one register multiply. Far slower than directly stuffing the precomputed number into
// a register; in other words, at this granularity the program runs several or even tens of times slower
const int kSquare = 5 * 5;           // Computed at compile time, value is 25
const int kBufferSize = 1024 * 64;   // Also computed at compile time
```

"Hold on? Charliechen114514, let me ask you: isn't `const` also unmodifiable? Why does C++ bother with something so redundant?"

It is not redundant. `const` merely reminds the compiler that this thing must not be modified, but it does not tell the compiler that it can simply compute the result out directly. So with low optimization turned on, you can actually catch the CPU earnestly computing that 5 x 5 is 25! And everyone knows that when you write the literal 5 x 5, you might as well just write 25 directly.

```cpp
int x = 10;
const int cx = x;          // const but not constexpr, because x's value is only known at runtime
constexpr int kVal = 42;   // constexpr, which is at the same time const
```

Where `constexpr` gets more powerful is that it can be applied to functions. A `constexpr` function means: if the arguments passed in are all values determinable at compile time, then the function's return value can also be computed at compile time:

```cpp
constexpr int square(int x)
{
    return x * x;
}

constexpr int kResult = square(5);  // Computed at compile time, kResult = 25; if you don't believe it, have an AI show you how to objdump or dumpbin the assembly—we won't teach that here
```

Values computed at compile time come with a big benefit: they can be used in the places that require a constant expression, such as an array's size:

```cpp
constexpr int kArraySize = square(3);  // 9
int data[kArraySize];                   // Legal, because kArraySize is a compile-time constant
```

If `kArraySize` were merely an ordinary `const`, this line might not pass on some compilers (depending on whether the `const` variable is treated as a constant expression). With `constexpr`, there is no ambiguity whatsoever.

Here we are only getting a first touch of `constexpr`. It is one of the most important features of modern C++—by C++14 it allowed more complex logic inside such functions, C++17 relaxed the restrictions further, and C++20 went on to introduce `consteval` (must execute at compile time) and `constinit`. In embedded C++ we will use these critically important features over and over—**at the language level, they help us lock in both runtime efficiency and binary-size savings.**

## Putting It All Together — const_demo.cpp

Book knowledge only goes so far. Let's now string together every `const` usage discussed above into one complete example program. The logic will not be anything complex, but it covers each `const` combination and verifies the compiler's behavior.

```cpp
// const_demo.cpp — Demonstrates various uses of const variables, pointers, references, and constexpr

#include <iostream>

/// @brief constexpr function: computes a square
/// @param x the value to be squared
/// @return the square of x
constexpr int square(int x)
{
    return x * x;
}

int main()
{
    // --- const variable ---
    const int kMaxSize = 100;
    // kMaxSize = 200;  // Uncommenting this causes a compile error
    std::cout << "kMaxSize = " << kMaxSize << std::endl;

    // --- constexpr ---
    constexpr int kArraySize = square(5);  // Computed at compile time, result is 25
    std::cout << "kArraySize = " << kArraySize << std::endl;

    // --- pointer to a constant ---
    int a = 10;
    int b = 20;
    const int* p_to_const = &a;
    // *p_to_const = 100;  // Uncommenting this causes a compile error
    p_to_const = &b;       // Fine, the pointer can be repointed
    std::cout << "*p_to_const = " << *p_to_const << std::endl;

    // --- constant pointer ---
    int* const const_p = &a;
    *const_p = 100;        // Fine, the data can be modified
    // const_p = &b;       // Uncommenting this causes a compile error
    std::cout << "*const_p = " << *const_p << std::endl;

    // --- both const ---
    const int* const double_const = &a;
    // *double_const = 1;  // Compile error
    // double_const = &b;  // Compile error
    std::cout << "*double_const = " << *double_const << std::endl;

    // --- const reference ---
    int x = 42;
    const int& ref = x;
    // ref = 100;           // Compile error
    x = 100;               // Modifying x directly is fine
    std::cout << "ref = " << ref << std::endl;  // Prints 100

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o const_demo const_demo.cpp
./const_demo
```

Expected output:

```text
kMaxSize = 100
kArraySize = 25
*p_to_const = 20
*const_p = 100
*double_const = 100
ref = 100
```

You can uncomment those "compile error" lines one at a time and see what error messages the compiler produces. Getting a hands-on feel for how the compiler intercepts these operations leaves a far deeper impression than reading text alone.

## Run Online

Run const_demo.cpp online and observe the actual output of the various `const` usages:

<OnlineCompilerDemo
  title="A First Look at const: Variables, Pointers, References, and constexpr"
  source-path="code/examples/vol1/04_const_demo.cpp"
  description="Run online and observe the actual behavior of const pointers, const references, and constexpr."
  allow-run
/>

## Try It Yourself

That's the theory covered—now it is your turn to get hands-on. The three exercises below help you gauge your understanding of `const`; I suggest writing each one out in full, compiling, and running it.

### Exercise 1: Declare const Pointers and Predict the Behavior

Write out the following declarations, then for each pointer attempt (1) modifying the data the pointer points to and (2) modifying what the pointer itself points to. Before compiling, first predict which operations the compiler will reject, then verify your predictions.

- `const int* p1`
- `int* const p2`
- `const int* const p3`

::: details Reference answer

**main.cpp**

```cpp
#include <iostream>
int main()
{
    int a1 = 0;
    int a2 = 0;
    int a3 = 0;

    const int* p1 = &a1;
    // *p1 = 5;   // Compile error! Cannot modify data through a const int*
    p1 = &a2;     // Fine, the pointer itself can point elsewhere
    std::cout << "*p1 = " << *p1 << std::endl;

    int* const p2 = &a2;
    *p2 = 5;      // Fine, the data pointed to by int* const can be modified
    // p2 = &a3;  // Compile error! An int* const pointer itself cannot be repointed
    std::cout << "*p2 = " << *p2 << std::endl;

    p1 = &a3;     // Fine, a const int* pointer itself can point elsewhere

    const int* const p3 = &a3;
    // *p3 = 5;   // Compile error! const int* const can neither modify the data nor change what it points to
    // p3 = &a1;  // Compile error! A const int* const pointer itself cannot be repointed
    std::cout << "*p3 = " << *p3 << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
*p1 = 0
*p2 = 5
*p3 = 0
```

:::

### Exercise 2: Convert #define into constexpr

Below is some C-style code using `#define`. Replace all the macro constants with `constexpr` variables, and write a `constexpr` function `circle_area(double radius)` that computes the area of a circle.

```cpp
#define PI 3.14159265
#define MAX_RADIUS 100.0
#define MIN_RADIUS 0.1
```

::: details Reference answer

**main.cpp**

```cpp
#include <iostream>

constexpr double PI = 3.14159265;
constexpr double MAX_RADIUS = 100.0;
constexpr double MIN_RADIUS = 0.1;

constexpr double clamp_radius(double radius)
{
    return radius < MIN_RADIUS
        ? MIN_RADIUS
        : (radius > MAX_RADIUS ? MAX_RADIUS : radius);
}

constexpr double circle_area(double radius)
{
    const double r = clamp_radius(radius);
    return PI * r * r;
}

int main()
{
    double r = 0;
    std::cout << "请你输入所求圆的半径 : ";
    std::cin >> r;
    std::cout << "半径为" << r << "的面积是: " << circle_area(r) << std::endl;
    return 0;
}
```

The exercise only hands you three macros and does not prescribe how `MAX_RADIUS` / `MIN_RADIUS` should be used; converted verbatim to `constexpr`, they would sit idle. So here a fellow `constexpr` function `clamp_radius` is added, clamping the input radius back into the `[0.1, 100]` range so that both constants genuinely take part in the computation—a `constexpr` function may also call another `constexpr` function, and with a constant-expression initialization such as `constexpr double area = circle_area(2.0);`, the entire call chain gets computed at compile time.

Compile and run:

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
请你输入所求圆的半径 : 2
半径为2的面积是: 12.5664
```

:::

### Exercise 3: Write a Function That Takes const Reference Parameters

Write a function `print_sum` that takes two `const int&` parameters and prints their sum. Then call it inside `main`. Think it over: for a small type like `int`, is there a performance difference between using `const int&` versus plain `int` as the parameter? What kind of arguments is `const T&` best suited for?

::: details Reference answer

**main.cpp**

```cpp
#include <iostream>

void print_sum(const int& a, const int& b)
{
    std::cout << a << " + " << b << " 的值是: " << a + b << std::endl;
}

int main()
{
    int a = 0;
    int b = 0;
    std::cout << "请输入a的值是 :";
    std::cin >> a;
    std::cout << "请输入b的值是 :";
    std::cin >> b;
    print_sum(a, b);
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
请输入a的值是 :1
请输入b的值是 :3
1 + 3 的值是: 4
```

For a small type like `int`, pass-by-value is usually the more appropriate choice: copying a machine-word-sized value costs very little, and the compiler can often pass it directly in a register; using `const int&` is not necessarily faster, and any real difference should be settled by measurement. `const T&` is better suited to larger, read-only objects that do not need to be copied—for example `std::string` or containers; small scalar types can generally just be passed by value.

:::
