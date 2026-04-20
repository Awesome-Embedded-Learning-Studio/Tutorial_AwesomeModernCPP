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
  translated_at: '2026-04-20T03:17:48.848581+00:00'
  engine: anthropic
  token_count: 1747
---
# Function Basics and Parameter Passing

So far, all of our code has been stuffed into the `main` function. But real-world programs don't work like that — a project can easily stretch to tens of thousands of lines, and cramming everything into one function makes it practically unmaintainable. Functions are the fundamental unit of modular programming in C: we encapsulate a piece of logic, give it a name, and call it whenever we need it.

That sounds simple enough, but the mechanisms behind functions — how parameters are passed in, how return values come back, and how stack frames operate — need to be thoroughly understood. Otherwise, we will feel lost when we later encounter C++ reference passing, function overloading, and templates.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Correctly declare, define, and call C functions
> - [ ] Understand that C only uses pass-by-value
> - [ ] Master the technique of returning multiple values via pointers
> - [ ] Understand the principles of recursion and the risk of stack overflow

## Environment Setup

We will run all of the following experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `gcc -std=c17 -Wall -Wextra -Wpedantic -O0 -g`

## Step 1 — Function Declaration and Definition

### Declare First, Use Later

The C compiler processes code from top to bottom. If we call a function inside `main`, but that function is defined after `main`, the compiler doesn't know the function exists when it reaches the call site. Therefore, we need a **function declaration** (also called a function prototype) to tell the compiler the function's "signature" in advance — its parameter types and return type:

```c
#include <stdio.h>

// 函数声明（函数原型）
int add(int a, int b);

int main(void) {
    int result = add(3, 5);
    printf("3 + 5 = %d\n", result);
    return 0;
}

// 函数定义
int add(int a, int b) {
    return a + b;
}
```

Let's verify this by compiling and running:

```bash
$ gcc -std=c17 -Wall -Wextra -Wpedantic -O0 -g -o add add.c
$ ./add
3 + 5 = 8
```

Result:

```text
3 + 5 = 8
```

In real projects, function declarations are typically placed in header files (`.h`), and function definitions are placed in source files (`.c`). Other files that need to call the function simply `#include` the corresponding header — this is the basic pattern of modularization, which we already saw in the compilation fundamentals chapter.

Parameter names in function prototypes can be omitted (keeping only the types), but retaining parameter names is a better practice — it serves as documentation, letting anyone reading the code immediately understand the purpose of each parameter.

## Step 2 — C Only Uses Pass-by-Value

This is the most critical point for understanding C functions: **C only uses pass-by-value**. All parameters are copied when passed. The function receives a copy of the original data, and modifying the copy does not affect the original data.

### Copies Remain Unchanged — The Safety of Pass-by-Value

```c
#include <stdio.h>

void try_modify(int x) {
    x = 100;  // 修改的是副本
    printf("函数内 x = %d\n", x);
}

int main(void) {
    int a = 42;
    try_modify(a);
    printf("函数外 a = %d\n", a);  // a 没有被改变
    return 0;
}
```

`try_modify` receives a copy of `a` (the parameter `x`), and modifying `x` does not affect the outer `a`. This might seem like it "didn't work," but looking at it from another angle — it also means the function won't accidentally modify the caller's data. This is a form of safety protection.

### Passing Pointers — Bypassing the Limitations of Pass-by-Value

What if we genuinely need the function to modify the caller's variable? The answer is to pass the address (a pointer). Note that we are still passing by value here — it's just that the "value" is an address:

```c
#include <stdio.h>

void swap(int *pa, int *pb) {
    int temp = *pa;
    *pa = *pb;
    *pb = temp;
}

int main(void) {
    int x = 10, y = 20;
    printf("交换前: x = %d, y = %d\n", x, y);
    swap(&x, &y);
    printf("交换后: x = %d, y = %d\n", x, y);
    return 0;
}
```

`swap` receives the addresses of `x` and `y` (a value copy of the pointers), and then uses dereferencing (`*pa`) to directly read and write that memory. The pointers themselves are copies, but the memory they point to is the original data.

Let's verify this:

```bash
$ gcc -std=c17 -Wall -Wextra -Wpedantic -O0 -g -o swap swap.c
$ ./swap
交换前: x = 10, y = 20
交换后: x = 20, y = 10
```

Result:

```text
交换前: x = 10, y = 20
交换后: x = 20, y = 10
```

> ⚠️ **Pitfall Warning**
> When passing large structures by value, the entire block of data gets copied — wasting both stack space and time. We should pass a pointer (typically a `const` pointer), copying only an address (4 or 8 bytes) to give the function access to the entire structure.

## Step 3 — Return Values and Multiple Return Values

A C function can only return one value. If we need to return multiple results, there are two common techniques.

### Method 1: "Returning" via Pointer Parameters

```c
#include <stdio.h>

// 返回除法结果，通过指针返回余数
int divide(int dividend, int divisor, int *remainder) {
    if (divisor == 0) {
        return -1;  // 错误码
    }
    *remainder = dividend % divisor;
    return dividend / divisor;
}

int main(void) {
    int rem;
    int quotient = divide(17, 5, &rem);
    printf("商 = %d, 余数 = %d\n", quotient, rem);
    return 0;
}
```

This is a very common C pattern — values that need to be "returned" are passed out through pointer parameters, while the function's actual return value is typically used to indicate success or failure.

### Method 2: Returning a Structure

```c
#include <stdio.h>

typedef struct {
    int quotient;
    int remainder;
} DivResult;

DivResult divide(int dividend, int divisor) {
    DivResult result = {-1, -1};
    if (divisor != 0) {
        result.quotient = dividend / divisor;
        result.remainder = dividend % divisor;
    }
    return result;
}

int main(void) {
    DivResult r = divide(17, 5);
    printf("商 = %d, 余数 = %d\n", r.quotient, r.remainder);
    return 0;
}
```

Modern compilers have excellent optimizations for returning structures (return value optimization, RVO), and this usually does not incur extra copying overhead.

## Step 4 — Recursion: A Function Calling Itself

### What Is Recursion

When a function calls itself directly or indirectly, that is recursion. The essence of recursion is breaking a problem down into smaller subproblems of the same type. As an analogy: if you want to count how many cards are in a deck, you can count the top card (1), then recursively count the rest (N-1 cards), and the final result is 1 + (N-1) = N.

```c
#include <stdio.h>

int factorial(int n) {
    if (n <= 1) {
        return 1;  // 基准情况（递归终止条件）
    }
    return n * factorial(n - 1);  // 递归调用
}

int main(void) {
    for (int i = 1; i <= 10; i++) {
        printf("%d! = %d\n", i, factorial(i));
    }
    return 0;
}
```

Recursion call chain: `factorial(5)` → `factorial(4)` → `factorial(3)` → ... → `factorial(1)`

Each recursive call allocates a new stack frame on the stack (storing local variables, parameters, and the return address), so the recursion depth is limited by the stack size — this is why recursion can potentially lead to stack overflow.

Let's verify this:

```bash
$ gcc -std=c17 -Wall -Wextra -Wpedantic -O0 -g -o factorial factorial.c
$ ./factorial
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

Result:

```text
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

If a recursive call is the very last operation in a function, it satisfies the form of tail recursion. Theoretically, the compiler can optimize tail recursion into a loop, avoiding the accumulation of stack frames:

```c
// 尾递归版本的阶乘
int factorial_tail(int n, int accumulator) {
    if (n <= 1) {
        return accumulator;  // 直接返回，不再有后续计算
    }
    return factorial_tail(n - 1, n * accumulator);  // 尾递归调用
}

// 包装函数，提供简洁的调用接口
int factorial(int n) {
    return factorial_tail(n, 1);
}
```

However, note that the C standard does not guarantee that compilers will perform tail recursion optimization. In scenarios involving deep recursion, manually converting to iteration is safer.

## Step 5 — Variadic Functions

Some functions have a variable number of arguments — the most typical example being `printf`. C provides the mechanism for variadic functions through `stdarg.h`:

```c
#include <stdio.h>
#include <stdarg.h>

// 计算任意个整数的和
int sum(int count, ...) {
    va_list args;
    va_start(args, count);

    int total = 0;
    for (int i = 0; i < count; i++) {
        total += va_arg(args, int);
    }

    va_end(args);
    return total;
}

int main(void) {
    printf("sum(3, 10, 20, 30) = %d\n", sum(3, 10, 20, 30));
    printf("sum(5, 1, 2, 3, 4, 5) = %d\n", sum(5, 1, 2, 3, 4, 5));
    return 0;
}
```

Result:

```text
sum(3, 10, 20, 30) = 60
sum(5, 1, 2, 3, 4, 5) = 15
```

The variadic argument mechanism follows four steps: `...` declares the parameter list → `va_start` initializes → `va_arg` retrieves arguments one by one → `va_end` cleans up.

> ⚠️ **Pitfall Warning**
> Variadic arguments have no type checking — if we pass a `double` but use `va_arg` with `int` to retrieve it, the compiler won't report an error, but the value we get at runtime will be wrong. There is also no argument count checking — we must tell the function how many arguments there are through some mechanism. This is the most dangerous aspect of C variadic arguments.

## Bridging to C++

C++ makes comprehensive enhancements to functions. The most direct change is **reference passing** — `&` makes parameter passing both efficient and intuitive, eliminating the need for manual address-of and dereferencing.

C++ also supports **function overloading** — functions with the same name can have different parameter lists, and the compiler automatically selects the right one based on the argument types at the call site. This solves the naming bloat problem in C seen with `print_int`, `print_float`, `print_string`, and similar names. **Variadic templates**, introduced in C++11, are a type-safe variadic mechanism that perfectly replaces C's `stdarg.h`.

`constexpr` functions allow functions to execute at compile time — if the arguments are compile-time constants, the function's result is also a compile-time constant. This is much safer than C macros.

## Summary

Functions are the foundation of modular programming in C. Understanding the essence of pass-by-value — that all parameters are copies — is a prerequisite for mastering pointer parameters and multiple return value techniques. When we need to modify the caller's variables, we pass pointers; for large structures, we should pass `const` pointers. Recursion is elegant, but we must watch out for stack overflow. Variadic arguments provide flexibility but lack type safety.

At this point, we have mastered the basic usage of functions. The next question arises: how are variable scope and lifetime managed? What exactly does the `static` keyword do? These are the topics we will discuss in the next chapter.

## Exercises

### Exercise 1: Variadic Log Function

Implement a custom log function that supports log levels and formatted strings:

```c
#include <stdio.h>
#include <stdarg.h>

// TODO: 实现日志函数
// void log_msg(const char *level, const char *fmt, ...);

int main(void) {
    log_msg("INFO", "系统启动, 版本: %s", "1.0.0");
    log_msg("WARN", "内存使用率: %d%%", 85);
    log_msg("ERROR", "打开文件失败: %s", "config.txt");
    return 0;
}
```

### Exercise 2: Recursion vs. Iteration — Binary Search

Implement binary search using both recursion and iteration, and compare their performance and readability:

```c
#include <stdio.h>

// TODO: 递归版本
// int binary_search_recursive(const int arr[], int size, int target);

// TODO: 迭代版本
// int binary_search_iterative(const int arr[], int size, int target);

int main(void) {
    int arr[] = {2, 5, 8, 12, 16, 23, 38, 56, 72, 91};
    int size = sizeof(arr) / sizeof(arr[0]);
    int target = 23;

    printf("递归: 索引 = %d\n", binary_search_recursive(arr, size, target));
    printf("迭代: 索引 = %d\n", binary_search_iterative(arr, size, target));
    return 0;
}
```

### Exercise 3: Multiple Return Values in Practice

Implement a function that simultaneously calculates the maximum and minimum values of an array:

```c
#include <stdio.h>

// TODO: 实现函数
// void find_min_max(const int arr[], int size, int *min, int *max);

int main(void) {
    int arr[] = {34, 12, 56, 78, 5, 99, 23};
    int size = sizeof(arr) / sizeof(arr[0]);
    int min, max;

    find_min_max(arr, size, &min, &max);
    printf("最小值: %d, 最大值: %d\n", min, max);
    return 0;
}
```

## References

- [cppreference: Function declaration](https://en.cppreference.com/w/c/language/function_declaration)
- [cppreference: stdarg.h](https://en.cppreference.com/w/c/variadic)
