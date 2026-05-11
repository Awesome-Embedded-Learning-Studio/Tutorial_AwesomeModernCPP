# Function Basics and Parameter Passing

So far, all the code we've written has been stuffed into the `main` function. But real-world programs don't work like this — a project can easily reach tens of thousands of lines of code, and cramming everything into a single function makes it practically unmaintainable. Functions are the fundamental unit of modular programming in C: we encapsulate a piece of logic, give it a name, and call it whenever we need it.

This sounds simple, but the mechanisms behind functions — how parameters are passed in, how return values come back, and how stack frames operate — must be thoroughly understood. Otherwise, we will feel confused later when learning about C++ reference passing, function overloading, and templates.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Correctly declare, define, and call C functions
> - [ ] Understand the essence of C's pass-by-value nature
> - [ ] Master the technique of achieving multiple return values via pointers
> - [ ] Understand the principles of recursion and the risk of stack overflow

## Environment Setup

We will conduct all the following experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -Werror -std=c17 -O2`

## Step 1 — Function Declaration and Definition

### Declare First, Use Later

The C compiler processes code from top to bottom. If we call a function inside `main`, but that function is defined after `main`, the compiler doesn't know the function exists when it reaches the call site. Therefore, we need a **function declaration** (also known as a function prototype) to tell the compiler the function's "signature" in advance — the parameter types and return type:

```c
#include <stdio.h>

// Function declaration (prototype)
int add(int a, int b);

int main(void) {
    int result = add(3, 5);
    printf("3 + 5 = %d\n", result);
    return 0;
}

// Function definition
int add(int a, int b) {
    return a + b;
}
```

Let's verify this by compiling and running:

```text
$ gcc -Wall -Wextra -Werror -std=c17 -O2 -o add add.c
$ ./add
3 + 5 = 8
```

Result:

```text
3 + 5 = 8
```

In real projects, function declarations are usually placed in header files (`.h`), and function definitions are placed in source files (`.c`). Other files that need to call the function simply `#include` the corresponding header — this is the basic pattern of modularity, which we already saw in the compilation basics chapter.

Parameter names in function prototypes can be omitted (keeping only the types), but retaining the names is a better practice — it acts as documentation, letting anyone reading the code know at a glance what each parameter is for.

## Step 2 — C Only Has Pass-by-Value

This is the most critical point for understanding C functions: **C only has pass-by-value**. All parameters are copied when passed. The function receives a copy of the original data, and modifying the copy does not affect the original data.

### Copies Remain Unchanged — The Safety of Pass-by-Value

```c
#include <stdio.h>

void try_modify(int x) {
    x = 100;  // Only modifies the local copy
    printf("Inside function: x = %d\n", x);
}

int main(void) {
    int a = 42;
    try_modify(a);
    printf("Outside function: a = %d\n", a);
    return 0;
}
```

`try_modify` receives a copy of `a` (the parameter `x`), and modifying `x` does not affect the outer `a`. This might look like it "didn't work," but from another perspective — it also means the function won't accidentally modify the caller's data. This is a form of safety protection.

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
    printf("Before: x = %d, y = %d\n", x, y);
    swap(&x, &y);
    printf("After:  x = %d, y = %d\n", x, y);
    return 0;
}
```

`swap` receives the addresses of `x` and `y` (a value copy of the pointers), and then uses dereferencing (`*pa`) to directly read and write that memory. The pointers themselves are copies, but the memory they point to is the original data.

Let's verify this:

```text
$ gcc -Wall -Wextra -Werror -std=c17 -O2 -o swap swap.c
$ ./swap
```

Result:

```text
Before: x = 10, y = 20
After:  x = 20, y = 10
```

> ⚠️ **Pitfall Warning**
> When passing large structures by value, the entire block of data gets copied — wasting both stack space and time. We should pass a pointer (usually a `const` pointer), copying only an address (4 or 8 bytes) to give the function access to the entire structure.

## Step 3 — Return Values and Multiple Return Values

A C function can only return one value. If we need to return multiple results, there are two common techniques.

### Method 1: "Returning" via Pointer Parameters

```c
#include <stdio.h>

// Returns quotient via pointer, function return value is the remainder
int divide(int a, int b, int *quotient) {
    if (b == 0) {
        return -1;  // Error: division by zero
    }
    *quotient = a / b;
    return a % b;
}

int main(void) {
    int q, r;
    r = divide(17, 5, &q);
    printf("17 / 5 = %d remainder %d\n", q, r);
    return 0;
}
```

This is a very common C pattern — values that need to be "returned" are passed out via pointer parameters, while the function's actual return value is typically used to indicate success or failure.

### Method 2: Returning a Structure

```c
#include <stdio.h>

typedef struct {
    int quotient;
    int remainder;
} DivResult;

DivResult divide(int a, int b) {
    DivResult result = {0, -1};  // Default: error
    if (b != 0) {
        result.quotient = a / b;
        result.remainder = a % b;
    }
    return result;
}

int main(void) {
    DivResult r = divide(17, 5);
    if (r.remainder != -1) {
        printf("17 / 5 = %d remainder %d\n", r.quotient, r.remainder);
    }
    return 0;
}
```

Modern compilers have excellent optimizations for returning structures (return value optimization (RVO)), so this usually doesn't incur extra copy overhead.

## Step 4 — Recursion: A Function Calling Itself

### What Is Recursion

When a function calls itself directly or indirectly, that is recursion. The essence of recursion is breaking a problem down into smaller subproblems of the same type. As an analogy: if you want to count how many cards are in a deck, you can count the top card (1), then recursively count the rest (N-1 cards), and the final result is 1 + (N-1) = N.

```c
#include <stdio.h>

int factorial(int n) {
    if (n <= 1) {
        return 1;  // Base case
    }
    return n * factorial(n - 1);  // Recursive case
}

int main(void) {
    printf("5! = %d\n", factorial(5));
    return 0;
}
```

Recursion call chain: `factorial(5)` → `factorial(4)` → `factorial(3)` → ... → `factorial(1)`

Each recursive call allocates a new stack frame on the stack (saving local variables, parameters, and the return address), so the recursion depth is limited by the stack size — this is why recursion can potentially lead to stack overflow.

Let's verify this:

```text
$ gcc -Wall -Wextra -Werror -std=c17 -O2 -o factorial factorial.c
$ ./factorial
```

Result:

```text
5! = 120
```

> ⚠️ **Pitfall Warning**
> The biggest risk with recursion is **stack overflow**. Each recursive call consumes stack space. If the recursion depth is too large (e.g., `factorial(100000)`), the stack space is exhausted and the program crashes immediately. For scenarios involving deep recursion, manually converting to an iterative loop is safer.

### Tail Recursion

If a recursive call is the very last operation in a function, it satisfies the form of tail recursion. Theoretically, the compiler can optimize tail recursion into a loop, avoiding the accumulation of stack frames:

```c
// Tail-recursive version of factorial
int factorial_tail(int n, int accumulator) {
    if (n <= 1) {
        return accumulator;  // Base case
    }
    return factorial_tail(n - 1, n * accumulator);  // Tail call
}

// Wrapper function for convenient calling
int factorial(int n) {
    return factorial_tail(n, 1);
}
```

However, note that the C standard does not guarantee that the compiler will perform tail recursion optimization. In scenarios with deep recursion, manually converting to iteration is safer.

## Step 5 — Variadic Functions

Some functions have a variable number of arguments — the most typical example is `printf`. C provides the mechanism for variadic functions through `<stdarg.h>`:

```c
#include <stdio.h>
#include <stdarg.h>

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

The usage of the variadic argument mechanism follows four steps: `va_list` declares the argument list → `va_start` initializes it → `va_arg` retrieves arguments one by one → `va_end` cleans up.

> ⚠️ **Pitfall Warning**
> Variadic arguments have no type checking — if we pass a `double` but retrieve it with `va_arg(args, int)`, the compiler won't report an error, but the value retrieved at runtime will be wrong. There is also no argument count checking — we must tell the function how many arguments there are through some means. This is the most dangerous aspect of C's variadic arguments.

## Bridging to C++

C++ makes comprehensive enhancements to functions. The most direct change is **reference passing** — `&` makes parameter passing both efficient and intuitive, eliminating the need for manual address-of and dereferencing.

C++ also supports **function overloading** — functions with the same name can have different parameter lists, and the compiler automatically selects the correct one based on the argument types at the call site. This solves the naming bloat problem in C, such as `abs_int`, `abs_long`, `abs_float`. **Variadic templates**, introduced in C++11, are a type-safe variadic mechanism that perfectly replaces C's `<stdarg.h>`.

The `constexpr` function allows functions to execute at compile time — if the arguments are compile-time constants, the function's result is also a compile-time constant. This is much safer than C macros.

## Summary

Functions are the foundation of modular programming in C. Understanding the essence of pass-by-value — all parameters are copies — is a prerequisite for mastering pointer parameters and multiple return value techniques. If we need to modify the caller's variable, pass a pointer; for large structures, pass a `const` pointer. Recursion is elegant, but we must watch out for stack overflow. Variadic arguments provide flexibility but lack type safety.

At this point, we have mastered the basic usage of functions. The next question arises — how are variable scope and lifetime managed? What is the `static` keyword actually for? These are the topics we will discuss in the next chapter.

## Exercises

### Exercise 1: Variadic Log Function

Implement a custom log function that supports log levels and formatted strings:

```c
#include <stdio.h>
#include <stdarg.h>

// TODO: Implement this function
// Level: 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR
void log_msg(int level, const char *fmt, ...) {
    // Your code here
}

int main(void) {
    log_msg(0, "value = %d", 42);
    log_msg(2, "warning: %s", "low battery");
    log_msg(3, "error code: %d", -1);
    return 0;
}
```

### Exercise 2: Recursion vs. Iteration — Binary Search

Implement binary search using both recursion and iteration, and compare their performance and readability:

```c
#include <stdio.h>

// TODO: Recursive version
int binary_search_rec(const int *arr, int left, int right, int target) {
    // Your code here
    return -1;
}

// TODO: Iterative version
int binary_search_iter(const int *arr, int size, int target) {
    // Your code here
    return -1;
}

int main(void) {
    int data[] = {2, 5, 8, 12, 16, 23, 38, 56, 72, 91};
    int size = sizeof(data) / sizeof(data[0]);

    int target = 23;
    printf("Recursive: index = %d\n", binary_search_rec(data, 0, size - 1, target));
    printf("Iterative: index = %d\n", binary_search_iter(data, size, target));
    return 0;
}
```

### Exercise 3: Multiple Return Values in Practice

Implement a function that simultaneously calculates the maximum and minimum values of an array:

```c
#include <stdio.h>
#include <limits.h>

// TODO: Implement this function
// Returns 0 on success, -1 if array is empty
int find_min_max(const int *arr, int size, int *min_out, int *max_out) {
    // Your code here
    return -1;
}

int main(void) {
    int data[] = {3, 7, 1, 9, 4, 6, 2, 8, 5};
    int size = sizeof(data) / sizeof(data[0]);

    int min, max;
    if (find_min_max(data, size, &min, &max) == 0) {
        printf("Min = %d, Max = %d\n", min, max);
    }
    return 0;
}
```

## References

- [cppreference: Function declaration](https://en.cppreference.com/w/c/language/function_declaration)
- [cppreference: stdarg.h](https://en.cppreference.com/w/c/variadic)
