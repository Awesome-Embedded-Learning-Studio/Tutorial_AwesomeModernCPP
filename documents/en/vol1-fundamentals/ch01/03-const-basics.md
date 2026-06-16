---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the various uses of `const` with variables and pointers, and get
  a preliminary understanding of `constexpr` compile-time constants.
difficulty: beginner
order: 3
platform: host
prerequisites:
- 类型转换
reading_time_minutes: 14
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: A First Look at const
translation:
  source: documents/vol1-fundamentals/ch01/03-const-basics.md
  source_hash: ef0fa70e3e44914ca4ae7bf8a5dc18e4c95fa31e128a015f507df996185f3b2f
  translated_at: '2026-06-16T03:40:40.890129+00:00'
  engine: anthropic
  token_count: 2256
---
# An Introduction to `const`

When writing code, some things simply shouldn't be changed—configuration parameters shouldn't be accidentally overwritten once set, array capacities shouldn't fluctuate after declaration, and physical constants like Pi are non-negotiable. If we rely solely on "discipline" to ensure these values remain intact, we might as well be walking blindfolded at night. Sooner or later, a slip of the hand will modify a critical value, leading to hours spent debugging a mysterious bug.

C++ provides us with a safety lock: `const`. The core concept is simple—if something shouldn't change, explicitly tell the compiler so it can watch over it. Any code attempting to modify a `const` value is blocked right at the compilation stage. Killing the problem during compilation is far more reliable than discovering data corruption in production. (Rust actually flips this paradigm: unless you say a variable is mutable, it is immutable! So variables are `const` by default!)

## Locking Down Variables — Basic `const` Usage

Let's start with the simplest scenario. Suppose we have a maximum buffer capacity that should remain unchanged throughout the program's execution:

```cpp
const int MAX_BUFFER_SIZE = 1024;
```

Once we add `const`, this variable becomes "read-only"—we must provide an initial value at declaration, and any subsequent attempt to modify it will be rejected by the compiler. Let's try it:

```cpp
MAX_BUFFER_SIZE = 2048; // Error!
```

The compiler will give a very clear error message:

```text
error: assignment of read-only variable 'MAX_BUFFER_SIZE'
```

This is the core value of `const`—it elevates "I shouldn't change this" from a gentleman's agreement to a compiler-enforced rule. You might ask, isn't this just using the compiler as a bodyguard? Exactly, and this bodyguard never falls asleep on the job.

### `const` vs `#define`: What's the Difference?

If you've used C, you might say, "I can do this with `#define`." True, the effect looks similar, but there are key differences.

First, `const` variables have explicit types. The `int` in `const int` tells the compiler this is an integer. If you accidentally assign it to a `float`, the compiler can perform type checking or issue a warning. `#define` is just simple text replacement; the preprocessor doesn't care about types—it dutifully replaces all `MAX_BUFFER_SIZE` with `1024`, regardless of whether 1024 is an integer or a float.

Second, `const` variables follow normal scoping rules. A `const` variable declared inside a function is visible only within that function, while a global `const` variable has internal linkage by default (meaning other `.cpp` files can't see it). `#define` takes effect from the point of definition to the end of the file with no scope restrictions—this easily triggers naming conflicts in large projects.

Finally, when debugging, a `const` variable is just a normal variable; you can see its name and value in the debugger. A `#define` macro is replaced during preprocessing, so the debugger only sees a bare number `1024`, leaving you clueless about where it came from.

Our conclusion: in C++, prefer `const` or `constexpr` (discussed later) to define constants, leaving `#define` for scenarios that truly require conditional compilation.

Regarding naming conventions, constants in this tutorial use the `kCamelCase` style, like `kMaxBufferSize`, `kPi`, `kTimeoutMs`. The `k` prefix is a common convention in the C++ community to signal that a value is constant and shouldn't be modified.

## `const` and Pointers — The Most Confusing Part

Using `const` to modify a simple variable is straightforward, but when `const` meets pointers, things get interesting. Many folks get confused here—I certainly struggled with this when starting out. Don't worry, let's break it down step by step.

The core question is: does `const` modify the pointer itself, or the data the pointer points to? The answer depends on where `const` appears. C++ has three `const` and pointer combinations. Let's look at them one by one.

### Pointer to Constant: `const int* p`

```cpp
int a = 10;
const int* p = &a; // p points to a, but the data is read-only via p
```

Here, `const` modifies `int`, meaning modifying the data pointed to by `p` is forbidden. However, the pointer `p` itself can change—it can point to a different address. Think of it as "this pointer is well-behaved; it promises not to modify the target data through itself."

```cpp
*p = 20; // Error: cannot modify data through p
p = nullptr; // OK: can change where p points
```

Note a detail: although you can't modify `a`'s value through `p`, `a` itself is not `const`. Modifying `a` directly is perfectly legal—`const` just means "I won't modify it through this pointer," not that the target data is truly immutable.

### Constant Pointer: `int* const p`

```cpp
int a = 10;
int* const p = &a; // p is constant, but the data is modifiable
```

This time, `const` modifies the pointer variable `p` itself. Once initialized, the pointer is locked to that address and cannot point elsewhere. However, modifying the target data through `p` is fully allowed.

```cpp
*p = 20; // OK: can modify data
p = nullptr; // Error: cannot change where p points
```

Think of this as a "stubborn pointer"—it fixates on an address and won't budge, but it can change the contents at that address freely.

### Both `const`: `const int* const p`

```cpp
int a = 10;
const int* const p = &a; // Neither p nor *p can be modified
```

This combines the two constraints: the pointer itself cannot change where it points, and the data cannot be modified through the pointer. This is quite common in function parameters—when passing a pointer to a function, if you don't want the function to change the pointer's target or the data itself, you write it this way.

### Read Right-to-Left — A Practical Reading Trick

Many find these three combinations hard to remember. Here is a classic reading method: **read the declaration from right to left**. Let's take `const int* const p` as an example:

- Start with the variable name `p`, read left
- `const` → p is a constant
- `*` → pointer
- `int` → to int type
- `const` → this int is constant

Put together: `p` is a constant pointer to a constant int.

Look at `const int* p` again: `p` is a pointer (`*`) to a constant int (`const int`)—data immutable, pointer mutable.

`int* const p`: `p` is a constant (`const`) pointer (`*`) to int—pointer immutable, data mutable.

Practice with a few more examples, and you'll build intuition quickly.

> **Pitfall Warning**: Interviews and exams love to test the differences between these three declarations. If you can't tell them apart, don't guess—use the right-to-left method and break it down step by step; it's much more reliable than rote memory. Also, `const int* p` and `int const* p` are completely equivalent; `const` can go before or after `int`. But `int* const p` is different; `const` is to the right of `*`, modifying the pointer. This positional difference is key.

The pitfalls don't stop there. Many beginners think `const int* p` means `a` itself becomes constant—it doesn't. `a` is still a normal variable; you can modify `a` directly. `const` means "I won't modify through this pointer," an access constraint, not a constraint on the target data itself.

## `const` and References

Done with pointers, let's look at references. `const` with references is much simpler than with pointers, because references themselves cannot be rebound—they are bound to a variable from birth. So there is only one `const` and reference combination:

```cpp
int a = 10;
const int& ref = a; // ref is a read-only alias for a
```

`ref` is an alias for `a`, but you cannot modify `a`'s value through `ref`. Similar to `const int* p`, this just means "I won't modify through `ref`"; `a` itself can still be freely modified.

This "const reference" has an extremely important use in practical development—function parameters. Imagine a function that needs to receive a `std::string` parameter:

```cpp
void printString(std::string str) {
    // ...
}
```

Every time `printString` is called, a copy of the string occurs. If the string is long, or the function is called frequently, this copy overhead is non-negligible. Changing it to a `const` reference solves this:

```cpp
void printString(const std::string& str) {
    // ...
}
```

`const std::string&` means: receive a reference (no copy), but promise not to modify it. This avoids copy overhead while guaranteeing safety to the caller. This `const T&` parameter pattern appears extremely frequently in C++; we will encounter it repeatedly in later chapters. For now, just be aware of it.

## `constexpr` — Let the Compiler Calculate for You

So far, our `const` just means "this value won't change at runtime." But some constants have values determined at compile time—like `3.14 * 2` definitely equals `6.28`, no need to wait for the program to run. C++11 introduced `constexpr` to explicitly tell the compiler: "You can calculate this value during compilation."

```cpp
constexpr double PI = 3.14159;
constexpr double DIAMETER = 2.0 * PI; // Calculated at compile time
```

The relationship between `constexpr` and `const` can be summarized in one sentence: `constexpr` implies `const` (compile-time constants certainly can't change), but `const` doesn't imply `constexpr` (read-only values determined at runtime also count as `const`). For example:

```cpp
int runtimeInput;
std::cin >> runtimeInput;
const int c = runtimeInput; // OK: const, but not constexpr
```

`constexpr` is more powerful because it can be used on functions. A `constexpr` function means: if the arguments passed are compile-time determinable, the return value can also be calculated at compile time:

```cpp
constexpr int square(int x) {
    return x * x;
}

constexpr int result = square(5); // Calculated at compile time, result is 25
```

Values calculated at compile time have a major benefit: they can be used where constant expressions are required, like array sizes:

```cpp
int arr[square(5)]; // OK: square(5) is a constant expression
```

If `square` were just a normal `const` function, this line might fail on some compilers (depending on whether the variable is treated as a constant expression). Using `constexpr` leaves no ambiguity.

Here we just touch briefly on `constexpr`. It is one of the most important features of modern C++—C++14 allowed more complex logic in functions, C++17 further relaxed restrictions, and C++20 introduced `consteval` (must execute at compile time) and `constinit`. Later, we will have a dedicated chapter to dive deep into compile-time computation. For now, just know: if your constant value can be determined at compile time, prefer `constexpr`.

> **Pitfall Warning**: `constexpr` functions don't guarantee execution at compile time. The compiler forces compile-time calculation only when a "compile-time constant" is needed (like array size, template parameters). Otherwise, the compiler might choose to calculate at compile time or runtime—depending on optimization strategy and function complexity. If you need to force compile-time execution, C++20's `consteval` is the correct choice.

## Comprehensive Practice — const_demo.cpp

Theory is shallow. Let's string together all the `const` usage discussed above into a complete example program. This program won't have complex logic, but it will cover every `const` combination and verify the compiler's behavior.

```cpp
#include <iostream>
#include <string>

// 1. Basic const variable
const int kMaxSize = 100;

// 2. constexpr variable
constexpr int kSquare(int x) {
    return x * x;
}

int main() {
    // 3. const pointer (pointer cannot change, data can)
    int a = 10;
    int* const p1 = &a;
    *p1 = 20; // OK
    // p1 = nullptr; // Error: assignment of read-only variable 'p1'

    // 4. Pointer to const (data cannot change, pointer can)
    const int* p2 = &a;
    // *p2 = 30; // Error: assignment of read-only location '* p2'
    p2 = nullptr; // OK

    // 5. Pointer to const pointer (both cannot change)
    const int* const p3 = &a;
    // *p3 = 40; // Error
    // p3 = nullptr; // Error

    // 6. const reference
    const int& ref = a;
    // ref = 50; // Error: assignment of read-only reference 'ref'

    // 7. constexpr function usage
    constexpr int size = kSquare(5);
    int arr[size]; // OK: array size is a constant expression

    std::cout << "a = " << a << std::endl;
    std::cout << "Array size: " << size << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++20 const_demo.cpp -o const_demo
./const_demo
```

Expected output:

```text
a = 20
Array size: 25
```

You can uncomment the "compilation error" lines one by one to see what error messages the compiler produces. Experiencing how the compiler blocks these operations firsthand is much more memorable than just reading text.

## Run Online

Run `const_demo.cpp` online and observe the actual output of various `const` usages:

<OnlineCompilerDemo
  title="const 初探：变量、指针、引用与 constexpr"
  source-path="code/examples/vol1/04_const_demo.cpp"
  description="在线运行并观察 const 指针、const 引用和 constexpr 的实际行为。"
  allow-run
/>

## Try It Yourself

Done with theory, now it's your turn. The following three exercises help verify your understanding of `const`. I suggest writing, compiling, and running each one completely.

### Exercise 1: Declare `const` Pointers and Predict Behavior

Write the following declarations, then for each pointer try (1) modifying the data the pointer points to, (2) modifying the pointer's target itself. Before compiling, predict which operations the compiler will reject, then verify your prediction.

- `const int* p1`
- `int* const p2`
- `const int* const p3`

### Exercise 2: Transform `#define` into `constexpr`

Here is a snippet of C-style code using `#define`. Replace all macro constants with `constexpr` variables, and write a `constexpr` function `calculateArea` to calculate the area of a circle.

```cpp
#include <iostream>
#include <cmath>

#define PI 3.14159
#define MAX_RADIUS 100

int main() {
    double r = 5.0;
    double area = PI * r * r;
    std::cout << "Area: " << area << std::endl;
    return 0;
}
```

### Exercise 3: Write a Function Using `const` Reference Parameters

Write a function `printSum` that accepts two `int` parameters and outputs their sum. Then call it in `main`. Think about it: for a small type like `int`, is there a performance difference between using `const int&` and passing `int` directly? What types of parameters are best suited for `const T&` passing?

## Summary

In this chapter, we focused on the `const` keyword and reviewed the most common "read-only" mechanisms in C++. `const` variables must be initialized at declaration and cannot be modified afterward; they are safer, more type-safe, and easier to debug than `#define`. The combination of `const` and pointers is the most error-prone area—`const int*` is a "pointer to constant" (data immutable, pointer mutable), `int* const` is a "constant pointer" (pointer immutable, data mutable), and reading right-to-left is an effective way to distinguish them. `const` references are extremely common in function parameters; the `const T&` pattern avoids copying while ensuring safety. `constexpr` is a stricter constant—it requires the value to be calculable at compile time, making programs faster and usable in scenarios requiring constant expressions like array sizes.

In the next chapter, we will enter the world of value categories—what exactly are lvalues and rvalues, and why does move semantics make programs faster? These concepts sound abstract, but understanding `const` first will reveal many shared ideas.
