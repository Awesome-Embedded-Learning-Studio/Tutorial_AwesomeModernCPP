---
chapter: 1
cpp_standard:
- 11
description: Understand the declaration, definition, and calling mechanisms of C functions,
  the essence of pass-by-value, pointer parameters, return value strategies, and recursion
  principles, laying a solid foundation for C++ pass-by-reference and function overloading.
difficulty: beginner
order: 7
platform: host
prerequisites:
- 指针与数组、const 和空指针
reading_time_minutes: 12
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: Function Basics and Parameter Passing
translation:
  source: documents/vol1-fundamentals/c_tutorials/05-function-basics.md
  source_hash: 52ac72efa9b0b73c5e1deb359525fa5ee279170f394f95f639532f4fcc3e02b5
  translated_at: '2026-05-26T10:28:11.498698+00:00'
  engine: anthropic
  token_count: 1747
---
# Function Basics and Parameter Passing

So far, all of our code has been stuffed into the `main` function. But real-world programs don't work like that — a project can easily reach tens of thousands of lines of code, and cramming everything into a single function makes it practically unmaintainable. Functions are the basic unit of modular programming in C: we encapsulate a piece of logic, give it a name, and call it whenever we need it.

That sounds simple enough, but the mechanisms behind functions — how parameters are passed in, how return values come back, and how stack frames operate — must be thoroughly understood. Otherwise, we will feel lost when we later learn about C++ reference passing, function overloading, and templates.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Correctly declare, define, and call C functions
> - [ ] Understand that C only uses pass-by-value
> - [ ] Master the technique of returning multiple values via pointers
> - [ ] Understand the principles of recursion and the risk of stack overflow

## Environment Setup

We will conduct all of the following experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c17`

## Step 1 — Function Declaration and Definition

### Declare First, Use Later

The C compiler processes code from top to bottom. If we call a function inside `main`, but that function is defined after `main`, the compiler doesn't know the function exists when it reaches the call site. Therefore, we need a **function declaration** (also known as a function prototype) to tell the compiler the function's "signature" in advance — the parameter types and return type:

```c
#include <stdio.h>

// 函数声明（原型）——提前告诉编译器这个函数长什么样
int calculate_checksum(const unsigned char* data, unsigned int length);

int main(void) {
    unsigned char buffer[] = {0x01, 0x02, 0x03, 0x04};
    int checksum = calculate_checksum(buffer, 4);
    printf("Checksum: 0x%02X\n", checksum);
    return 0;
}

// 函数定义——函数真正的实现
int calculate_checksum(const unsigned char* data, unsigned int length) {
    int sum = 0;
    for (unsigned int i = 0; i < length; i++) {
        sum += data[i];
    }
    return sum & 0xFF;
}
```

Let's verify this by compiling and running:

```bash
gcc -Wall -Wextra -std=c17 checksum.c -o checksum && ./checksum
```

Output:

```text
Checksum: 0x0a
```

In real-world projects, function declarations are typically placed in header files (`.h`), and function definitions are placed in source files (`.c`). Other files that need to call the function simply `#include` the corresponding header — this is the basic pattern of modularization, which we already saw in the compilation basics chapter.

Parameter names in function prototypes can be omitted (keeping only the types), but retaining the names is a better practice — it serves as documentation, letting anyone reading the code immediately understand the purpose of each parameter.

## Step 2 — C Only Uses Pass-by-Value

This is the most critical point for understanding C functions: **C only uses pass-by-value**. All parameters are copied when passed. The function receives a copy of the original data, and modifying the copy does not affect the original data.

### The Copy Remains Unchanged — The Safety of Pass-by-Value

```c
void try_modify(int x) {
    x = 100;  // 修改的是 x 的副本
}

int main(void) {
    int value = 42;
    try_modify(value);
    printf("%d\n", value);  // 仍然是 42
    return 0;
}
```

`try_modify` receives a copy of `value` (`x`). Modifying `x` does not affect the outer `value`. This might look like it "didn't work," but from another perspective, it also means the function won't accidentally modify the caller's data — this is a form of safety protection.

### Passing Pointers — Bypassing the Limitations of Pass-by-Value

What if we actually need the function to modify the caller's variable? The answer is to pass the address (a pointer). Note that we are still passing by value here — it's just that the "value" is an address:

```c
void swap(int* a, int* b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

int main(void) {
    int x = 10, y = 20;
    swap(&x, &y);
    printf("x=%d, y=%d\n", x, y);
    return 0;
}
```

`swap` receives the addresses of `x` and `y` (a value copy of the pointers), and then directly reads and writes to that memory through dereferencing `*`. The pointer itself is copied, but the memory it points to is the original data.

Let's verify this:

```bash
gcc -Wall -Wextra -std=c17 swap_demo.c -o swap_demo && ./swap_demo
```

Output:

```text
x=20, y=10
```

> ⚠️ **Pitfall Warning**
> When passing large structures by value, the entire block of data gets copied — wasting both stack space and time. We should pass a pointer (typically a `const` pointer), copying only an address (4 or 8 bytes) to allow the function to access the entire structure.

## Step 3 — Return Values and Multiple Return Values

A C function can only return one value. If we need to return multiple results, there are two common techniques.

### Method 1: "Returning" via Pointer Parameters

```c
void divmod(int dividend, int divisor, int* quotient, int* remainder) {
    *quotient = dividend / divisor;
    *remainder = dividend % divisor;
}

int main(void) {
    int q, r;
    divmod(17, 5, &q, &r);
    printf("17 / 5 = %d 余 %d\n", q, r);
    return 0;
}
```

This is a very common C pattern — values that need to be "returned" are passed out through pointer parameters, while the function's actual return value is typically used to indicate success or failure.

### Method 2: Returning a Structure

```c
typedef struct {
    int quotient;
    int remainder;
} DivResult;

DivResult div_with_remainder(int dividend, int divisor) {
    DivResult result;
    result.quotient = dividend / divisor;
    result.remainder = dividend % divisor;
    return result;
}
```

Modern compilers have excellent optimizations for returning structures (return value optimization, RVO), so this usually doesn't incur extra copy overhead.

## Step 4 — Recursion: A Function Calling Itself

### What Is Recursion

When a function calls itself directly or indirectly, that is recursion. The essence of recursion is breaking a problem down into smaller subproblems of the same type. As an analogy: if we want to count how many cards are in a deck, we can count the top card (1), then recursively count the rest (N-1 cards), and the final result is 1 + (N-1) = N.

```c
int factorial(int n) {
    if (n <= 1) {
        return 1;  // 基准情况——停止递归的条件
    }
    return n * factorial(n - 1);  // 递归步骤
}
```

Recursion call chain: `factorial(5)` → `5 * factorial(4)` → `5 * 4 * factorial(3)` → ... → `5 * 4 * 3 * 2 * 1 = 120`

Each recursive call allocates a new stack frame on the stack (storing local variables, parameters, and the return address), so the recursion depth is limited by the stack size — this is why recursion can potentially lead to stack overflow.

Let's verify this:

```c
#include <stdio.h>

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main(void) {
    for (int i = 0; i <= 10; i++) {
        printf("%d! = %d\n", i, factorial(i));
    }
    return 0;
}
```

Output:

```text
0! = 1
1! = 1
2! = 2
3! = 6
4! = 24
5! = 120
6! = 720
7! = 5040
8! = 40320
9! = 362880
10! = 3628800
```

> ⚠️ **Pitfall Warning**
> The biggest risk with recursion is **stack overflow**. Each recursive call consumes stack space. If the recursion depth is too large (for example, `factorial(100000)`), the stack space is exhausted and the program crashes immediately. For scenarios involving deep recursion, manually converting to an iterative loop is safer.

### Tail Recursion

If the recursive call is the very last operation in a recursive function, it satisfies the form of tail recursion. Theoretically, the compiler can optimize tail recursion into a loop, avoiding the accumulation of stack frames:

```c
int factorial_tail(int n, int accumulator) {
    if (n <= 1) return accumulator;
    return factorial_tail(n - 1, n * accumulator);
}
// 使用：factorial_tail(5, 1) → 120
```

However, note that the C standard does not guarantee that the compiler will perform tail call optimization. In scenarios involving deep recursion, manually converting to an iteration is safer.

## Step 5 — Variadic Functions

Some functions have a variable number of arguments — the most typical example is `printf`. C provides the mechanism for variadic functions through `<stdarg.h>`:

```c
#include <stdarg.h>
#include <stdio.h>

/// @brief 计算任意数量整数的平均值
/// @param count 整数的个数
/// @param ... 可变数量的 int 参数
/// @return 平均值
double average(int count, ...) {
    va_list args;
    va_start(args, count);  // 初始化，count 是最后一个固定参数

    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, int);  // 逐个取出 int 类型的参数
    }

    va_end(args);  // 清理
    return sum / count;
}

int main(void) {
    printf("Avg: %.2f\n", average(3, 10, 20, 30));
    printf("Avg: %.2f\n", average(5, 1, 2, 3, 4, 5));
    return 0;
}
```

Output:

```text
Avg: 20.00
Avg: 3.00
```

The usage of the variadic argument mechanism follows four steps: `va_list` to declare the argument list → `va_start` to initialize → `va_arg` to retrieve arguments one by one → `va_end` to clean up.

> ⚠️ **Pitfall Warning**
> Variadic arguments have no type checking — if we pass a `double` but retrieve it with `va_arg(args, int)`, the compiler won't report an error, but the value retrieved at runtime will be wrong. There is also no argument count checking — we must tell the function how many arguments there are through some mechanism. This is the most dangerous aspect of C variadic arguments.

## Bridging to C++

C++ makes comprehensive enhancements to functions. The most direct change is **reference passing** — `void swap(int& a, int& b)` makes parameter passing both efficient and intuitive, without the need for manual address-of and dereferencing.

C++ also supports **function overloading** — functions with the same name can have different parameter lists, and the compiler automatically selects the correct one based on the argument types at the call site. This solves the naming bloat problem in C, where we see things like `print_int`, `print_float`, and `print_string`. **Variadic templates**, introduced in C++11, provide a type-safe variadic mechanism that perfectly replaces C's `va_list`.

The `constexpr` function allows functions to execute at compile time — if the arguments are compile-time constants, the function's result is also a compile-time constant. This is much safer than C macros.

## Summary

Functions are the foundation of modular programming in C. Understanding the essence of pass-by-value — that all parameters are copies — is a prerequisite for mastering pointer parameters and multiple return value techniques. When we need to modify the caller's variables, we pass pointers; for large structures, we should pass `const` pointers. Recursion is elegant, but we must watch out for stack overflow. Variadic arguments provide flexibility but lack type safety.

At this point, we have mastered the basic usage of functions. The next question arises — how are variable scope and lifetime managed? What is the `static` keyword actually for? These are the topics we will discuss in the next chapter.

## Exercises

### Exercise 1: Variadic Log Function

Implement a custom log function that supports log levels and formatted strings:

```c
typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } LogLevel;

/// @brief 带级别的日志输出
/// @param level 日志级别
/// @param format 格式化字符串
void log_message(LogLevel level, const char* format, ...);
```

### Exercise 2: Recursion vs. Iteration — Binary Search

Implement binary search using both recursion and iteration, and compare their performance and readability:

```c
int binary_search_recursive(const int* arr, size_t len, int target);
int binary_search_iterative(const int* arr, size_t len, int target);
```

### Exercise 3: Multiple Return Values in Practice

Implement a function that simultaneously calculates the maximum and minimum values of an array:

```c
/// @brief 同时找出数组的最大值和最小值
/// @param data 数组
/// @param len 数组长度
/// @param min_out 最小值输出指针
/// @param max_out 最大值输出指针
void find_min_max(const int* data, size_t len, int* min_out, int* max_out);
```

## References

- [cppreference: Function declaration](https://en.cppreference.com/w/c/language/function_declaration)
- [cppreference: stdarg.h](https://en.cppreference.com/w/c/variadic)
