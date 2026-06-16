---
chapter: 1
cpp_standard:
- 11
description: Understand the C function declaration, definition, and calling mechanisms,
  the essence of pass-by-value, pointer parameters, return value strategies, and recursion
  principles, to build a solid foundation for C++ pass-by-reference and function overloading.
difficulty: beginner
order: 7
platform: host
prerequisites:
- 指针与数组、const 和空指针
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
  source_hash: 04657bb27248ca746408f203ef4731211b9cb85ffc225e70cc702f66d2cd6b8a
  translated_at: '2026-06-16T03:33:53.249516+00:00'
  engine: anthropic
  token_count: 1747
---
# Function Basics and Parameter Passing

Up to this point, we have crammed all our code into the `main` function. However, real-world programs do not work this way. A project often spans tens of thousands of lines of code. If we squeeze everything into a single function, it becomes unmaintainable. Functions are the basic unit of modular programming in C: we encapsulate a block of logic, give it a name, and call it whenever needed.

This sounds simple, but to truly master functions, we need to understand the mechanisms behind them: how parameters are passed in, how return values come back, and how stack frames operate. Only with this solid foundation can we avoid confusion when we later tackle C++ reference passing, function overloading, and templates.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Correctly declare, define, and call C functions
> - [ ] Understand that C only supports pass-by-value
> - [ ] Master techniques for achieving multiple return values via pointers
> - [ ] Understand the principles of recursion and the risks of stack overflow

## Environment Setup

We will conduct all subsequent experiments in the following environment:

- **Platform:** Linux x86_64 (WSL2 is also acceptable)
- **Compiler:** GCC 13+ or Clang 17+
- **Compiler Flags:** `-std=c17 -Wall -Wextra`

## Step 1 — Function Declaration and Definition

### Declare First, Use Later

The C compiler processes code from top to bottom. If you call a function inside `main`, but that function is defined after `main`, the compiler does not know the function exists when it encounters the call point. Therefore, we need a **function declaration** (also known as a function prototype) to tell the compiler the function's "signature" in advance—specifically, the parameter types and return type:

```c
#include <stdio.h>

// Function Prototype: Tells the compiler that a function named 'add' exists elsewhere.
// It takes two integers and returns an integer.
int add(int a, int b);

int main() {
    int result = add(5, 3);
    printf("5 + 3 = %d\n", result);
    return 0;
}

// Function Definition: The actual implementation of the function.
int add(int a, int b) {
    return a + b;
}
```

Let's verify this by compiling and running:

```bash
gcc -std=c17 -Wall -Wextra main.c -o main
./main
```

**Output:**

```text
5 + 3 = 8
```

In real-world projects, function declarations are usually placed in header files (`.h`), while function definitions are placed in source files (`.c`). Other files that need to call the function simply `#include` the corresponding header file. This is the basic pattern of modularization, which we saw in the compilation basics chapter.

Parameter names in function prototypes can be omitted (keeping only the types), but retaining them is better practice. It acts as documentation, allowing anyone reading the code to immediately understand the purpose of each parameter.

## Step 2 — C Only Supports Pass-by-Value

This is the most critical point to understanding C functions: **C only supports pass-by-value**. All parameters are copied when passed. The function receives a copy of the original data, and modifications to that copy do not affect the original data.

### The Copy Remains Unchanged — The Safety of Pass-by-Value

```c
#include <stdio.h>

void try_to_modify(int x) {
    x = 100; // Modifies the local copy 'x'
    printf("Inside function: x = %d\n", x);
}

int main() {
    int num = 10;
    try_to_modify(num);
    printf("Outside function: num = %d\n", num);
    return 0;
}
```

`try_to_modify` receives a copy of `num` (let's call it `x`). Modifying `x` does not affect the external `num`. While this might look like it "didn't work," look at it from another perspective: it means the function cannot accidentally modify the caller's data. This is a form of safety protection.

### Passing Pointers — Bypassing the Limitations of Pass-by-Value

What if we actually need the function to modify the caller's variable? The answer is to pass the address (a pointer). Note that we are still technically passing by value—it's just that the "value" being passed is an address:

```c
#include <stdio.h>

// Receives addresses of two integers
void swap(int *a, int *b) {
    int temp = *a; // Dereference to read value
    *a = *b;       // Dereference to write value
    *b = temp;
}

int main() {
    int x = 10;
    int y = 20;
    printf("Before swap: x = %d, y = %d\n", x, y);

    swap(&x, &y); // Pass the addresses of x and y

    printf("After swap: x = %d, y = %d\n", x, y);
    return 0;
}
```

`swap` receives the addresses of `x` and `y` (a copy of the pointer value), and then reads and writes to that memory location directly via dereferencing (`*a`). The pointer itself is a copy, but the memory it points to is the original data.

Let's verify this:

```bash
gcc -std=c17 -Wall -Wextra main.c -o main
./main
```

**Output:**

```text
Before swap: x = 10, y = 20
After swap: x = 20, y = 10
```

> ⚠️ **Warning**
> When passing large structures by value, the entire block of data is copied. This wastes both stack space and time. You should pass a pointer (usually a `const` pointer) instead. This copies only an address (4 or 8 bytes), allowing the function to access the entire structure efficiently.

## Step 3 — Return Values and Multiple Return Values

A C function can only return one value. If we need to return multiple results, there are two common techniques.

### Method 1: "Returning" via Pointer Parameters

```c
#include <stdio.h>
#include <stdbool.h>

// Returns success/failure status, actual results are written via pointers
bool divide(int a, int b, int *quotient, int *remainder) {
    if (b == 0) {
        return false;
    }
    *quotient = a / b;
    *remainder = a % b;
    return true;
}

int main() {
    int q, r;
    if (divide(10, 3, &q, &r)) {
        printf("Quotient: %d, Remainder: %d\n", q, r);
    } else {
        printf("Error: Division by zero\n");
    }
    return 0;
}
```

This is a very common C language pattern. Values that need to be "returned" are passed out via pointer parameters, while the function's actual return value is typically used to indicate success or failure.

### Method 2: Returning a Structure

```c
#include <stdio.h>

typedef struct {
    int quotient;
    int remainder;
} DivResult;

DivResult divide(int a, int b) {
    DivResult res = {0, 0};
    if (b != 0) {
        res.quotient = a / b;
        res.remainder = a % b;
    }
    return res;
}

int main() {
    DivResult res = divide(10, 3);
    printf("Quotient: %d, Remainder: %d\n", res.quotient, res.remainder);
    return 0;
}
```

Modern compilers have excellent optimizations for returning structures (Return Value Optimization, RVO), so this usually does not incur extra copying overhead.

## Step 4 — Recursion: A Function Calling Itself

### What is Recursion?

A function that calls itself, either directly or indirectly, is recursion. The essence of recursion is to break a problem down into smaller, similar sub-problems. Think of it this way: if you want to count a stack of cards, you count the top one (1), then recursively count the rest (N-1 cards), and finally the result is 1 + (N-1) = N.

```c
#include <stdio.h>

int factorial(int n) {
    if (n <= 1) {
        return 1; // Base case: stop recursion
    }
    return n * factorial(n - 1); // Recursive step
}

int main() {
    int n = 5;
    printf("%d! = %d\n", n, factorial(n));
    return 0;
}
```

**Recursion Call Chain:** `factorial(5)` → `factorial(4)` → `factorial(3)` → ... → `factorial(1)`

Each recursive call allocates a new stack frame on the stack (to store local variables, parameters, and the return address). Therefore, recursion depth is limited by the stack size. This is why recursion can potentially lead to stack overflow.

Let's verify this:

```bash
gcc -std=c17 -Wall -Wextra main.c -o main
./main
```

**Output:**

```text
5! = 120
```

> ⚠️ **Warning**
> The biggest risk with recursion is **stack overflow**. Every recursive call consumes stack space. If the recursion depth is too large (e.g., `factorial(100000)`), the stack space will be exhausted and the program will crash immediately. For scenarios involving deep recursion, manually converting it to an iterative loop is safer.

### Tail Recursion

If the recursive call is the very last operation in a recursive function, it satisfies the form of tail recursion. Theoretically, the compiler can optimize tail recursion into a loop, avoiding the accumulation of stack frames:

```c
#include <stdio.h>

// Tail recursive version
int factorial_tail(int n, int accumulator) {
    if (n <= 1) {
        return accumulator;
    }
    return factorial_tail(n - 1, n * accumulator);
}

int main() {
    printf("5! = %d\n", factorial_tail(5, 1));
    return 0;
}
```

However, note that the C standard does not guarantee that the compiler will perform tail recursion optimization. In deep recursion scenarios, manually converting to iteration is still safer.

## Step 5 — Variadic Functions

Some functions accept a variable number of arguments—the most typical example is `printf`. C provides a mechanism for variadic functions via `<stdarg.h>`:

```c
#include <stdio.h>
#include <stdarg.h>

int sum_all(int count, ...) {
    va_list args;
    va_start(args, count); // Initialize args list

    int sum = 0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, int); // Get next argument
    }

    va_end(args); // Clean up
    return sum;
}

int main() {
    printf("Sum of 1, 2, 3: %d\n", sum_all(3, 1, 2, 3));
    printf("Sum of 5, 10, 15, 20: %d\n", sum_all(4, 5, 10, 15, 20));
    return 0;
}
```

**Output:**

```text
Sum of 1, 2, 3: 6
Sum of 5, 10, 15, 20: 50
```

The usage of the variadic mechanism involves four steps: declare the list with `va_list` → initialize with `va_start` → fetch arguments one by one with `va_arg` → clean up with `va_end`.

> ⚠️ **Warning**
> Variadic arguments have no type checking. If you pass a `double` but use `va_arg` to retrieve an `int`, the compiler will not report an error, but the value retrieved at runtime will be wrong. There is also no count checking—you must inform the function of the number of arguments through some other means (like the `count` parameter above). This is the most dangerous aspect of C variadic functions.

## Bridging to C++

C++ makes comprehensive enhancements to functions. The most direct change is **reference passing**—`T&` makes parameter passing both efficient and intuitive, eliminating the need for manual address-taking and dereferencing.

C++ also supports **function overloading**. Functions with the same name can have different parameter lists, and the compiler automatically selects the correct one based on the argument types. This solves the naming bloat problem seen in C with functions like `add_int`, `add_float`, `add_double`. **Variadic templates** (introduced in C++11) provide a type-safe mechanism for variadic arguments, perfectly replacing C's `<stdarg.h>`.

The `constexpr` function allows functions to execute at compile time. If the arguments are compile-time constants, the function result is also a compile-time constant. This is much safer than C macros.

## Summary

Functions are the foundation of modular programming in C. Understanding the essence of pass-by-value—that all parameters are copies—is the prerequisite for mastering pointer parameters and techniques for multiple return values. If you need to modify the caller's variable, pass a pointer. For large structures, pass a `const` pointer. While recursion is elegant, be wary of stack overflow. Variadic functions provide flexibility but lack type safety.

At this point, we have mastered the basic usage of functions. The next question arises: how are variable scope and lifetime managed? What is the actual use of the `static` keyword? These are the topics we will discuss in the next article.

## Exercises

### Exercise 1: Variadic Log Function

Implement a custom log function that supports log levels and formatted strings:

```c
#include <stdio.h>
#include <stdarg.h>

void log_message(const char *level, const char *fmt, ...) {
    // TODO: Print timestamp and log level
    // TODO: Use va_list to handle formatted string
    printf("[%s] ", level);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

int main() {
    log_message("INFO", "System started with code %d", 200);
    log_message("ERROR", "Failed to open file: %s", "config.txt");
    return 0;
}
```

### Exercise 2: Recursion vs. Iteration — Binary Search

Implement binary search using both recursion and iteration, and compare their performance and readability:

```c
#include <stdio.h>

// TODO: Implement recursive binary search
int binary_search_recursive(int arr[], int low, int high, int target) {
    // Base case and recursive step
    return -1;
}

// TODO: Implement iterative binary search
int binary_search_iterative(int arr[], int size, int target) {
    // Loop implementation
    return -1;
}

int main() {
    int data[] = {1, 3, 5, 7, 9, 11, 13};
    int target = 7;
    // Test both functions
    return 0;
}
```

### Exercise 3: Multiple Return Values in Practice

Implement a function that calculates both the maximum and minimum values of an array:

```c
#include <stdio.h>
#include <limits.h>

// TODO: Implement function to find min and max
// Return false if array is empty
bool find_min_max(int arr[], int size, int *min_out, int *max_out) {
    return false;
}

int main() {
    int data[] = {3, 1, 4, 1, 5, 9, 2, 6};
    int min, max;
    if (find_min_max(data, 8, &min, &max)) {
        printf("Min: %d, Max: %d\n", min, max);
    }
    return 0;
}
```

## References

- [cppreference: Function declaration](https://en.cppreference.com/w/c/language/function_declaration)
- [cppreference: stdarg.h](https://en.cppreference.com/w/c/variadic)
