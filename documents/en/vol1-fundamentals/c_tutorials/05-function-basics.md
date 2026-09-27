---
chapter: 1
cpp_standard:
- 11
description: Understand how C functions are declared, defined, and called; the essence
  of pass-by-value; pointer parameters; return value strategies; and recursion, building
  a solid foundation for C++ pass-by-reference and function overloading.
difficulty: beginner
order: 7
platform: host
prerequisites:
- Pointers, Arrays, const, and Null Pointers
reading_time_minutes: 10
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: Function Basics and Parameter Passing
translation:
  source: documents/vol1-fundamentals/c_tutorials/05-function-basics.md
  source_hash: 141442920fddb75b0c78242b9176e0fb9ccd7c0a472887333905b3be71ad558a
  translated_at: '2026-09-25T12:58:21+00:00'
  engine: anthropic
  token_count: 4500
---
# Function Basics and Parameter Passing

So far, every line of code we've written has been crammed into `main`. Real-world programs don't work that way—a project easily runs to tens of thousands of lines, and squeezing all of that into a single function would be next to impossible to maintain. Functions are the basic unit of modular programming in C: wrap up a piece of logic, give it a name, and call it whenever you need it.

It sounds simple, but the machinery behind functions—how arguments get in, how return values come back, how stack frames operate—is worth understanding properly. Once it clicks, you won't be confused later when you get to C++ reference passing, function overloading, and templates.

## Step 1 — Function Declaration and Definition

### Declare First, Use Later

The C compiler processes code from top to bottom. If you call a function inside `main` but that function is defined after `main`, the compiler doesn't yet know the function exists when it reaches the call site. That's why we need a **function declaration** (also called a function prototype) to tell the compiler the function's "signature" in advance—the parameter types and the return type:

```c
#include <stdio.h>

// Function declaration (prototype)—tell the compiler in advance what this function looks like
int calculate_checksum(const unsigned char* data, unsigned int length);

int main(void) {
    unsigned char buffer[] = {0x01, 0x02, 0x03, 0x04};
    int checksum = calculate_checksum(buffer, 4);
    printf("Checksum: 0x%02X\n", checksum);
    return 0;
}

// Function definition—the actual implementation of the function
int calculate_checksum(const unsigned char* data, unsigned int length) {
    int sum = 0;
    for (unsigned int i = 0; i < length; i++) {
        sum += data[i];
    }
    return sum & 0xFF;
}
```

Let's verify—compile and run:

```bash
gcc -Wall -Wextra -std=c17 checksum.c -o checksum && ./checksum
```

Output:

```text
Checksum: 0x0a
```

In real projects, function declarations usually go in header files (`.h`), and function definitions go in source files (`.c`). Any other file that needs to call the function just `#include`s the corresponding header—that's the basic pattern of modularization, and we already saw it in the post on compilation basics.

Parameter names in a function prototype can be omitted (leaving only the types), but keeping them is the better practice—they act as documentation, letting anyone reading the code see at a glance what each parameter is for.

## Step 2 — C Has Only Pass-by-Value

This is the single most important point for understanding C functions: **C has only pass-by-value**. Every argument is copied when it is passed; what the function holds inside is a copy of the original data, and changes to that copy do not affect the original.

### Only the Copy Gets Modified—The Safety of Pass-by-Value

```c
void try_modify(int x) {
    x = 100;  // modifies a copy of x
}

int main(void) {
    int value = 42;
    try_modify(value);
    printf("%d\n", value);  // still 42
    return 0;
}
```

What `try_modify` receives is a copy of `value` (named `x`), and modifying `x` has no effect on the `value` outside. It may look like the call "didn't work", but flip it around—it also means a function can't accidentally modify the caller's data. That's a safety guarantee.

### Passing Pointers—Getting Around the Pass-by-Value Limitation

But what if we really do need the function to modify the caller's variables? The answer is to pass an address (a pointer). Note that this is still pass-by-value—the only difference is that the "value" being passed is an address:

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

`swap` receives the addresses of `x` and `y` (copies of the pointer values), then reads and writes that memory directly by dereferencing with `*`. The pointer itself is a copy, but the memory it points to is the original data.

Let's verify:

```bash
gcc -Wall -Wextra -std=c17 swap_demo.c -o swap_demo && ./swap_demo
```

Output:

```text
x=20, y=10
```

Passing a large struct by value copies the entire block of data—wasting both stack space and time. Pass a pointer instead (usually a `const` pointer): copying a single address (4 or 8 bytes) is enough to give the function access to the whole struct.

## Step 3 — Return Values and Multiple Return Values

A C function can return only one value. When you need to produce multiple results, two tricks are common.

### Method 1: "Returning" Through Pointer Parameters

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

This is a very common C pattern—the values to be "returned" go out through pointer parameters, while the function's own return value is typically reserved for indicating success or failure.

### Method 2: Returning a Struct

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

Modern compilers optimize struct returns very well (return value optimization, RVO), so there is usually no extra copy overhead.

## Step 4 — Recursion: A Function Calling Itself

### What Is Recursion

A function calling itself, directly or indirectly, is recursion. The essence of recursion is decomposing a problem into smaller subproblems of the same kind. As an analogy: to count how many cards are in a stack, you count the top card (1), then recursively count the rest (N-1 cards), and the final answer is 1 + (N-1) = N.

```c
int factorial(int n) {
    if (n <= 1) {
        return 1;  // Base case—the condition that stops the recursion
    }
    return n * factorial(n - 1);  // Recursive step
}
```

The chain of recursive calls: `factorial(5)` → `5 * factorial(4)` → `5 * 4 * factorial(3)` → ... → `5 * 4 * 3 * 2 * 1 = 120`

Every recursive call allocates a new stack frame on the stack (holding local variables, parameters, and the return address), so recursion depth is limited by the stack size—which is why recursion can lead to stack overflow.

Let's verify:

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

The biggest risk of recursion is **stack overflow**. Every recursive call consumes stack space; if the recursion goes too deep (say `factorial(100000)`), the stack is exhausted and the program crashes outright. For deeply recursive scenarios, converting to an iterative loop by hand is safer.

### Tail Recursion

If the recursive call is the very last operation of the recursive function, the function fits the tail-recursive form. In theory, the compiler can optimize tail recursion into a loop, avoiding the accumulation of stack frames:

```c
int factorial_tail(int n, int accumulator) {
    if (n <= 1) return accumulator;
    return factorial_tail(n - 1, n * accumulator);
}
// Usage: factorial_tail(5, 1) → 120
```

But note: the C standard does not guarantee that the compiler performs this tail-recursion optimization. For deep recursion, manually converting to iteration is safer.

## Step 5 — Variadic Functions

Some functions take a variable number of arguments—the most classic example is `printf`. C provides the variadic-function mechanism through `<stdarg.h>`:

```c
#include <stdarg.h>
#include <stdio.h>

/// @brief Compute the average of any number of integers
/// @param count the number of integers
/// @param ... a variable number of int arguments
/// @return the average
double average(int count, ...) {
    va_list args;
    va_start(args, count);  // initialization; count is the last fixed parameter

    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, int);  // fetch the arguments one by one, as int
    }

    va_end(args);  // clean up
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

Using the variadic mechanism is a four-step affair: declare the argument list with `va_list` → initialize with `va_start` → fetch arguments one by one with `va_arg` → clean up with `va_end`.

Variadic arguments have no type checking—if you pass a `double` but fetch it with `va_arg(args, int)`, the compiler won't complain, but the value you get at runtime is wrong. There is no argument-count check either—you have to tell the function, somehow, how many arguments there are. This is the most dangerous part of C variadics.

## C++ Transition

C++ upgrades functions across the board. The most direct change is **pass-by-reference**—`void swap(int& a, int& b)` makes parameter passing both efficient and intuitive, with no manual address-taking and dereferencing.

C++ also supports **function overloading**—functions with the same name can have different parameter lists, and the compiler picks the right one automatically based on the argument types at the call site. This solves C's naming bloat with `print_int`, `print_float`, `print_string`, and friends. **Variadic templates**, introduced in C++11, are a type-safe variadic mechanism that perfectly replaces C's `va_list`.

`constexpr` functions can execute at compile time—if the arguments are compile-time constants, the result is a compile-time constant too. That is far safer than C macros.

## Exercises

### Exercise 1: Maximum of Variadic Arguments

**Difficulty: Basic** · fetch arguments one by one with va_arg

Following the pattern of this post's `average`, implement a variadic function that returns the maximum among all of its integer arguments. The first parameter `count` says how many integers follow:

```c
/// @brief Return the maximum of count integers
/// @param count the number of integer arguments that follow
/// @return the maximum; returns 0 when count is 0
int max_int(int count, ...);
```

Usage: `max_int(3, 10, 25, 7)` should return `25`.

**Challenge extension** (optional): to implement a formatted logging function like `log_message(level, format, ...)`, you need to forward the variadic arguments to the `printf` family—that is, `vprintf`/`vfprintf` (not covered in this post). Look up `vprintf` on cppreference first, then get to work.

::: details Reference solution

```c
#include <stdarg.h>

int max_int(int count, ...) {
    if (count <= 0) {
        return 0;
    }
    va_list args;
    va_start(args, count);

    int result = va_arg(args, int);
    for (int i = 1; i < count; i++) {
        int next = va_arg(args, int);
        if (next > result) {
            result = next;
        }
    }

    va_end(args);
    return result;
}
```

The structure is exactly the same as `average`; we've just replaced "sum, then divide" with "compare one by one and keep the maximum". Consider this one more pass over the `va_start` / `va_arg` / `va_end` trio.

:::

### Exercise 2: Recursion vs. Iteration—Binary Search

**Difficulty: Intermediate** · two ways to write the same algorithm

Implement binary search both recursively and iteratively, and compare the two for performance and readability:

```c
int binary_search_recursive(const int* arr, size_t len, int target);
int binary_search_iterative(const int* arr, size_t len, int target);
```

::: details Reference solution

```c
int binary_search_recursive(const int* arr, size_t len, int target) {
    if (len < 1) {
        printf("%d is not found in index\n", target);
        return -1;
    }
    size_t mid = (len - 1) / 2;

    if (arr[mid] == target) {return mid;}
    if (arr[mid] < target) {
        int res = binary_search_recursive(arr + mid + 1, len - mid - 1, target);
        return (res == -1) ? -1 : (int)(res + mid + 1);
    }
    if (arr[mid] > target) {return binary_search_recursive(arr, mid , target);}
    return -1;
}

int binary_search_iterative(const int* arr, size_t len, int target) {
    size_t lo = 0, hi = len;            // search interval [lo, hi), half-open
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] == target) {
            return mid;
        }
        if (arr[mid] < target) {
            lo = mid + 1;               // search the right half; lo only grows, no underflow
        } else {
            hi = mid;                   // search the left half; hi converges to mid, no underflow
        }
    }
    printf("%d is not found in index\n", target);
    return -1;
}
```

:::

### Exercise 3: Multiple Return Values in Practice

**Difficulty: Basic** · carry out multiple results through pointer parameters

Implement a function that finds the maximum and the minimum of an array at the same time:

```c
/// @brief Find both the minimum and the maximum of an array
/// @param data the array
/// @param len the length of the array
/// @param min_out output pointer for the minimum
/// @param max_out output pointer for the maximum
void find_min_max(const int* data, size_t len, int* min_out, int* max_out);
```

::: details Reference solution

```c
void find_min_max(const int* data, size_t len, int* min_out, int* max_out) {
    if (data == NULL || min_out == NULL || max_out == NULL || len < 1) {
        return;
    }
    *min_out = *max_out = data[0];
    for (size_t i = 1; i < len; i++) {
        if (data[i] < *min_out) {
            *min_out = data[i];
        }
        if (data[i] > *max_out) {
            *max_out = data[i];
        }
    }
}
```

:::

## References

- [cppreference: function declaration](https://en.cppreference.com/w/c/language/function_declaration)
- [cppreference: stdarg.h](https://en.cppreference.com/w/c/variadic)
