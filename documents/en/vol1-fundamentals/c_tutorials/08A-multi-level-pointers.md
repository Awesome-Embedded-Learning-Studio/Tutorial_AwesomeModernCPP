---
chapter: 1
cpp_standard:
  - 11
description: Understand the memory model of multilevel pointers and where they actually
  get used, tell pointer arrays apart from array pointers, and master the cdecl way
  of reading declarations and combinations of const with multilevel pointers
difficulty: beginner
order: 11
platform: host
prerequisites:
  - Pointers, Arrays, const, and Null Pointers
reading_time_minutes: 9
tags:
  - host
  - cpp-modern
  - beginner
  - 入门
  - 基础
title: Multilevel Pointers and Reading Declarations
translation:
  source: documents/vol1-fundamentals/c_tutorials/08A-multi-level-pointers.md
  source_hash: 4d1618630161b296cd466f3faa861e114aefffc75170339842e2fb833b3a0234
  translated_at: '2026-09-25T13:08:40+00:00'
  engine: anthropic
  token_count: 3800
---
# Multilevel Pointers and Reading Declarations

In the previous article we sorted out how pointers relate to arrays, `const`, and NULL. Now for the twistier parts of pointer country — multilevel pointers (pointers to pointers), the "confusing twins" that are pointer arrays and array pointers, and a method that keeps your brain from crashing when a declaration like `const int* const *` shows up.

Honestly, these things are genuinely easy to mix up at first. But our experience is: don't grind through them by rote memorization. Once you have a systematic method for reading declarations, you can take even the nastiest one apart. What matters more, `unique_ptr<T[]>`, `std::span`, and the pointer hand-off in move semantics are all built on top of these low-level mechanisms.

## Step 1 — Figuring Out What a Multilevel Pointer Actually Points To

### The Memory Model: Links in a Chain

If the address stored inside a pointer points at yet another pointer, you have a multilevel pointer. `int*` points at an `int`, `int**` points at an `int*`, `int***` points at an `int**`, and so on. In memory they look like a chain:

```text
int*** ppp  ──→  int** pp  ──→  int* p  ──→  int value = 42
  0x1000          0x2000         0x3000       0x4000
```

Every level stores the address of the next one. `*ppp` gets you `pp` (an `int**`), `**ppp` gets you `p` (an `int*`), and only `***ppp` reaches the final `42`. Let's verify:

```c
#include <stdio.h>

int main(void)
{
    int value = 42;
    int* p = &value;
    int** pp = &p;
    int*** ppp = &pp;

    printf("value 的地址 = %p\n", (void*)&value);
    printf("p   的值     = %p\n", (void*)p);
    printf("pp  解一次   = %p\n", (void*)*pp);
    printf("ppp 解三次   = %d\n", ***ppp);
    return 0;
}
```

```bash
gcc -Wall -Wextra -std=c17 multi_ptr.c -o multi_ptr && ./multi_ptr
```

The output:

```text
value 的地址 = 0x7ffd1234abcd
p   的值     = 0x7ffd1234abcd
pp  解一次   = 0x7ffd1234abcd
ppp 解三次   = 42
```

Great — each dereference walks one link further down the chain, and at the very end we reach `42`.

### When to Use Multilevel Pointers

Truth be told, anything beyond two levels is rare in a sane project. The most common scenario: you want to **modify the pointer variable itself inside a function** (not the data it points at), so you pass in that pointer's address:

```c
void allocate_buffer(int** out_ptr, int size)
{
    *out_ptr = (int*)malloc(size * sizeof(int));
    // What gets modified is the pointer variable that out_ptr points to
}

int main(void)
{
    int* buffer = NULL;
    allocate_buffer(&buffer, 100);
    // buffer now points at the memory malloc handed out
    free(buffer);
    return 0;
}
```

C is pass-by-value only, so to modify the variable `buffer` itself you must pass `&buffer` — which is an `int**`.

Multilevel pointers are not a flex. Pointers deeper than three levels should not show up in the overwhelming majority of projects — if you catch yourself writing `int****`, the design is most likely at fault. If a struct can wrap it, don't use raw multilevel pointers.

### argv — The Most Common Second-Level Pointer

The `argv` parameter of `main` is a `char**`:

```c
int main(int argc, char *argv[]) { /* ... */ }
int main(int argc, char **argv)    { /* ... */ }  // completely equivalent
```

Inside a parameter list, `char *argv[]` decays to `char**`; the two spellings are identical. `argv` points at an array of `char*`, each element of which points at one command-line argument string, and the whole thing ends with a `NULL` sentinel:

```text
argv
  │
  ▼
  ┌─────┐     ┌─────────────────┐
  │ ptr ├────→│ "./myprogram\0" │  argv[0]
  ├─────┤     └─────────────────┘
  │ ptr ├────→│ "hello\0"       │  argv[1]
  ├─────┤     └─────────────────┘
  │ ptr ├────→│ "world\0"       │  argv[2]
  ├─────┤     └─────────────────┘
  │ NULL │     argv[3] = NULL
  └─────┘
```

## Step 2 — Telling Pointer Arrays and Array Pointers Apart

`int* a[10]` and `int (*a)[10]` differ by a single pair of parentheses, yet they mean completely different things. These are the classic "confusing twins" of C declaration syntax.

### Pointer Array: `int* a[10]`

`int* a[10]` declares an **array** holding 10 elements of type `int*`:

```c
int x = 10, y = 20, z = 30;
int* arr[3] = {&x, &y, &z};

printf("%d %d %d\n", *arr[0], *arr[1], *arr[2]);
// 10 20 30
```

Memory layout — the array stores three pointer values contiguously, and each pointer points at a different `int`:

```text
arr[0]  arr[1]  arr[2]
  │        │       │
  ▼        ▼       ▼
 &x       &y      &z
```

### Array Pointer: `int (*a)[10]`

`int (*a)[10]` declares a **pointer** — one that points at a whole array of 10 `int`s. Its most common use is alongside two-dimensional arrays:

```c
int matrix[3][10] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
    {10, 11, 12, 13, 14, 15, 16, 17, 18, 19},
    {20, 21, 22, 23, 24, 25, 26, 27, 28, 29}
};

int (*row_ptr)[10] = matrix;  // points at the first row
printf("%d\n", (*row_ptr)[2]);         // 2
printf("%d\n", (*(row_ptr + 1))[2]);   // 12 — skips to the second row
```

`row_ptr + 1` skips an entire row (10 `int`s = 40 bytes) and lands on the next row.

`*(row_ptr + 1)[2]` is not the answer you want — `[]` binds tighter than `*`, so that expression first evaluates `(row_ptr + 1)[2]` and then dereferences, and the result is flat-out wrong. The correct spelling needs the parentheses: `(*(row_ptr + 1))[2]`. Precedence is one of the most bug-friendly corners of C.

## Step 3 — Mastering the cdecl Way to Read Declarations

There is a systematic method for reading any C declaration, called the "right-left rule" (also known as the spiral rule). The core of it: **start from the identifier, read to the right first, then to the left, and when you hit a parenthesis, jump out to the next layer**.

Take `int* a[10]`:

1. Find the identifier `a`
2. Right: `[10]` — "a is an array of 10 elements"
3. Left: `int*` — "the element type is pointer to int"
4. Put it together: **a is an array of 10 pointer-to-int elements (a pointer array)**

Take `int (*a)[10]`:

1. The identifier `a`
2. Parentheses block the way right, so go left first: `*` — "a is a pointer"
3. Step out of the parentheses and go right: `[10]` — "pointing at an array of 10 elements"
4. Then left again: `int` — "the element type is int"
5. Put it together: **a is a pointer to an array of 10 int elements (an array pointer)**

Now a function pointer: `int (*func)(double)`

1. The identifier `func`
2. Parentheses block the way, go left: `*` — "func is a pointer"
3. Step out and go right: `(double)` — "pointing at a function taking a double parameter"
4. Left: `int` — "returning int"
5. Put it together: **func is a function pointer, pointing at a function that takes a double and returns an int**

Run through this method a few times and it becomes muscle memory — after that, no strange declaration can faze you. You can also use the online tool [cdecl.org](https://cdecl.org/) to double-check your reading.

In the declaration `int* a, b`, `a` is an `int*` but `b` is merely an `int` — not two pointers. The `*` follows the declarator, not the type. If you genuinely want two pointers, you must write `int *a, *b`. This pitfall has tripped up who knows how many people.

## Step 4 — Combining const with Multilevel Pointers

The combinations of `const` with a single-level pointer were covered in the previous article. Now for the multilevel case — the core principle is unchanged: **`const` qualifies the type immediately to its left (or, if it is leftmost, the type on its right)**.

### Review: Single-Level const Pointers

```c
const int* p1;              // pointer to const int: the value can't be changed through p1, but p1 can be redirected
int* const p2 = &v;         // const pointer: p2 can't be redirected, but the value can be changed through it
const int* const p3 = &v;   // both locked down
```

### Multilevel const Pointers

With `int**` in play, `const` can sit at different positions:

```c
int value = 42;
int* ptr = &value;

// Low-level const: the pointed-to pointer is read-only
int* const* pp1 = &ptr;
// pp1 can change, *pp1 cannot, **pp1 can

// Top-level const: pp2 itself is read-only
int** const pp2 = &ptr;
// pp2 cannot change, *pp2 can, **pp2 can

// Double const
const int* const* pp3 = &ptr;
// pp3 can change, *pp3 cannot, **pp3 cannot
```

You still read these with the right-left rule, peeling off one layer at a time. Take `const int* const* p`: `p` is a pointer → to a `const` pointer → and that pointer points at a `const int`.

You honestly won't run into these much in practice, but knowing how to read them matters — function signatures in the C++ standard library and template error messages regularly feature types of similar complexity.

## Bridging to C++

Every multilevel-pointer mechanism in C has a modern C++ counterpart, and understanding the machinery underneath makes you better with these high-level tools.

`std::unique_ptr<T[]>` manages dynamic arrays automatically — no manual `malloc`/`free` needed. The pain of hand-managing a two-dimensional array with `int**` in C (allocate, free row by row, and it's so easy to forget) collapses into a single line in C++:

```cpp
auto matrix = std::make_unique<int[]>(rows * cols);
// Access it as matrix[i * cols + j]; it is freed automatically when it leaves scope
```

Move semantics is, at its core, a transfer of pointers — instead of copying the data, you "steal" ownership of the resource and null out the source object. That is exactly what manually swapping pointers and nulling the old one in C does; C++ just standardized the pattern.

`std::span<const int>` packs the classic "pointer + length" pair from C functions into one type-safe object — no manual length tracking, and it constructs automatically from plain arrays, `vector`s, and `array`s.

`std::reference_wrapper<int>` provides rebindable reference semantics, standing in for multilevel pointers when you want to store "references" inside containers.

We'll go deep on all of these in the later C++ tutorials. For now, keep just the core idea: **C++'s philosophy is to let the type system manage resources automatically instead of relying on programmer discipline**.

## Exercises

### Exercise: Allocating and Freeing a Dynamic Two-Dimensional Array

**Difficulty: Intermediate** · manage a dynamic two-dimensional array with int** (pay attention to how you roll back and free if one row's malloc fails)

Use multilevel pointers to implement the allocation, filling, and freeing of a dynamic two-dimensional array. Implement the following three functions yourself:

```c
/// @brief Allocate a dynamic rows x cols two-dimensional array
/// @param rows the number of rows
/// @param cols the number of columns
/// @return a second-level pointer to the two-dimensional array, or NULL on failure
int** allocate_matrix(int rows, int cols);

/// @brief Free the dynamic two-dimensional array
/// @param matrix the second-level pointer
/// @param rows the number of rows (used to free row by row)
void free_matrix(int** matrix, int rows);

/// @brief Fill every element of the two-dimensional array with the given value
/// @param matrix the second-level pointer
/// @param rows the number of rows
/// @param cols the number of columns
/// @param value the fill value
void fill_matrix(int** matrix, int rows, int cols, int value);
```

::: details Reference solution

```c
#include <stdio.h>
#include <stdlib.h>

int** allocate_matrix(int rows, int cols) {
    if (rows < 1 || cols < 1) {
        return NULL;
    }

    int** matrix = malloc(sizeof(int*) * rows);
    if (matrix == NULL) {
        return NULL;    // Guard against allocation failure. Unlikely... but just in case — what if the program runs on hardware from 1978? (laughs)
    }

    for (int i = 0; i < rows; i++) {
        matrix[i] = malloc(sizeof(int) * cols);
        if (matrix[i] == NULL) {    // Out of memory: cancel the allocation and free what we already got.
            for (int j = 0; j < i; j++) {
                free(matrix[j]);
            }
            free(matrix);
            return NULL;
        }
    }
    return matrix;
}

void free_matrix(int** matrix, int rows) {
    if (matrix == NULL) {
        return;
    }

    for (int i = 0; i < rows; i++) {
        free(matrix[i]);
    }
    free(matrix);
}

void fill_matrix(int** matrix, int rows, int cols, int value) {
    if (rows < 1 || cols < 1 || matrix == NULL) {
        return;
    }

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            matrix[i][j] = value;
        }
    }
}

```

```text
Feeling inspired, so here comes a diagram awa

matrix has type int**, so it takes two dereferences to actually reach an int:

  matrix  (int**)
    │
    │   matrix[0]    matrix[1]    matrix[2]    ...    matrix[n]      ← 1st dereference: each one is an int*
    │      │            │            │                   │
    │      ▼            ▼            ▼                   ▼
    └─▶ ┌─┬─┬─┬─┐   ┌─┬─┬─┬─┐    ┌─┬─┬─┬─┐         ┌─┬─┐
         │ │ │ │ │   │ │ │ │ │    │ │ │ │ │   ...   │ │ │   ← 2nd dereference: each one is an int
         └─┴─┴─┴─┘   └─┴─┴─┴─┘    └─┴─┴─┴─┘         └─┴─┘
             ↑
             matrix[0][1] is exactly this cell (the int in row 0, column 1)

  - matrix[i]     : int*, pointing at the start of row i (that block of cols ints)
  - matrix[i][j]  : int, the concrete value in row i, column j

Everything clear now OwO?
```

Hint: allocate the pointer array first (the dimension the `int**` points at), then `malloc` each row separately. Freeing reverses the order — release every row first, then the pointer array itself.

:::

## References

- [C Declaration Syntax - cppreference](https://en.cppreference.com/w/c/language/declarations)
- [cdecl: A Tool That Translates C Declarations](https://cdecl.org/)
